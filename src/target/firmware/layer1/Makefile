INCLUDES=-I../include/ -I../../../../include
-include ../Makefile.inc

LIBNAME=layer1
OBJS=avg.o agc.o afc.o sync.o gsm.o tdma_sched.o tpu_window.o init.o l23_api.o mframe_sched.o

LST=$(OBJS:.o=.lst)

all:	lib$(LIBNAME).a

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $^

lib$(LIBNAME).a: $(OBJS)
	$(CROSS_COMPILE)$(AR) cru $@ $^

clean:
	rm -f *.a $(OBJS) $(LST)
