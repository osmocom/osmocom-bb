AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6

# versioning magic
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

INCLUDES = $(all_includes) -I../layer23/include -DHOST_BUILD
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS)

sbin_PROGRAMS = gsmmap 

gsmmap_SOURCES = gsmmap.c geo.c locate.c log.c ../layer23/src/common/sysinfo.c ../layer23/src/common/networks.c ../layer23/src/common/logging.c
gsmmap_LDADD = $(LIBOSMOGSM_LIBS) $(LIBOSMOCORE_LIBS) -lm

