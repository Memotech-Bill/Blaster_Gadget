Raspberry Pi Altera "USB Blaster" Gadget
========================================

The aim of this project is to use a Raspberry Pi Zero (or Zero W) as a
replacement for the Altera "USB Blaster" chip programmer.

** This is a work in progress and is not yet working **

For a project I am working on I wish to make use of an Altera EPM7032S chip.
These chips require a JTAG programmer to configure. I purchased a cheap
"USB Blaster" clone for the job. However for reasons yet to be determined
it does not work.

Not wishing to risk purchasing another dubious clone, I looked for alternatives.
There are existing designes for "USB Blaster" clones on the Net. However these
typically use a programable microcontroller such as a PIC18F14K50.
I don't have a programmer for these either.

It occurred to me that it should be possible to use a Raspberry Pi, with
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
      Byte 0: 0x31
      Byte 1: 0x60
      Byte 2+: Data as above.

Status
------

* Code written
* Compiled new kernel with FunctionFS enabled.
* USB Gadget is showing USB 2.00 rather than USB 1.10
* The input endpoint has endpoint number 1 (duplicating the output endpoint)
rather than endpoint number 2.

References
----------

The following links have been of assistance in developing the code:

* http://sa89a.net/mp.cgi/ele/ub.htm
* https://translate.google.co.uk/translate?sl=auto&tl=en&u=http%3A%2F%2Fsa89a.net%2Fmp.cgi%2Fele%2Fub.htm
* https://www.isticktoit.net/?p=1383
* https://www.collabora.com/news-and-blog/blog/2019/02/18/modern-usb-gadget-on-linux-and-how-to-integrate-it-with-systemd-part-1/
* https://www.collabora.com/news-and-blog/blog/2019/03/27/modern-usb-gadget-on-linux-and-how-to-integrate-it-with-systemd-part-2/
* https://www.collabora.com/news-and-blog/blog/2019/06/24/using-dummy-hcd/
* https://stackoverflow.com/questions/39375475/raspberry-pi-zero-usb-device-emulation
* https://www.kernel.org/doc/Documentation/usb/gadget_configfs.txt
* https://www.kernel.org/doc/Documentation/usb/functionfs.txt
* https://www.kernel.org/doc/html/latest/usb/functionfs.html
* https://www.kernel.org/doc/html/latest/driver-api/usb/gadget.html
* https://events.static.linuxfound.org/sites/events/files/slides/LinuxConNA-Make-your-own-USB-gadget-Andrzej.Pietrasiewicz.pdf
* https://github.com/libusbgx/libusbgx
* https://github.com/kopasiak/gt
