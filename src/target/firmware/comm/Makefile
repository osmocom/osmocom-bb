INCLUDES=-I../include/
-include ../Makefile.inc

LIBNAME=comm
CSRCS=msgb.c sercomm.c sercomm_cons.c
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
