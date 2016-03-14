nobase_include_HEADERS = \
                       osmocom/codec/codec.h \
                       osmocom/core/application.h \
                       osmocom/core/backtrace.h \
                       osmocom/core/bit16gen.h \
                       osmocom/core/bit32gen.h \
                       osmocom/core/bit64gen.h \
                       osmocom/core/bits.h \
                       osmocom/core/bitvec.h \
                       osmocom/core/bitcomp.h \
                       osmocom/core/conv.h \
                       osmocom/core/crc16.h \
                       osmocom/core/crc16gen.h \
                       osmocom/core/crc32gen.h \
                       osmocom/core/crc64gen.h \
                       osmocom/core/crc8gen.h \
                       osmocom/core/crcgen.h \
                       osmocom/core/endian.h \
                       osmocom/core/defs.h \
                       osmocom/core/gsmtap.h \
                       osmocom/core/gsmtap_util.h \
                       osmocom/core/linuxlist.h \
                       osmocom/core/linuxrbtree.h \
                       osmocom/core/logging.h \
                       osmocom/core/loggingrb.h \
                       osmocom/core/stats.h \
                       osmocom/core/macaddr.h \
                       osmocom/core/msgb.h \
                       osmocom/core/panic.h \
                       osmocom/core/prim.h \
                       osmocom/core/process.h \
                       osmocom/core/rate_ctr.h \
                       osmocom/core/stat_item.h \
                       osmocom/core/select.h \
                       osmocom/core/signal.h \
                       osmocom/core/socket.h \
                       osmocom/core/statistics.h \
                       osmocom/core/strrb.h \
                       osmocom/core/talloc.h \
                       osmocom/core/timer.h \
                       osmocom/core/utils.h \
                       osmocom/core/write_queue.h \
                       osmocom/crypt/auth.h \
                       osmocom/crypt/gprs_cipher.h \
		       osmocom/ctrl/control_cmd.h \
		       osmocom/ctrl/control_if.h \
		       osmocom/ctrl/ports.h \
                       osmocom/gprs/gprs_bssgp.h \
                       osmocom/gprs/gprs_bssgp_bss.h \
                       osmocom/gprs/gprs_msgb.h \
                       osmocom/gprs/gprs_ns.h \
                       osmocom/gprs/gprs_ns_frgre.h \
                       osmocom/gprs/protocol/gsm_08_16.h \
                       osmocom/gprs/protocol/gsm_08_18.h \
                       osmocom/gsm/a5.h \
                       osmocom/gsm/abis_nm.h \
                       osmocom/gsm/apn.h \
                       osmocom/gsm/comp128.h \
                       osmocom/gsm/comp128v23.h \
                       osmocom/gsm/gan.h \
                       osmocom/gsm/gsm0341.h \
                       osmocom/gsm/gsm0411_smc.h \
                       osmocom/gsm/gsm0411_smr.h \
                       osmocom/gsm/gsm0411_utils.h \
                       osmocom/gsm/gsm0480.h \
                       osmocom/gsm/gsm0502.h \
                       osmocom/gsm/gsm0808.h \
                       osmocom/gsm/gsm48.h \
                       osmocom/gsm/gsm48_ie.h \
                       osmocom/gsm/gsm_utils.h \
                       osmocom/gsm/ipa.h \
                       osmocom/gsm/lapd_core.h \
                       osmocom/gsm/lapdm.h \
                       osmocom/gsm/meas_rep.h \
                       osmocom/gsm/mncc.h \
                       osmocom/gsm/prim.h \
                       osmocom/gsm/l1sap.h \
                       osmocom/gsm/protocol/gsm_03_40.h \
                       osmocom/gsm/protocol/gsm_03_41.h \
                       osmocom/gsm/protocol/gsm_04_08.h \
                       osmocom/gsm/protocol/gsm_04_11.h \
                       osmocom/gsm/protocol/gsm_04_12.h \
                       osmocom/gsm/protocol/gsm_04_80.h \
                       osmocom/gsm/protocol/gsm_08_08.h \
                       osmocom/gsm/protocol/gsm_08_58.h \
                       osmocom/gsm/protocol/gsm_09_02.h \
                       osmocom/gsm/protocol/gsm_12_21.h \
                       osmocom/gsm/protocol/gsm_44_318.h \
                       osmocom/gsm/protocol/ipaccess.h \
                       osmocom/gsm/protocol/smpp34_osmocom.h \
                       osmocom/gsm/rsl.h \
                       osmocom/gsm/rxlev_stat.h \
                       osmocom/gsm/sysinfo.h \
                       osmocom/gsm/tlv.h \
		       osmocom/sim/class_tables.h \
		       osmocom/sim/sim.h

if ENABLE_PLUGIN
nobase_include_HEADERS += osmocom/core/plugin.h
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
                          osmocom/vty/stats.h \
                          osmocom/vty/misc.h \
                          osmocom/vty/telnet_interface.h \
                          osmocom/vty/vector.h \
                          osmocom/vty/vty.h \
                          osmocom/vty/ports.h \
                          osmocom/ctrl/control_vty.h
endif

noinst_HEADERS = \
	osmocom/core/timer_compat.h \
	osmocom/gsm/kasumi.h

osmocom/core/bit%gen.h: osmocom/core/bitXXgen.h.tpl
	$(AM_V_GEN)$(MKDIR_P) $(dir $@)
	$(AM_V_GEN)sed -e's/XX/$*/g' $< > $@

osmocom/core/crc%gen.h: osmocom/core/crcXXgen.h.tpl
	$(AM_V_GEN)$(MKDIR_P) $(dir $@)
	$(AM_V_GEN)sed -e's/XX/$*/g' $< > $@
