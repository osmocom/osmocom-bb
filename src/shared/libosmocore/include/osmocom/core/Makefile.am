osmocore_HEADERS = signal.h linuxlist.h timer.h select.h msgb.h bits.h \
		   bitvec.h statistics.h utils.h socket.h \
		   gsmtap.h write_queue.h prim.h \
		   logging.h rate_ctr.h gsmtap_util.h \
		   crc16.h panic.h process.h linuxrbtree.h \
		   backtrace.h conv.h application.h \
		   crcgen.h crc8gen.h crc16gen.h crc32gen.h crc64gen.h

noinst_HEADERS = timer_compat.h

if ENABLE_PLUGIN
osmocore_HEADERS += plugin.h
endif

if ENABLE_TALLOC
osmocore_HEADERS += talloc.h
endif

if ENABLE_MSGFILE
osmocore_HEADERS += msgfile.h
endif

if ENABLE_SERIAL
osmocore_HEADERS += serial.h
endif

osmocoredir = $(includedir)/osmocom/core

crc%gen.h: crcXXgen.h.tpl
	@echo "  SED    $< -> $@"
	@sed -e's/XX/$*/g' $< > $@
