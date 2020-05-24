# Compile for RPi Zero
CFLAGS =	-g -Werror -march=armv6k -mtune=arm1176jzf-s
LFLAGS =	-g
LIBS =

blaster:	blaster.o diag.o gpio.o
		$(CC) -o $@ $(LFLAGS) $+ $(LIBS)

blaster.o:	blaster.c diag.h gpio.h
		$(CC) -c -o $@ $(CFLAGS) $<

gpio.o:		gpio.c gpio.h
		$(CC) -c -o $@ $(CFLAGS) $<

diag.o:		diag.c diag.h
		$(CC) -c -o $@ $(CFLAGS) $<
