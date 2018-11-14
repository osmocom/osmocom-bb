OSMO_GSM_MANUALS_DIR = $(top_srcdir)
EXTRA_DIST = osmocombb-usermanual.adoc \
	osmocombb-usermanual-docinfo.xml \
	chapters

ASCIIDOC = osmocombb-usermanual.adoc
ASCIIDOC_DEPS = $(srcdir)/chapters/*.adoc
include $(OSMO_GSM_MANUALS_DIR)/build/Makefile.asciidoc.inc

include $(OSMO_GSM_MANUALS_DIR)/build/Makefile.common.inc
