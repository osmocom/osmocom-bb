# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=4:0:0

INCLUDES = $(all_includes) -I$(top_srcdir)/include -I$(top_builddir)/include
AM_CFLAGS = -Wall

lib_LTLIBRARIES = libosmocore.la

libosmocore_la_SOURCES = timer.c select.c signal.c msgb.c bits.c \
			 bitvec.c statistics.c \
			 write_queue.c utils.c socket.c \
			 logging.c logging_syslog.c rate_ctr.c \
			 gsmtap_util.c crc16.c panic.c backtrace.c \
			 conv.c application.c rbtree.c \
			 crc8gen.c crc16gen.c crc32gen.c crc64gen.c

BUILT_SOURCES = crc8gen.c crc16gen.c crc32gen.c crc64gen.c

if ENABLE_PLUGIN
libosmocore_la_SOURCES += plugin.c
libosmocore_la_LDFLAGS = -version-info $(LIBVERSION) $(LIBRARY_DL)
else
libosmocore_la_LDFLAGS = -version-info $(LIBVERSION)
endif

if ENABLE_TALLOC
libosmocore_la_SOURCES += talloc.c
else
libosmocore_la_LIBADD = -ltalloc
endif

if ENABLE_MSGFILE
libosmocore_la_SOURCES += msgfile.c
endif

if ENABLE_SERIAL
libosmocore_la_SOURCES += serial.c
endif

crc%gen.c: crcXXgen.c.tpl
	$(AM_V_GEN)sed -e's/XX/$*/g' $< > $@
