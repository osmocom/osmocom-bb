AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6

SUBDIRS = \
	include \
	src \
	$(NULL)

BUILT_SOURCES = \
	$(top_srcdir)/.version \
	$(NULL)
EXTRA_DIST = \
	.version \
	$(NULL)

# versioning magic
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version
