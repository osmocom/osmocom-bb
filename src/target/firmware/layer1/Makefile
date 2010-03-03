INCLUDES=-I../include/ -I../../../../include
-include ../Makefile.inc

LIBNAME=layer1
CSRCS=avg.c agc.c afc.c sync.c gsm.c tdma_sched.c tpu_window.c init.c l23_api.c \
	mframe_sched.c sched_gsmtime.c async.c
SSRCS=

COBJS=$(CSRCS:.c=.o)
SOBJS=$(SSRCS:.S=.o)
OBJS=$(COBJS) $(SOBJS)

LST=$(OBJS:.o=.lst)

all:	lib$(LIBNAME).a

$(COBJS): %.o : %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $^

$(SOBJS): %.o : %.S
	$(CROSS_COMPILE)$(CC) $(ASFLAGS) -c -o $@ $^

lib$(LIBNAME).a: $(OBJS)
	$(CROSS_COMPILE)$(AR) cru $@ $^

clean:
	rm -f *.a $(OBJS) $(LST)
