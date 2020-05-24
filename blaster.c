/* USB Blaster Gadget for Raspberry Pi Zero */

#define PATH_LEN    256

// Blaster input bits
#define BIT_TDO     0x01
#define BIT_ASO     0x02
// Blaster output bits
#define BIT_TCK     0x01
#define BIT_TMS     0x02
#define BIT_NCE     0x04
#define BIT_NCS     0x08
#define BIT_TDI     0x10
#define BITS_PORT   0x1F
// Protocol bits
#define BIT_RD      0x40
#define BIT_SEQ     0x80
#define BITS_CNT    0x3F
// GPIO Pins
#define PIN_TCK     0
#define PIN_TMS     1
#define PIN_NCE     2
#define PIN_NCS     3
#define PIN_TDI     4
#define PIN_TDO     5
#define PIN_ASO     6
#define PIN_CNT     7

#include "diag.h"
#include "gpio.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <linux/usb/functionfs.h>

typedef enum {false, true} bool;

static char sRoot[PATH_LEN] = "/sys/kernel/config/usb_gadget/";
static int nRootLen = 0;
static char sFFS[PATH_LEN] = "/var/run/blaster";
static int nFFSLen = 0;
static int ep0 = -1;
static int ep1 = -1;
static int ep2 = -1;
static int iJTAGPin[PIN_CNT] = { 0, 0, 0, 0, 0, 0, 0 };
static int iJTAGInp = 0;
static int iJTAGOut = 0;

void GadgetName (const char *psName)
    {
    strcat (sRoot, psName);
    diag_message (DIAG_USB, "Create gadget folder %s\n", sRoot);
    if ( mkdir (sRoot, 0777) < 0 ) diag_message (DIAG_SYSERR, "Error creating folder\n");
    strcat (sRoot, "/");
    nRootLen = strlen (sRoot);
    }

void WriteConfig (const char *psPath, const char *psValue)
    {
    strcpy (&sRoot[nRootLen], psPath);
    diag_message (DIAG_USB, "%s = %s\n", sRoot, psValue);
    int fd = open (sRoot, O_WRONLY);
    if ( fd < 0 ) diag_message (DIAG_SYSERR, "Error opening gadget configuration file\n");
    int nLen = strlen (psValue);
    if ( write (fd, psValue, nLen) != nLen )
        diag_message (DIAG_SYSERR, "Error writing gadget configuration file\n");
    if ( close (fd) < 0 ) diag_message (DIAG_SYSERR, "Error closing gadget configuration file\n");
    sRoot[nRootLen] = '\0';
    }

void MakeDirs (const char *psPath)
    {
    strcpy (&sRoot[nRootLen], psPath);
    diag_message (DIAG_USB, "Create gadget folder %s\n", sRoot);
    char *ps = strchr (&sRoot[nRootLen], '/');
    while (ps != NULL)
        {
        *ps = '\0';
        if ( mkdir (sRoot, 0777) < 0 )
            {
            if ( errno != EEXIST ) diag_message (DIAG_SYSERR, "Error creating %s\n", sRoot);
            }
        *ps = '/';
        ps = strchr (ps + 1, '/');
        }
    if ( mkdir (sRoot, 0777) < 0 ) diag_message (DIAG_SYSERR, "Error creating folder\n");
    sRoot[nRootLen] = '\0';
    }

void Assign (const char *psTarget, const char *psPath)
    {
    char sTarget[PATH_LEN];
    strcpy (sTarget, sRoot);
    strcpy (&sRoot[nRootLen], psPath);
    strcpy (&sTarget[nRootLen], psTarget);
    const char *ps = strrchr (psTarget, '/');
    if ( ps == NULL )
        {
        strcat (&sRoot[nRootLen], "/");
        strcat (&sRoot[nRootLen], psTarget);
        }
    else
        {
        strcat (&sRoot[nRootLen], ps);
        }
        
    diag_message (DIAG_USB, "Assign %s to %s\n", sTarget, sRoot);
    if ( symlink (sTarget, sRoot) < 0 ) diag_message (DIAG_SYSERR, "Error creating link\n");
    sRoot[nRootLen] = '\0';
    }

void DescInt (unsigned char **pptr, int value, int size)
    {
    int size2 = size;
    diag_message (DIAG_USB, "DescInt: *pptr = %p, value = %d, size = %d\n", *pptr, value, size);
    unsigned char *ptr = *pptr;
    while ( size > 0 )
        {
        *ptr = (unsigned char)( value & 0xFF );
        ++ptr;
        value >>= 8;
        --size;
        }
    for (int i = 0; i < size2; ++i) diag_message (DIAG_USB, " %02X", (*pptr)[i]);
    diag_message (DIAG_USB, "\n");
    *pptr = ptr;
    }

/*
 * Descriptors format:
 *
 * | off | name      | type         | description                          |
 * |-----+-----------+--------------+--------------------------------------|
 * |   0 | magic     | LE32         | FUNCTIONFS_DESCRIPTORS_MAGIC_V2      |
 * |   4 | length    | LE32         | length of the whole data chunk       |
 * |   8 | flags     | LE32         | combination of functionfs_flags      |
 * |     | eventfd   | LE32         | eventfd file descriptor              |
 * |     | fs_count  | LE32         | number of full-speed descriptors     |
 * |     | hs_count  | LE32         | number of high-speed descriptors     |
 * |     | ss_count  | LE32         | number of super-speed descriptors    |
 * |     | os_count  | LE32         | number of MS OS descriptors          |
 */
void DescHead (unsigned char **pptr, int eventfd, int fs_count, int hs_count, int ss_count, int os_count)
    {
    int flags = 0;
    DescInt (pptr, FUNCTIONFS_DESCRIPTORS_MAGIC_V2, 4);
    DescInt (pptr, 0, 4);        // Reserve space for length
    unsigned char *ptrFlg = *pptr;
    DescInt (pptr, flags, 4);    // Reserve space for flags
    if ( eventfd > 0 )
        {
        flags |= FUNCTIONFS_EVENTFD;
        DescInt (pptr, eventfd, 4);
        }
    if ( fs_count > 0 )
        {
        flags |= FUNCTIONFS_HAS_FS_DESC;
        DescInt (pptr, fs_count, 4);
        }
    if ( hs_count > 0 )
        {
        flags |= FUNCTIONFS_HAS_HS_DESC;
        DescInt (pptr, hs_count, 4);
        }
    if ( ss_count > 0 )
        {
        flags |= FUNCTIONFS_HAS_SS_DESC;
        DescInt (pptr, ss_count, 4);
        }
    if ( os_count > 0 )
        {
        flags |= FUNCTIONFS_HAS_MS_OS_DESC;
        DescInt (pptr, os_count, 4);
        }
    DescInt (&ptrFlg, flags, 4);  // Re-write flags
    }

/* struct usb_interface_descriptor {
 * 	__u8  bLength;
 * 	__u8  bDescriptorType;
 * 	__u8  bInterfaceNumber;
 * 	__u8  bAlternateSetting;
 * 	__u8  bNumEndpoints;
 * 	__u8  bInterfaceClass;
 * 	__u8  bInterfaceSubClass;
 * 	__u8  bInterfaceProtocol;
 * 	__u8  iInterface;
 * } __attribute__((packed));
 */

void DescInterface (unsigned char **pptr, int num, int settings, int endpoints, int class,
    int subclass, int protocol, int iface)
    {
    DescInt (pptr, sizeof (struct usb_interface_descriptor), 1);
    DescInt (pptr, USB_DT_INTERFACE, 1);
    DescInt (pptr, num, 1);
    DescInt (pptr, settings, 1);
    DescInt (pptr, endpoints, 1);
    DescInt (pptr, class, 1);
    DescInt (pptr, subclass, 1);
    DescInt (pptr, protocol, 1);
    DescInt (pptr, iface, 1);
    }

/* struct usb_endpoint_descriptor_no_audio {
 * 	__u8  bLength;
 * 	__u8  bDescriptorType;
 * 	__u8  bEndpointAddress;
 * 	__u8  bmAttributes;
 * 	__le16 wMaxPacketSize;
 * 	__u8  bInterval;
 * } __attribute__((packed));
 */

void DescEndpoint (unsigned char **pptr, int addr, bool bSend, int attr, int max_size, int interval)
    {
    DescInt (pptr, sizeof (struct usb_endpoint_descriptor_no_audio), 1);
    DescInt (pptr, USB_DT_ENDPOINT, 1);
    DescInt (pptr, addr | (bSend ? USB_DIR_IN /* to host */: USB_DIR_OUT /* to device */) , 1);
    DescInt (pptr, attr, 1);
    DescInt (pptr, max_size, 2);
    DescInt (pptr, interval, 1);
    }

void DescLength (unsigned char *ptrStart, unsigned char *ptrEnd)
    {
    int nLen = ptrEnd - ptrStart;
    ptrStart += sizeof (__le32);
    DescInt (&ptrStart, nLen, 4);
    }

void WriteDesc (int ep0)
    {
    int nLen = sizeof (struct usb_functionfs_descs_head_v2)
        + sizeof (__le32)   // fs_count
        + sizeof (struct usb_interface_descriptor)
        + 2 * sizeof (struct usb_endpoint_descriptor_no_audio);
    unsigned char *ptrDesc = (unsigned char *) malloc (nLen);
    if ( ptrDesc == NULL )
        diag_message (DIAG_FATAL, "Unable to allocate memory for endpoint definitions\n");
    unsigned char *ptr = ptrDesc;
    DescHead (&ptr, 0, 3, 0, 0, 0);
    DescInterface (&ptr, 0, 0, 2, 255, 255, 255, 0);
    DescEndpoint (&ptr, 1, true,
        USB_ENDPOINT_XFER_BULK | USB_ENDPOINT_SYNC_NONE | USB_ENDPOINT_USAGE_DATA,
        64, 1);
    DescEndpoint (&ptr, 2, false,
        USB_ENDPOINT_XFER_BULK | USB_ENDPOINT_SYNC_NONE | USB_ENDPOINT_USAGE_DATA,
        64, 1);
    DescLength (ptrDesc, ptr);
    diag_message (DIAG_USB, "Write endpoint definitions: ep0 = %d, nLen = %d:\n", ep0, nLen);
    for (int i = 0; i < nLen; ++i) diag_message (DIAG_USB, " %02X", ptrDesc[i]);
    diag_message (DIAG_USB, "\n");
    if ( write (ep0, ptrDesc, nLen) < nLen )
        diag_message (DIAG_SYSERR, "Error writing endpoint definitions\n");
    free (ptrDesc);
    }

/*
 * Strings format:
 *
 * | off | name       | type                  | description                |
 * |-----+------------+-----------------------+----------------------------|
 * |   0 | magic      | LE32                  | FUNCTIONFS_STRINGS_MAGIC   |
 * |   4 | length     | LE32                  | length of the data chunk   |
 * |   8 | str_count  | LE32                  | number of strings          |
 * |  12 | lang_count | LE32                  | number of languages        |
 * |  16 | stringtab  | StringTab[lang_count] | table of strings per lang  |
 *
 * For each language there is one stringtab entry (ie. there are lang_count
 * stringtab entires).  Each StringTab has following format:
 *
 * | off | name    | type              | description                        |
 * |-----+---------+-------------------+------------------------------------|
 * |   0 | lang    | LE16              | language code                      |
 * |   2 | strings | String[str_count] | array of strings in given language |
 *
 * For each string there is one strings entry (ie. there are str_count
 * string entries).  Each String is a NUL terminated string encoded in
 * UTF-8.
 */

void DescStringsHead (unsigned char **pptr, int nLang, int nString)
    {
    DescInt (pptr, FUNCTIONFS_STRINGS_MAGIC, 4);
    DescInt (pptr, 0, 4);        // Reserve space for length
    DescInt (pptr, nString, 4);
    DescInt (pptr, nLang, 4);
    }

void DescString (unsigned char **pptr, const unsigned char *psString)
    {
    unsigned char *ptr = *pptr;
    while ( *psString ) *(ptr++) = (unsigned char) *(psString++);
    *(ptr++) = 0;
    *pptr = ptr;
    }

void WriteStrings (int ep0)
    {
    static const char *psString = "USB-Blaster";
    int nLen = sizeof (struct usb_functionfs_strings_head)
        + sizeof (__le16)           // language code
        + strlen (psString) + 1;    // Include NULL byte
    unsigned char *ptrDesc = (unsigned char *) malloc (nLen);
    if ( ptrDesc == NULL )
        diag_message (DIAG_FATAL, "Unable to allocate memory for endpoint strings\n");
    unsigned char *ptr = ptrDesc;
    DescStringsHead (&ptr, 1, 1);
    DescInt (&ptr, 0x0409, 2);      // US-English
    DescString (&ptr, psString);
    DescLength (ptrDesc, ptr);
    diag_message (DIAG_USB, "Write endpoint strings: ep0 = %d, nLen = %d:\n", ep0, nLen);
    for (int i = 0; i < nLen; ++i) diag_message (DIAG_USB, " %02X", ptrDesc[i]);
    diag_message (DIAG_USB, "\n");
    if ( write (ep0, ptrDesc, nLen) < nLen )
        diag_message (DIAG_SYSERR, "Error writing endpoint strings\n");
    free (ptrDesc);
    }

int OpenEndpoint (const char *psPath, int flags)
    {
    if ( nFFSLen == 0 ) nFFSLen = strlen (sFFS);
    sFFS[nFFSLen] = '/';
    strcpy (&sFFS[nFFSLen + 1], psPath);
    int fd = open (sFFS, flags);
    diag_message (( fd > 0 ) ? DIAG_USB: DIAG_SYSERR, "Open endpoint %s = %d\n", sFFS, fd);
    sFFS[nFFSLen] = '\0';
    return fd;
    }

const char * GetUDCDevice (void)
    {
    struct dirent *pdent;
    DIR *pdir = opendir ("/sys/class/udc");
    if ( pdir == NULL ) diag_message (DIAG_SYSERR, "Unable to open /sys/class/udc\n");
    while (true)
        {
        pdent = readdir (pdir);
        if ( pdent == NULL ) break;
        if ( pdent->d_name[0] != '.' ) break;
        }
    if ( closedir (pdir) < 0 ) diag_message (DIAG_SYSERR, "Error closing /sys/class/udc\n");
    if ( pdent == NULL ) diag_message (DIAG_SYSERR, "No UDC device\n");
    diag_message (DIAG_USB, "UDC = %s\n", pdent->d_name);
    return pdent->d_name;
    }

void GadgetConfig (void)
    {
    diag_message (DIAG_USB, "Configure gadget\n");
    GadgetName ("blaster");
    WriteConfig ("idVendor", "0x09fb");
    WriteConfig ("idProduct", "0x6001");
    MakeDirs ("strings/0x409");
    WriteConfig ("strings/0x409/manufacturer", "Altera");
    WriteConfig ("strings/0x409/product", "USB-Blaster");
    MakeDirs ("configs/default.1");
    MakeDirs ("functions/ffs.blaster");
    Assign ("functions/ffs.blaster", "configs/default.1");
    diag_message (DIAG_USB, "Mount function filesystem at %s\n", sFFS);
    if ( mkdir (sFFS, 0777) < 0 )
        {
        if ( errno != EEXIST ) diag_message (DIAG_SYSERR, "Error creating folder\n", sFFS);
        }
    if ( mount ("blaster", sFFS, "functionfs", 0, "") < 0 )
        diag_message (DIAG_SYSERR, "Error mounting function filesystem\n");
    ep0 = OpenEndpoint ("ep0", O_RDWR | O_NONBLOCK);
    WriteDesc (ep0);
    WriteStrings (ep0);
    WriteConfig ("UDC", GetUDCDevice ());
    ep1 = OpenEndpoint ("ep1", O_WRONLY | O_NONBLOCK);
    ep2 = OpenEndpoint ("ep2", O_RDONLY | O_NONBLOCK);
    }

bool JTAG_Init (void)
    {
    for (int i = PIN_TCK; i <= PIN_TDI; ++i) iJTAGInp |= iJTAGPin[i];
    for (int i = PIN_TDO; i <= PIN_ASO; ++i) iJTAGOut |= iJTAGPin[i];
    if ( gpio_init () != GPIO_OK ) return false;
    gpio_input (iJTAGInp);
    gpio_pullup (iJTAGInp);
    gpio_put (iJTAGOut, iJTAGPin[PIN_TMS] | iJTAGPin[PIN_NCS] | iJTAGPin[PIN_NCE] | iJTAGPin[PIN_TDI]);
    gpio_output (iJTAGOut);
    return true;
    }

void JTAG_WR (unsigned char u)
    {
    int iOut = 0;
    for (int i = PIN_TCK; i <= PIN_TDI; ++i)
        {
        if ( u & 0x01 ) iOut |= iJTAGPin[i];
        u >>= 1;
        }
    gpio_put (iJTAGOut, iOut);
    usleep (10);
    }

unsigned char JTAG_RD (void)
    {
    int iIn = gpio_get (iJTAGInp);
    unsigned char u = 0;
    if ( iIn & iJTAGPin[PIN_TDO] ) u |= BIT_TDO;
    if ( iIn & iJTAGPin[PIN_ASO] ) u |= BIT_ASO;
    return u;
    }

void Blast (void)
    {
    struct usb_functionfs_event event;
    static struct timespec tsNow;
    static struct timespec tsLast;
    unsigned char uIn[64];
    unsigned char uOut[66];
    unsigned char uPort = BIT_TMS | BIT_NCE | BIT_NCS | BIT_TDI;
    unsigned char uRead = BIT_TDO;
    int nSeq = 0;
    int nIn = 0;
    int nOut = 2;
    bool bRun = false;
    bool bRead;
    uOut[0] = 0x31;
    uOut[1] = 0x60;
    clock_gettime (CLOCK_MONOTONIC, &tsLast);
    while (true)
        {
        if ( read (ep0, &event, sizeof (event)) > 0 )
            {
            switch (event.type)
                {
                case FUNCTIONFS_BIND:
                    diag_message (DIAG_USB, "ep0: Bind\n");
                    break;
                case FUNCTIONFS_UNBIND:
                    diag_message (DIAG_USB, "ep0: Unind\n");
                    break;
                case FUNCTIONFS_ENABLE:
                    diag_message (DIAG_USB, "ep0: Enable\n");
                    break;
                case FUNCTIONFS_DISABLE:
                    diag_message (DIAG_USB, "ep0: Disable\n");
                    break;
                case FUNCTIONFS_SETUP:
                    diag_message (DIAG_USB, "ep0: Setup\n");
                    break;
                case FUNCTIONFS_SUSPEND:
                    diag_message (DIAG_USB, "ep0: Suspend\n");
                    break;
                case FUNCTIONFS_RESUME:
                    diag_message (DIAG_USB, "ep0: Resume\n");
                    break;
                }
            switch (event.type)
                {
                case FUNCTIONFS_SETUP:
                case FUNCTIONFS_RESUME:
                    bRun = true;
                    break;
                case FUNCTIONFS_BIND:
                case FUNCTIONFS_UNBIND:
                case FUNCTIONFS_ENABLE:
                case FUNCTIONFS_DISABLE:
                case FUNCTIONFS_SUSPEND:
                    bRun = false;
                    break;
                }
            }
        if ( ! bRun ) continue;
        clock_gettime (CLOCK_MONOTONIC, &tsNow);
        long te = 1000000000UL * (tsNow.tv_sec - tsLast.tv_sec) + tsNow.tv_nsec - tsLast.tv_nsec;
        if ( ( nOut > 2 ) || ( te > 1000000UL ) )
            {
            int nWrite = write (ep1, uOut, sizeof (uOut));
            diag_message (DIAG_JTAG, "Write %d:", nWrite);
            for (int i = 0; i < nWrite; ++i) diag_message (DIAG_JTAG, " %02X", uOut[i]);
            diag_message (DIAG_JTAG, "\n");
            if ( nWrite > 2 )
                {
                if ( nWrite < nOut ) memcpy (&uOut[2], &uOut[nWrite], nOut - nWrite);
                nOut -= nWrite - 2;
                }
            tsLast.tv_sec = tsNow.tv_sec;
            tsLast.tv_nsec = tsNow.tv_nsec;
            }
        if ( nOut > 2 ) continue;
        nIn = read (ep2, uIn, sizeof (uIn));
        if ( nIn > 0 )
            {
            diag_message (DIAG_JTAG, "Read %d:", nIn);
            for (int i = 0; i < nIn; ++i)
                {
                diag_message (DIAG_JTAG, " %02X", uIn[i]);
                if ( nSeq > 0 )
                    {
                    unsigned char uSend = uIn[i];
                    unsigned char uRecv = 0;
                    for (int j = 0; j < 8; ++j)
                        {
                        if (bRead)
                            {
                            uRecv >>= 1;
                            if (JTAG_RD () & uRead) uRecv |= 0x80;
                            }
                        if ( uSend & 0x01 ) uPort |= BIT_TDI;
                        else uPort &= ~ BIT_TDI;
                        JTAG_WR (uPort);
                        }
                    if ( bRead ) uOut[nOut++] = uRecv;
                    --nSeq;
                    }
                else
                    {
                    bRead = uIn[i] & BIT_RD;
                    if ( uIn[i] & BIT_SEQ )
                        {
                        nSeq = uIn[i] & BITS_CNT;
                        uPort &= ~ BIT_TCK;
                        if ( uPort & BIT_NCS ) uRead = BIT_TDO;
                        else uRead = BIT_ASO;
                        }
                    else
                        {
                        uPort = uIn[i] & BITS_PORT;
                        if ( bRead ) uOut[nOut++] = JTAG_RD ();
                        }
                    JTAG_WR (uPort);
                    }
                if ( ++nOut >= sizeof (uOut) )
                    {
                    write (ep1, uOut, sizeof (uOut));
                    nOut = 2;
                    }
                }
            }
        diag_message (DIAG_JTAG, "\n");
        }
    }

bool SetPin (int nArg, const char *psArg[], int *piArg)
    {
    static const char *psPin[] = { "-tck", "-tms", "-nce", "-ncs", "-tdi", "-tdo", "-aso" };
    for (int i = 0; i < PIN_CNT; ++i)
        {
        if ( strncmp (psArg[*piArg], psPin[i], 4) == 0 )
            {
            int nPin = 0;
            if ( psArg[*piArg][4] == '=' )
                {
                nPin = atoi (&psArg[*piArg][5]);
                }
            else if ( *piArg < nArg - 1 )
                {
                ++(*piArg);
                nPin = atoi (psArg[*piArg]);
                }
            else
                {
                return false;
                }
            iJTAGPin[i] = 1 << nPin;
            return true;
            }
        }
    return false;
    }

bool SetString (int nArg, const char *psArg[], int *piArg, const char *psSwitch, int nDest, char *psDest)
    {
    const char *psSrc;
    int nLen = strlen (psSwitch);
    if ( strncmp (psArg[*piArg], psSwitch, nLen) == 0 )
        {
        if ( psArg[*piArg][nLen] == '=' )
            {
            psSrc = &psArg[*piArg][nLen+1];
            }
        else if ( *piArg < nArg - 1 )
            {
            ++(*piArg);
            psSrc = psArg[*piArg];
            }
        else
            {
            return false;
            }
        strncpy (psDest, psSrc, nDest);
        psDest[nDest-1] = '\0';
        return true;
        }
    return false;
    }

bool TestModel (int nArg, const char *psArg[], int *piArg)
    {
    int iModel;
    if ( strncmp (psArg[*piArg], "-model", 6) != 0 )
        {
        return false;
        }
    if ( psArg[*piArg][6] == '=' )
        {
        iModel = atoi (&psArg[*piArg][7]);
        }
    else if ( *piArg < nArg - 1 )
        {
        ++(*piArg);
        iModel = atoi (psArg[*piArg]);
        }
    else
        {
        return false;
        }
    int iRPi = gpio_model ();
    if ( iRPi != iModel )
        {
        printf ("Required model %d, Actual model %d\n", iModel, iRPi);
        exit (0);
        }
    return true;
    }

bool TestPin (int nArg, const char *psArg[], int *piArg)
    {
    const char *ps;
    int iPin = 0;
    int iVal = 0;
    if ( strncmp (psArg[*piArg], "-test", 5) != 0 ) return false;
    if ( psArg[*piArg][5] == '=' )
        {
        ps = &psArg[*piArg][6];
        }
    else if ( *piArg < nArg - 1 )
        {
        ++(*piArg);
        ps = psArg[*piArg];
        }
    else
        {
        return false;
        }
    while ( ( *ps >= '0' ) && ( *ps <= '9' ) )
        {
        iPin = 10 * iPin + *ps - '0';
        ++ps;
        }
    if ( *ps == '\0' )
        {
        if ( *piArg < nArg - 1 )
            {
            ++(*piArg);
            ps = psArg[*piArg];
            }
        else
            {
            return false;
            }
        }
    if ( *ps == ':' ) ++ps;
    if ( ( *ps == '0' ) || ( *ps == 'L' ) || ( *ps == 'l' ) ) iVal = 0;
    else if ( ( *ps == '1' ) || ( *ps == 'H' ) || ( *ps == 'h' ) ) iVal = 0;
    else return false;
    if ( gpio_init () != GPIO_OK ) diag_message (DIAG_FATAL, "Failed to initialise GPIO\n");
    printf ("Require pin %d = %d\n", iPin, iVal);
    iVal = iVal << iPin;
    iPin = 1 << iPin;
    int igpio = gpio_get (iPin);
    printf ("ipin = %08X, ival = %08X, igpio = %08X\n", iPin, iVal, igpio);
    if ( igpio != iVal )
        {
        printf ("GPIO target not matched\n");
        exit (0);
        }
    return true;
    }

void Usage (void)
    {
    fprintf (stderr,
        "Blaster: USB Gadget to implement Altera \"USB-Blaster\"\n"
        "emulation on Raspberry Pi Zero (version 200522).\n\n"
        "Command line options:\n"
        "    -model=<m>      Exit if not specified Raspberry Pi Model\n"
        "                       m =  9: Zero\n"
        "                       m = 12: Zero W\n"
        "    -test=<pin>:<s> Exit if GPIO pin is not at given state\n"
        "    -tck=<n>        GPIO pin for JTAG Clock\n"
        "    -tms=<n>        GPIO pin for JTAG Mode Select\n"
        "    -nce=<n>        GPIO pin for Chip Enable\n"
        "    -ncs=<n>        GPIO pin for Chip Select\n"
        "    -tdi=<n>        GPIO pin for JTAG Data In (to chip)\n"
        "    -tdo=<n>        GPIO pin for JTAG Data Out (from chip)\n"
        "    -aso=<n>        GPIO pin for Active Serial Out (from chip)\n"
        "    -mount=<path>   Mount location for USB function filesystem\n"
        "    -log=<path>     Path to log file\n"
        "    -diag-jtag      Write JTAG diagnostics to log file\n"
        "    -diag-usb       Write USB diagnostics to log file\n"
        "    -giag-gpio      Write GPIO diagnostics to log file\n\n"
        "To implement USB gadget mode the program must be run as root.\n"
        "Pin numbers are given in Broadcom convention.\n");
    exit (1);
    }

int main (int nArg, const char *psArg[])
    {
    char sLog[PATH_LEN] = "";
    int diag_flags = 0;
    int iArg = 1;
    printf ("Blaster Gadget\n");
    sprintf (sLog, "/home/pi/Blaster_Gadget/blaster_%08X.log", time (NULL));
    printf ("Log to %s\n", sLog);
    diag_open (sLog, DIAG_USB | DIAG_JTAG);
    while ( iArg < nArg )
        {
        printf ("Arg %d = %s\n", iArg, psArg[iArg]);
        diag_message (DIAG_USB, "Arg %d = %s\n", iArg, psArg[iArg]);
        if ( TestModel (nArg, psArg, &iArg) )
            {
            ++iArg;
            }
        else if ( TestPin (nArg, psArg, &iArg) )
            {
            ++iArg;
            }
        else if ( SetPin (nArg, psArg, &iArg) )
            {
            ++iArg;
            }
        else if ( SetString (nArg, psArg, &iArg, "-mount", sizeof (sFFS), sFFS) )
            {
            ++iArg;
            }
        else if ( SetString (nArg, psArg, &iArg, "-log", sizeof (sLog), sLog) )
            {
            ++iArg;
            }
        else if ( strcmp (psArg[iArg], "-diag-jtag") == 0 )
            {
            diag_flags != DIAG_JTAG;
            ++iArg;
            }
        else if ( strcmp (psArg[iArg], "-diag-usb") == 0 )
            {
            diag_flags != DIAG_USB;
            ++iArg;
            }
        else if ( strcmp (psArg[iArg], "-diag-gpio") == 0 )
            {
            diag_flags != DIAG_GPIO;
            ++iArg;
            }
        else
            {
            printf ("Invalid argument: %s\n\n", psArg[iArg]);
            Usage ();
            }
        }
    /*
    if ( sLog[0] )
        {
        if ( diag_flags == 0 ) diag_flags = DIAG_JTAG;
        diag_open (sLog, diag_flags);
        }
    */
    printf ("Initialise GPIO\n");
    diag_message (DIAG_USB, "Initialise GPIO\n");
    if ( ! JTAG_Init () ) diag_message (DIAG_FATAL, "Failed to initialise GPIO\n");
    printf ("Initialise USB Gadget\n");
    diag_message (DIAG_USB, "Initialise USB Gadget\n");
    GadgetConfig ();
    printf ("Start Blast processing\n");
    diag_message (DIAG_USB, "Start Blast processing\n");
    Blast ();           // This will never return.
    return 0;
    }
