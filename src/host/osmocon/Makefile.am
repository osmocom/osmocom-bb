AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6
EXTRA_DIST = \
	     .version \
	     git-version-gen \
	     loader/protocol.h \
	     sercomm.h \
	     $(NULL)

# versioning magic
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)

sbin_PROGRAMS = osmocon osmoload

# FIXME: sercomm needs to move into libosmocore or another shared lib
sercomm.h:
	( echo "/* This file was copied from"; \
	  echo "   ../../target/firmware/include/comm/sercomm.h,"; \
	  echo "   do not modify! */"; \
	  cat ../../target/firmware/include/comm/sercomm.h )> sercomm.h
loader/protocol.h:
	mkdir -p loader
	( echo "/* This file was copied from"; \
	  echo "   ../../target/firmware/apps/loader/protocol.h,"; \
	  echo "   do not modify! */"; \
	  cat ../../target/firmware/apps/loader/protocol.h )> loader/protocol.h
INCLUDES += -I$(srcdir)/../../target/firmware/include/comm -I$(srcdir)/../../target/firmware/apps -DHOST_BUILD
osmocon_SOURCES = osmocon.c tpu_debug.c $(srcdir)/../../target/firmware/comm/sercomm.c
osmocon_LDADD = $(LIBOSMOCORE_LIBS)

osmoload_SOURCE = osmoload.c $(srcdir)/../../target/firmware/comm/sercomm.c
osmoload_LDADD = $(LIBOSMOCORE_LIBS)
