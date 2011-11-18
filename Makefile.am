ACLOCAL_AMFLAGS = -I m4

INCLUDES = $(all_includes) -I$(top_srcdir)/include
SUBDIRS = include src src/vty src/codec src/gsm src/gb tests utils

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libosmocore.pc libosmocodec.pc libosmovty.pc libosmogsm.pc \
		 libosmogb.pc

BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

EXTRA_DIST = git-version-gen

if HAVE_DOXYGEN

html_DATA = $(top_builddir)/doc/html.tar

$(html_DATA): $(top_builddir)/doc/core/html/index.html \
		  $(top_builddir)/doc/gsm/html/index.html \
		  $(top_builddir)/doc/vty/html/index.html \
		  $(top_builddir)/doc/codec/html/index.html
	cd $(top_builddir)/doc && tar cf html.tar */html

$(top_builddir)/doc/core/html/index.html: $(SOURCES) Doxyfile.core
	@rm -rf doc/core
	mkdir -p doc/core
	$(DOXYGEN) Doxyfile.core

$(top_builddir)/doc/gsm/html/index.html: $(SOURCES) Doxyfile.gsm
	@rm -rf doc/gsm
	mkdir -p doc/gsm
	$(DOXYGEN) Doxyfile.gsm

$(top_builddir)/doc/vty/html/index.html: $(SOURCES) Doxyfile.vty
	@rm -rf doc/vty/{html,latex}
	$(DOXYGEN) Doxyfile.vty

$(top_builddir)/doc/codec/html/index.html: $(SOURCES) Doxyfile.codec
	@rm -rf doc/codec
	mkdir -p doc/codec
	$(DOXYGEN) Doxyfile.codec

install-data-hook:
	cd $(DESTDIR)$(htmldir) && tar xf html.tar && rm -f html.tar

uninstall-hook:
	cd $(DESTDIR)$(htmldir) && rm -rf {core,gsm,vty,codec}

DX_CLEAN = doc/{core,gsm,vty,codec}/{html,latex}/* doc/html.tar
endif

MOSTLYCLEANFILES = $(DX_CLEAN)
