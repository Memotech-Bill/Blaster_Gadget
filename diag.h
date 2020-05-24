/* diag.h - Diagnostic code */

#ifndef DIAG_H
#define	DIAG_H

#define DIAG_SYSERR -3
#define DIAG_FATAL  -2
#define DIAG_ERROR  -1
#define DIAG_JTAG   0x01
#define DIAG_USB    0x02
#define DIAG_GPIO   0x04

extern void diag_open (const char *psPath, int diags);
extern void diag_message (int flag, const char *fmt, ...);
extern void diag_close (void);

#endif
