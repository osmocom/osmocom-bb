
#### TOOLCHAIN CONFIGURATION ####

CROSS_COMPILE?=arm-elf-

CC=gcc
LD=ld
AR=ar
SIZE=size
OBJCOPY=objcopy

DEBUGF=dwarf-2

CFLAGS=-mcpu=arm7tdmi $(INCLUDES)
CFLAGS += -Wall -Wextra -Wcast-align -Wimplicit -Wunused
CFLAGS += -Wswitch -Wredundant-decls -Wreturn-type -Wshadow -Wnested-externs
CFLAGS += -Wbad-function-cast -Wsign-compare -Waggregate-return
CFLAGS += -Wa,-adhlns=$(subst $(suffix $<),.lst,$<)
CFLAGS += -Os -ffunction-sections
CFLAGS += -g$(DEBUGF)

# Uncomment this line if you want to enable Tx (Transmit) Support.
#CFLAGS += -DCONFIG_TX_ENABLE

# some older toolchains don't support this, ignore it for now
#ASFLAGS=-Wa,-adhlns=$(<:.S=.lst),--g$(DEBUGF) $(INCLUDES) -D__ASSEMBLY__
ASFLAGS=-Wa,-adhlns=$(<:.S=.lst) $(INCLUDES) -D__ASSEMBLY__

LDFLAGS = -nostartfiles -nostdlib -nodefaultlibs --gc-sections #-Wl,-Map=$(TARGET).map,--cref

#### GIT VERSION ####

GIT_COMMIT:=$(shell git describe --always)
GIT_MODIFIED:=$(shell (git status | grep "modified:\|added:\|deleted:" -q) && echo "-modified")

GIT_REVISION:=$(GIT_COMMIT)$(GIT_MODIFIED)

CFLAGS += -DGIT_REVISION=\"$(GIT_REVISION)\"

#### GLOBAL DATA ####

ALL_OBJS=
ALL_DEPS=

ALL_LSTS=$(ALL_OBJS:.o=.lst)

#### APPLICATION DATA ####

ALL_APPS=

ALL_APP_TARGETS=$(ALL_APPS) $(ALL_APPS:.elf=.bin) $(ALL_APPS:.elf=.map) $(ALL_APPS:.elf=.size)

#### LIBRARY DATA ####

ALL_LIBS=

ALL_LIB_TARGETS=$(ALL_LIBS)


#### DEFAULT RULE ####

.PHONY: default
default: all


#### APPLICATION RULES ####

ALL_OBJS+=$(ANY_APP_OBJS)
ALL_DEPS+=$(ANY_APP_OBJS:.o=.p)

# template for application rules
define APPLICATION_BOARD_template

board/$(2)/$(1).elf board/$(2)/$(1).map board/$(2)/$(1).size: apps/$(1)/main.o $(ANY_APP_OBJS) $(ANY_APP_LIBS) $$($(2)_OBJS) board/$(2)/board.o
	$(CROSS_COMPILE)$(LD) $(LDFLAGS) -T $(LDS) -Bstatic -Map board/$(2)/$(1).map  -o board/$(2)/$(1).elf --start-group $$^ --end-group
	$(CROSS_COMPILE)$(SIZE) board/$(2)/$(1).elf | tee board/$(2)/$(1).size

ALL_APPS+=board/$(2)/$(1).elf

endef

define BOARD_template

board/$(1)/board.o: board/board.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -DBOARD=\"$(1)\" -c -o $$@ $$<

ALL_OBJS+=board/$(1)/board.o $$($(1)_OBJS)

endef

define APPLICATION_template

$(1)_SRCS_REL=$$(patsubst %,$$($(1)_DIR)/%,$$($(1)_SRCS))
$(1)_OBJS:=$$($(1)_SRCS_REL:.c=.o)
$(1)_OBJS:=$$($(1)_OBJS:.S=.o)

ALL_OBJS+=$$($(1)_OBJS) apps/$(1)/main.o
ALL_DEPS+=$$($(1)_OBJS:.o=.p) apps/$(1)/main.p

endef

# define rules for all defined applications
$(foreach app,$(APPLICATIONS),$(foreach board,$(BOARDS),$(eval $(call APPLICATION_BOARD_template,$(app),$(board)))))
$(foreach board,$(BOARDS),$(eval $(call BOARD_template,$(board))))
$(foreach app,$(APPLICATIONS),$(eval $(call APPLICATION_template,$(app))))


#### LIBRARY RULES ####

# template for library rules
define LIBRARY_template

$(1)_SRCS_REL=$$(patsubst %,$$($(1)_DIR)/%,$$($(1)_SRCS))
$(1)_OBJS:=$$($(1)_SRCS_REL:.c=.o)
$(1)_OBJS:=$$($(1)_OBJS:.S=.o)

$$($(1)_DIR)/lib$(1).a: $$($(1)_OBJS)
	$(AR) cru $$($(1)_DIR)/lib$(1).a $$($(1)_OBJS)

ALL_LIBS+=$$($(1)_DIR)/lib$(1).a

ALL_OBJS+=$$($(1)_OBJS)
ALL_DEPS+=$$($(1)_OBJS:.o=.p)

endef

# define rules for all defined libraries
$(foreach lbr,$(LIBRARIES),$(eval $(call LIBRARY_template,$(lbr))))


#### TOPLEVEL RULES ####

.PHONY: all
all: $(ALL_DEPS) $(ALL_APP_TARGETS) $(ALL_LIB_TARGETS)

.PHONY: depend
depend: $(ALL_DEPS)


#### COMPILATION RULES ####

%.p: %.c
	@$(CROSS_COMPILE)$(CC) $(CFLAGS) -M -o $(*).d $(<)
	@sed 's|.*\.o:|$(@:.p=.o): |g' < $*.d > $@; rm -f $*.d; [ -s $@ ] || rm -f $@

%.p: %.S
	@$(CROSS_COMPILE)$(CC) $(ASFLAGS) -M -o $(*).d $(<)
	@sed 's|.*\.o:|$(@:.p=.o): |g' < $*.d > $@; rm -f $*.d; [ -s $@ ] || rm -f $@

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CROSS_COMPILE)$(CC) $(ASFLAGS) -c -o $@ $<


%.bin: %.elf
	$(CROSS_COMPILE)objcopy --gap-fill=0xff -O binary $^ $@


#### CLEANUP RULES ####

.PHONY: clean
clean:
	rm -f $(ALL_APP_TARGETS) $(ALL_LIB_TARGETS) $(ALL_OBJS) $(ALL_DEPS) $(ALL_LSTS)

.PHONY: distclean
distclean: clean


#### DEPENDENCY LOAD ####

-include $(ALL_DEPS)
