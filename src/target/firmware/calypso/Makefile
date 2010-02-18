INCLUDES=-I../include/
-include ../Makefile.inc

OBJS=arm.o clock.o dma.o dsp.o du.o i2c.o irq.o rtc.o spi.o tpu.o tsp.o keypad.o misc.o timer.o backlight.o uart.o

LST=$(OBJS:.o=.lst)

all:	libcalypso.a

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $^

libcalypso.a: $(OBJS)
	$(CROSS_COMPILE)$(AR) cru $@ $^

clean:
	rm -f *.a $(OBJS) $(LST)
