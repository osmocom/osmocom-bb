OSMO_GSM_MANUALS_DIR := $(shell ./osmo-gsm-manuals-dir.sh)
srcdir=$(CURDIR)

ASCIIDOC = osmocombb-usermanual.adoc
ASCIIDOC_DEPS = $(srcdir)/chapters/*.adoc
include $(OSMO_GSM_MANUALS_DIR)/build/Makefile.asciidoc.inc

OSMO_REPOSITORY = osmocom-bb
include $(OSMO_GSM_MANUALS_DIR)/build/Makefile.common.inc
