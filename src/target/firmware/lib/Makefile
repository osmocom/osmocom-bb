INCLUDES=-I../include/
-include ../Makefile.inc

LIBNAME=mini
CSRCS=vsprintf.c string.c ctype.c printf.c console.c
SSRCS=changebit.S clearbit.S div64.S lib1funcs.S memcpy.S memset.S setbit.S testchangebit.S testclearbit.S testsetbit.S

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
