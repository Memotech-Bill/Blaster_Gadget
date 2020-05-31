Raspberry Pi Altera "USB Blaster" Gadget - FAILED
=================================================

The aim of this project was to use a Raspberry Pi Zero (or Zero W) as a
replacement for the Altera "USB Blaster" chip programmer.

For a project I am working on I wish to make use of an Altera EPM7032S chip.
These chips require a JTAG programmer to configure. I purchased a cheap
"USB Blaster" clone for the job. However for reasons yet to be determined,
although the Altera Quartus software recognised the clone as a "USB Blaster"
it failed to program the chip. This may have been caused by the programming
cable on the device having one of the connectors reversed when supplied,
resulting in damage to the JTAG drivers.

Not wishing to risk purchasing another dubious clone, I looked for alternatives.
There are existing designes for "USB Blaster" clones on the Net. However these
typically use a programable microcontroller such as a PIC18F14K50.
I don't have a programmer for these either.

It occurred to me that it might be possible to use a Raspberry Pi, with
just conventional programming tools. USB Gadget mode to provide the USB
interface, and GPIO (with suitable buffering and level shifting) to
provide the JTAG programming.

USB Blaster Protocol
--------------------

The following protocol definition was derived by reverse engineering the
software for the PIC18F14K50 based "USB Blaster" clone at
http://sa89a.net/mp.cgi/ele/ub.htm

Receive packets of up to 64 bytes containing command and data bytes as follows:

Command bytes:

1rnnnnnn    = Following nnnnnn data bytes are to be sent. If r is set, also read result.
              If /CS high, write & read JTAG, if /CS low write JTAG, read ASER.
              Also reset TCK to zero.

0r0xxxxx    = Bit bang the following pins:

* Bit RC0: TCK
* Bit RC1: TMS
* Bit RC2: /CE
* Bit RC3: /CS
* Bit RC4: TDI

If the read bit (bit 6) is set, then queue byte for return containing:

* Bit 0: TDO
* Bit 1: ASO

State read before changing output bits.

Data bytes:

JTAG bytes written to TDI low bit first. Clock taken high then low.
If required, results from TDO accumulated low bit first, before clock pulse.
Resulting bytes queued for return to host.

ASER bytes written to TDI low bit first. Clock taken high then low.
If required, results from ADO accumulated low bit first, before clock pulse.
Resulting bytes queued for return to host.

Sends to host 2-64 byte packets at least once every 10ms:

* Byte 0: 0x31
* Byte 1: 0x60
* Byte 2+: Data as above.

Development
===========

In order to work the Raspberry Pi would have to present the same USB interface
to the Quartus software as a real "USB Blaster". Connecting the clone "UB Blaster"
to a Linux machine, and running lsusb gave the following information as to the
interface:

        Device Descriptor:
         bLength                18
         bDescriptorType         1
         bcdUSB               1.10
         bDeviceClass            0 (Defined at Interface level)
         bDeviceSubClass         0 
         bDeviceProtocol         0 
         bMaxPacketSize0         8
         idVendor           0x09fb Altera
         idProduct          0x6001 Blaster
         bcdDevice            4.00
         iManufacturer           1 Altera
         iProduct                2 USB-Blaster
         iSerial                 3 00000000
         bNumConfigurations      1
         Configuration Descriptor:
           bLength                 9
           bDescriptorType         2
           wTotalLength           32
           bNumInterfaces          1
           bConfigurationValue     1
           iConfiguration          0 
           bmAttributes         0x80
             (Bus Powered)
           MaxPower               80mA
           Interface Descriptor:
             bLength                 9
             bDescriptorType         4
             bInterfaceNumber        0
             bAlternateSetting       0
             bNumEndpoints           2
             bInterfaceClass       255 Vendor Specific Class
             bInterfaceSubClass    255 Vendor Specific Subclass
             bInterfaceProtocol    255 Vendor Specific Protocol
             iInterface              0 
             Endpoint Descriptor:
               bLength                 7
               bDescriptorType         5
               bEndpointAddress     0x81  EP 1 IN
               bmAttributes            2
                 Transfer Type            Bulk
                 Synch Type               None
                 Usage Type               Data
               wMaxPacketSize     0x0040  1x 64 bytes
               bInterval               1
             Endpoint Descriptor:
               bLength                 7
               bDescriptorType         5
               bEndpointAddress     0x02  EP 2 OUT
               bmAttributes            2
                 Transfer Type            Bulk
                 Synch Type               None
                 Usage Type               Data
               wMaxPacketSize     0x0040  1x 64 bytes
               bInterval               1

So the objecive was to reproduce that using a Raspberry Pi.

Step 1
------

Having read up on USB gadget mode on the Raspberry Pi, it appeared that I
should be able to do this with code running entirely in user space making
use of the ConfigFS and FunctionFS interfaces, not requiring any kernel
mode programming.

First step was to edit "/boot/config.txt" to add:

      dtoverlay=dwc2

and "/etc/modules" to add:

     dwc2
     libcomposite

Reboot, and try and define a suitable interface using shell commands:

        cd /sys/kernel/config/usb_gadget
        mkdir blaster
        cd blaster
        echo 0x09fb >idVendor
        echo 0x6001 >idProduct
        mkdir -p strings/0x409
        echo Altera >strings/0x409/manufacturer
        echo USB-Blaster >strings/0x409/product
        mkdir -p configs/default.1
        mkdir -p functions/ffs.blaster

The last command failed:

    mkdir: cannot create directory ‘functions/ffs.blaster’: No such file or directory

Trials found that I could define other functions (such as "acm.test") but
not FunctionFS.

I found that the stock Raspbian kernel does not have a working version of the
usb_f_fs.ko module. (The file was present in the copy of the kernel I had downloaded,
but this had clearly been compiled for a different kernel as any attempt to load it
using modprobe or insmod failed).

Step 2
------

Therefore download and compile a new kernel following the instructions on the
Raspberry Pi website (see references below). Loaded the default configuration
for a Raspberry Pi Zero, then used "make menuconfig" to enable support for
FunctionFS, following the following menu selections:

* Device Drivers  --->
* USB support  --->
* USB Gadget Support  --->
* Function Filesystem

Press M to build FunctionFS support as a module.

In order to be able to identify the new kernel I gave it a unique name, from
menuconfig:

* General setup  --->
* Local version

Then ran a full compile on a Raspberry Pi 4. It took around 3-4 hours.

When copying the new kernel into the /boot partition I gave it the unique
name of "kernel-blaster.img".

Edit "/boot/config.txt" and replace the previous change with:

     [pi0]
     dtoverlay=dwc2
     kernel=kernel-blaster.img

Also edit "/etc/modules" and add:

     usb_f_fs

Booting this card on a Raspberry Pi Zero, it was now possible to run all
the required shell commands to begin to define the USB interface:

        cd /sys/kernel/config/usb_gadget
        mkdir blaster
        cd blaster
        echo 0x09fb >idVendor
        echo 0x6001 >idProduct
        mkdir -p strings/0x409
        echo Altera >strings/0x409/manufacturer
        echo USB-Blaster >strings/0x409/product
        mkdir -p configs/default.1
        mkdir -p functions/ffs.blaster
        ln -s functions/ffs.blaster/ configs/default.1/
        mkdir /var/run/blaster
        mount -t functionfs blaster /var/run/blaster

The next step would be to write the configuration of the other endpoints
to "/var/run/blaster/ep0". This can not readily be done using shell commands.

Step 3
------

Write a user mode program "blaster.c", making use of the ConfigFS and FunctionFS
interfaces as above to provide the USB interface, implement the "USB Blaster"
protocol, and use GPIO to implement the JTAG interface.

Compiled it and added a line to "/etc/rc.local" to runit on boot.

Put the SD card into a Raspberry Pi Zero, and with a cable in the USB socket
(not in the power socket), connected it to a Linux laptop. After waiting for
the Zero to finish booting, ran lsusb, which gave:

        Device Descriptor:
          bLength                18
          bDescriptorType         1
          bcdUSB               2.00
          bDeviceClass            0 (Defined at Interface level)
          bDeviceSubClass         0 
          bDeviceProtocol         0 
          bMaxPacketSize0        64
          idVendor           0x09fb Altera
          idProduct          0x6001 Blaster
          bcdDevice            4.00
          iManufacturer           1 Altera
          iProduct                2 USB-Blaster
          iSerial                 3 00000000
          bNumConfigurations      1
          Configuration Descriptor:
            bLength                 9
            bDescriptorType         2
            wTotalLength           32
            bNumInterfaces          1
            bConfigurationValue     1
            iConfiguration          0 
            bmAttributes         0x80
              (Bus Powered)
            MaxPower               80mA
            Interface Descriptor:
              bLength                 9
              bDescriptorType         4
              bInterfaceNumber        0
              bAlternateSetting       0
              bNumEndpoints           2
              bInterfaceClass       255 Vendor Specific Class
              bInterfaceSubClass    255 Vendor Specific Subclass
              bInterfaceProtocol    255 Vendor Specific Protocol
              iInterface              0 
              Endpoint Descriptor:
                bLength                 7
                bDescriptorType         5
                bEndpointAddress     0x81  EP 1 IN
                bmAttributes            2
                  Transfer Type            Bulk
                  Synch Type               None
                  Usage Type               Data
                wMaxPacketSize     0x0040  1x 64 bytes
                bInterval               1
              Endpoint Descriptor:
                bLength                 7
                bDescriptorType         5
                bEndpointAddress     0x01  EP 1 OUT
                bmAttributes            2
                  Transfer Type            Bulk
                  Synch Type               None
                  Usage Type               Data
                wMaxPacketSize     0x0040  1x 64 bytes
                bInterval               1

There are a number of differences between this and the original:

* The address of the second endpoint is 0x01, instead of 0x02.
* bcdUSB is 2.0 instead of 1.10.
* bMaxPacketSize0 is 64 instead of 8.

From the kernel documentation I found:

"... User space driver need to
write descriptors and strings to that file. It does not need
to worry about endpoints, interfaces or strings numbers but
simply provide descriptors such as if the function was the
only one (endpoints and strings numbers starting from one and
interface numbers starting from zero). The FunctionFS changes
them as needed also handling situation when numbers differ in
different configurations."

So clearly, I was not going to be able to achive the required emulation
entirely from user space.

Step 4
------

One option would have been to write an entirely new kernel module
to implement the interface I required. However, I did not have sufficient
understanding of the kernel to do that.

Instead I decided to try and tweak the existing kernel modules so that they
did what I wanted. I could have just inserted the specific setins I required
for my application, but I wanted to make the kernel changes generic so that
I could also use them for other applications, and possibly even submit them
upstream.

* The first change was to add a new flag, to request that the kernel used
the endpoint addresses as written to "/var/run/blaster/ep0", rather than
assigning the lowest possible address. This worked and gave the required
endpoint addresses.

* Investigation found that the libcomposite module was ignoring the value
written to ConfigFS, and was just inserting a value dependent upon the
features implemented by the gadget. Modified this code to use the value
from ConfigFS if greater than that required by the features. However allowed
any value from ConfigFS when there there were no special features (libcomposite
was using 2.0 in that case).

* The interface was now very close to the objective, so tried connecting the
Raspberry Pi Zero to the Windows machine where I run Quartus, rather than
the laptop I had been using for testing. However, the Windows OS failed to
recognise the device. Eventually discovered that the Laptop I had been using
for testing only had USB 1.1 ports, while the Windows machine had USB 2.0.
As a result, the Windows machine was connecting at USB "High" (USB 2.0) speed
rather than USB "Full" speed.

* Therefore added a test on the value of bcdUSB written to ConfigFS, and if it
is less than 2.0, restrict the maximum speed of the Raspberry Pi gadget interface
to "full". This changes hartware registers to prevent the USB interface from
signalling "high" speed capability during initial negotiation.

* With these changes the USB interface was very close to the target, and both
Linux and Windows OS recognised the Raspberry Pi as a valid USB device. However,
Quartus still did not recognise it. The only remaining difference was the endpoint
zero maximum packet size. Again, libcomposite was ignoring the value written to
ConfigFS, and just returning the value the hardware was set to. Modified this
code to return either the hardware value, or the value written to ConfigFS,
whichever was the smaller.

* With this change, the Raspberry Pi Zero gadget would not enumerate when connected
to either Linux or Windows. Clearly the value returned in the configuration had to
match the hardware setting. Eventually worked out how (during driver bind) to set the
hardware configured maximum packet size to that set in ConfigFS.

* However, even with this change, the Raspberry Pi gadget will not enumerate with
an ep0 maximum packet length of 8. Kernel diagnostic messages show that, as best
as I can tell, the hardware is being correctly configured for this packet length.
However the details of the Raspberry Pi Zero USB hardware are not currently
publicly available. It is possible that the remainder of the driver code is somewhere
assuming a longer maximum packet length.

Unless a solution can be found to this outstanding limitation can be found, the project has failed.

For interest, the kernel changes I made are included either as individual files in
the "kernel" folder, or as a single patch file "blaster_kernel.patch". As they stand
they include many diagnostic writes as "pr_crit" routine calls. These can be viewed by
attaching a serial terminal to the Raspberry Pi Zero GPIO pins as it boots. If the
project had been successful, I would have tidied up the code to remove these.

References
==========

The following links have been of assistance in developing the code:

* http://sa89a.net/mp.cgi/ele/ub.htm
* https://translate.google.co.uk/translate?sl=auto&tl=en&u=http%3A%2F%2Fsa89a.net%2Fmp.cgi%2Fele%2Fub.htm
* https://www.isticktoit.net/?p=1383
* https://www.collabora.com/news-and-blog/blog/2019/02/18/modern-usb-gadget-on-linux-and-how-to-integrate-it-with-systemd-part-1/
* https://www.collabora.com/news-and-blog/blog/2019/03/27/modern-usb-gadget-on-linux-and-how-to-integrate-it-with-systemd-part-2/
* https://www.collabora.com/news-and-blog/blog/2019/06/24/using-dummy-hcd/
* https://stackoverflow.com/questions/39375475/raspberry-pi-zero-usb-device-emulation
* https://www.raspberrypi.org/documentation/linux/kernel/building.md
* https://www.raspberrypi.org/documentation/linux/kernel/configuring.md
* https://www.kernel.org/doc/Documentation/usb/gadget_configfs.txt
* https://www.kernel.org/doc/Documentation/usb/functionfs.txt
* https://www.kernel.org/doc/html/latest/usb/functionfs.html
* https://www.kernel.org/doc/html/latest/driver-api/usb/gadget.html
* https://events.static.linuxfound.org/sites/events/files/slides/LinuxConNA-Make-your-own-USB-gadget-Andrzej.Pietrasiewicz.pdf
* https://github.com/libusbgx/libusbgx
* https://github.com/kopasiak/gt
