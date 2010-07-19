
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
CFLAGS += -Os -ffunction-sections
CFLAGS += -g$(DEBUGF)

# some older toolchains don't support this, ignore it for now
#ASFLAGS=--g$(DEBUGF) $(INCLUDES) -D__ASSEMBLY__
ASFLAGS=$(INCLUDES) -D__ASSEMBLY__

LDFLAGS = -nostartfiles -nostdlib -nodefaultlibs --gc-sections --cref

#### GIT VERSION ####

GIT_COMMIT:=$(shell git describe --always)
GIT_MODIFIED:=$(shell (git status | grep "modified:\|added:\|deleted:" -q) && echo "-modified")

GIT_REVISION:=$(GIT_COMMIT)$(GIT_MODIFIED)

ASFLAGS += -DGIT_REVISION=\"$(GIT_REVISION)\"
CFLAGS += -DGIT_REVISION=\"$(GIT_REVISION)\"

#### GLOBAL DATA ####

ALL_OBJS=

ALL_LSTS=$(ALL_OBJS:.o=.lst)
ALL_DEPS=$(ALL_OBJS:.o=.p)

#### APPLICATION DATA ####

ALL_APPS=

ALL_APP_TARGETS=$(ALL_APPS:.elf=.bin) $(ALL_APPS:.elf=.size) $(ALL_APPS) $(ALL_APPS:.elf=.map)

#### LIBRARY DATA ####

ALL_LIBS=

ALL_LIB_TARGETS=$(ALL_LIBS)


#### DEFAULT RULE ####

.PHONY: default
default: all


#### APPLICATION RULES ####

# template for application rules
define APPLICATION_BOARD_ENVIRONMENT_template

# define set of objects for this binary
$(1)_$(2)_$(3)_OBJS := apps/$(1)/main.o $(ANY_APP_OBJS) $$($(2)_OBJS)
$(1)_$(2)_$(3)_LIBS := $(ANY_APP_LIBS)

# define manifest compilation
board/$(2)/$(1).$(3).manifest.o: board/manifest.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -DAPPLICATION=\"$(1)\" -DBOARD=\"$(2)\" -DENVIRONMENT=\"$(3)\" -c -o $$@ $$<

# generate dummy dependencies for manifest
board/$(2)/$(1).$(3).manifest.p: board/manifest.c
	@touch board/$(2)/$(1).$(3).manifest.p

# add manifest object to object list
$(1)_$(2)_$(3)_OBJS+=board/$(2)/$(1).$(3).manifest.o $$($(3)_OBJS)

# define compilation rule, also generates map file
board/$(2)/$(1).$(3).elf board/$(2)/$(1).$(3).map: $$($(1)_$(2)_$(3)_OBJS) $$($(1)_$(2)_$(3)_LIBS) $$($(3)_LDS)
	$(CROSS_COMPILE)$(LD) $(LDFLAGS) -T $$($(3)_LDS) -Bstatic \
		-Map board/$(2)/$(1).$(3).map -o board/$(2)/$(1).$(3).elf \
		--start-group $$($(1)_$(2)_$(3)_OBJS) $$($(1)_$(2)_$(3)_LIBS) --end-group

# define size rule
board/$(2)/$(1).$(3).size: board/$(2)/$(1).$(3).elf
	$(CROSS_COMPILE)$(SIZE) board/$(2)/$(1).$(3).elf | tee board/$(2)/$(1).$(3).size

ALL_APPS+=board/$(2)/$(1).$(3).elf
ALL_OBJS+=board/$(2)/$(1).$(3).manifest.o

endef

define BOARD_template
ALL_OBJS+=$$($(1)_OBJS)
endef

define BOARD_ENVIRONMENT_template
ALL_OBJS+=$$($(1)_OBJS)
endef

define APPLICATION_template
$(1)_SRCS_REL=$$(patsubst %,$$($(1)_DIR)/%,$$($(1)_SRCS))
$(1)_OBJS:=$$($(1)_SRCS_REL:.c=.o)
$(1)_OBJS:=$$($(1)_OBJS:.S=.o)

ALL_OBJS+=$$($(1)_OBJS) apps/$(1)/main.o
endef

# define rules for all defined applications
$(foreach app,$(APPLICATIONS),                \
	$(foreach brd,$(BOARDS),                  \
		$(foreach env,$($(brd)_ENVIRONMENTS), \
			$(eval $(call APPLICATION_BOARD_ENVIRONMENT_template,$(app),$(brd),$(env))))))

$(foreach brd,$(BOARDS), \
	$(eval $(call BOARD_template,$(brd)) \
    $(foreach env,$($(brd)_ENVIRONMENTS), \
	    $(eval $(call BOARD_ENVIRONMENT_template,$(env))))))

$(foreach app,$(APPLICATIONS), \
	$(eval $(call APPLICATION_template,$(app))))


# add common things to global lists
ALL_OBJS+=$(ANY_APP_OBJS)

#### LIBRARY RULES ####

# template for library rules
define LIBRARY_template

$(1)_SRCS_REL=$$(patsubst %,$$($(1)_DIR)/%,$$($(1)_SRCS))
$(1)_OBJS:=$$($(1)_SRCS_REL:.c=.o)
$(1)_OBJS:=$$($(1)_OBJS:.S=.o)

$$($(1)_DIR)/lib$(1).a: $$($(1)_OBJS)
	$(CROSS_COMPILE)$(AR) cru $$($(1)_DIR)/lib$(1).a $$($(1)_OBJS)

ALL_LIBS+=$$($(1)_DIR)/lib$(1).a

ALL_OBJS+=$$($(1)_OBJS)

endef

# define rules for all defined libraries
$(foreach lbr,$(LIBRARIES),$(eval $(call LIBRARY_template,$(lbr))))


#### TOPLEVEL RULES ####

.PHONY: all
all: $(ALL_DEPS) $(ALL_APPS:.elf=.bin) $(ALL_APPS:.elf=.size)

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
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -Wa,-adhlns=$(@:.o=.lst) -c -o $@ $<

%.o: %.S
	$(CROSS_COMPILE)$(CC) $(ASFLAGS) -Wa,-adhlns=$(@:.o=.lst) -c -o $@ $<


%.bin: %.elf
	$(CROSS_COMPILE)objcopy --gap-fill=0xff -O binary $^ $@


#### CLEANUP RULES ####

.PHONY: clean
clean:
	rm -f $(ALL_APP_TARGETS) $(ALL_LIB_TARGETS) $(ALL_OBJS) $(ALL_DEPS) $(ALL_LSTS)

.PHONY: distclean
distclean: clean
	find . -name '*.o' -or -name '*.bin' -or -name '*.map' -or -name '*.lst' -or -name '*.p' -exec rm '{}' ';'


#### DEPENDENCY LOAD ####

-include $(ALL_DEPS)
