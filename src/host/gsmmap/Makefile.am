AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6

# versioning magic
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)

sbin_PROGRAMS = gsmmap 

INCLUDES += -I../layer23/include -DHOST_BUILD
gsmmap_SOURCES = gsmmap.c geo.c locate.c log.c ../layer23/src/common/sysinfo.c ../layer23/src/common/networks.c
gsmmap_LDADD = $(LIBOSMOCORE_LIBS) -lm

