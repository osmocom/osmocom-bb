nobase_include_HEADERS = \
                       osmocom/codec/codec.h \
                       osmocom/core/application.h \
                       osmocom/core/backtrace.h \
                       osmocom/core/bits.h \
                       osmocom/core/bitvec.h \
                       osmocom/core/conv.h \
                       osmocom/core/crc16.h \
                       osmocom/core/crc16gen.h \
                       osmocom/core/crc32gen.h \
                       osmocom/core/crc64gen.h \
                       osmocom/core/crc8gen.h \
                       osmocom/core/crcgen.h \
                       osmocom/core/gsmtap.h \
                       osmocom/core/gsmtap_util.h \
                       osmocom/core/linuxlist.h \
                       osmocom/core/linuxrbtree.h \
                       osmocom/core/logging.h \
                       osmocom/core/msgb.h \
                       osmocom/core/panic.h \
                       osmocom/core/prim.h \
                       osmocom/core/process.h \
                       osmocom/core/rate_ctr.h \
                       osmocom/core/select.h \
                       osmocom/core/signal.h \
                       osmocom/core/socket.h \
                       osmocom/core/statistics.h \
                       osmocom/core/timer.h \
                       osmocom/core/utils.h \
                       osmocom/core/write_queue.h \
                       osmocom/crypt/auth.h \
                       osmocom/crypt/gprs_cipher.h \
                       osmocom/gprs/gprs_bssgp.h \
                       osmocom/gprs/gprs_bssgp_bss.h \
                       osmocom/gprs/gprs_msgb.h \
                       osmocom/gprs/gprs_ns.h \
                       osmocom/gprs/gprs_ns_frgre.h \
                       osmocom/gprs/protocol/gsm_08_16.h \
                       osmocom/gprs/protocol/gsm_08_18.h \
                       osmocom/gsm/a5.h \
                       osmocom/gsm/abis_nm.h \
                       osmocom/gsm/comp128.h \
                       osmocom/gsm/gan.h \
                       osmocom/gsm/gsm0411_smc.h \
                       osmocom/gsm/gsm0411_smr.h \
                       osmocom/gsm/gsm0411_utils.h \
                       osmocom/gsm/gsm0480.h \
                       osmocom/gsm/gsm0502.h \
                       osmocom/gsm/gsm0808.h \
                       osmocom/gsm/gsm48.h \
                       osmocom/gsm/gsm48_ie.h \
                       osmocom/gsm/gsm_utils.h \
                       osmocom/gsm/lapd_core.h \
                       osmocom/gsm/lapdm.h \
                       osmocom/gsm/mncc.h \
                       osmocom/gsm/prim.h \
                       osmocom/gsm/protocol/gsm_03_41.h \
                       osmocom/gsm/protocol/gsm_04_08.h \
                       osmocom/gsm/protocol/gsm_04_11.h \
                       osmocom/gsm/protocol/gsm_04_12.h \
                       osmocom/gsm/protocol/gsm_04_80.h \
                       osmocom/gsm/protocol/gsm_08_08.h \
                       osmocom/gsm/protocol/gsm_08_58.h \
                       osmocom/gsm/protocol/gsm_12_21.h \
                       osmocom/gsm/protocol/gsm_44_318.h \
                       osmocom/gsm/protocol/ipaccess.h \
                       osmocom/gsm/rsl.h \
                       osmocom/gsm/rxlev_stat.h \
                       osmocom/gsm/sysinfo.h \
                       osmocom/gsm/tlv.h

if ENABLE_PLUGIN
nobase_include_HEADERS += osmocom/core/plugin.h
endif

if ENABLE_TALLOC
nobase_include_HEADERS += osmocom/core/talloc.h
endif

if ENABLE_MSGFILE
nobase_include_HEADERS += osmocom/core/msgfile.h
endif

if ENABLE_SERIAL
nobase_include_HEADERS += osmocom/core/serial.h
endif


if ENABLE_VTY
nobase_include_HEADERS += \
                          osmocom/vty/buffer.h \
                          osmocom/vty/command.h \
                          osmocom/vty/logging.h \
                          osmocom/vty/misc.h \
                          osmocom/vty/telnet_interface.h \
                          osmocom/vty/vector.h \
                          osmocom/vty/vty.h
endif

noinst_HEADERS = osmocom/core/timer_compat.h

osmocom/core/crc%gen.h: osmocom/core/crcXXgen.h.tpl
	$(AM_V_GEN)$(MKDIR_P) $(dir $@)
	$(AM_V_GEN)sed -e's/XX/$*/g' $< > $@
