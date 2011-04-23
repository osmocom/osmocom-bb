SUBDIRS=. vty codec gsm

# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=0:0:0

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -fPIC -Wall

lib_LTLIBRARIES = libosmocore.la

libosmocore_la_SOURCES = timer.c select.c signal.c msgb.c bits.c \
			 bitvec.c statistics.c \
			 write_queue.c utils.c \
			 logging.c logging_syslog.c rate_ctr.c \
			 gsmtap_util.c crc16.c panic.c backtrace.c \
			 process.c conv.c

if ENABLE_PLUGIN
libosmocore_la_SOURCES += plugin.c
libosmocore_la_LDFLAGS = -ldl
endif

if ENABLE_TALLOC
libosmocore_la_SOURCES += talloc.c
endif

if ENABLE_MSGFILE
libosmocore_la_SOURCES += msgfile.c
endif
