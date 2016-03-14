AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include
AM_CFLAGS = -Wall $(TALLOC_CFLAGS)
AM_LDFLAGS = $(TALLOC_LIBS)

check_PROGRAMS = timer/timer_test sms/sms_test ussd/ussd_test		\
                 smscb/smscb_test bits/bitrev_test a5/a5_test		\
                 conv/conv_test auth/milenage_test lapd/lapd_test	\
                 gsm0808/gsm0808_test gsm0408/gsm0408_test		\
		 gb/bssgp_fc_test gb/gprs_bssgp_test gb/gprs_ns_test	\
		 gprs/gprs_test	kasumi/kasumi_test			\
		 logging/logging_test fr/fr_test			\
		 loggingrb/loggingrb_test strrb/strrb_test              \
		 vty/vty_test comp128/comp128_test utils/utils_test	\
		 smscb/gsm0341_test stats/stats_test			\
		 bitvec/bitvec_test msgb/msgb_test bits/bitcomp_test	\
		 sim/sim_test

if ENABLE_MSGFILE
check_PROGRAMS += msgfile/msgfile_test
endif

utils_utils_test_SOURCES = utils/utils_test.c
utils_utils_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

stats_stats_test_SOURCES = stats/stats_test.c
stats_stats_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

a5_a5_test_SOURCES = a5/a5_test.c
a5_a5_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libgsmint.la

kasumi_kasumi_test_SOURCES = kasumi/kasumi_test.c
kasumi_kasumi_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libgsmint.la

comp128_comp128_test_SOURCES = comp128/comp128_test.c
comp128_comp128_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

auth_milenage_test_SOURCES = auth/milenage_test.c
auth_milenage_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

bits_bitrev_test_SOURCES = bits/bitrev_test.c
bits_bitrev_test_LDADD = $(top_builddir)/src/libosmocore.la

bitvec_bitvec_test_SOURCES = bitvec/bitvec_test.c
bitvec_bitvec_test_LDADD = $(top_builddir)/src/libosmocore.la

bits_bitcomp_test_SOURCES = bits/bitcomp_test.c
bits_bitcomp_test_LDADD = $(top_builddir)/src/libosmocore.la

conv_conv_test_SOURCES = conv/conv_test.c
conv_conv_test_LDADD = $(top_builddir)/src/libosmocore.la

gsm0808_gsm0808_test_SOURCES = gsm0808/gsm0808_test.c
gsm0808_gsm0808_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

gsm0408_gsm0408_test_SOURCES = gsm0408/gsm0408_test.c
gsm0408_gsm0408_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

gprs_gprs_test_SOURCES = gprs/gprs_test.c
gprs_gprs_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

lapd_lapd_test_SOURCES = lapd/lapd_test.c
lapd_lapd_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

msgb_msgb_test_SOURCES = msgb/msgb_test.c
msgb_msgb_test_LDADD = $(top_builddir)/src/libosmocore.la

msgfile_msgfile_test_SOURCES = msgfile/msgfile_test.c
msgfile_msgfile_test_LDADD = $(top_builddir)/src/libosmocore.la

smscb_smscb_test_SOURCES = smscb/smscb_test.c
smscb_smscb_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

smscb_gsm0341_test_SOURCES = smscb/gsm0341_test.c
smscb_gsm0341_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

sms_sms_test_SOURCES = sms/sms_test.c
sms_sms_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

timer_timer_test_SOURCES = timer/timer_test.c
timer_timer_test_LDADD = $(top_builddir)/src/libosmocore.la

ussd_ussd_test_SOURCES = ussd/ussd_test.c
ussd_ussd_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

gb_bssgp_fc_test_SOURCES = gb/bssgp_fc_test.c
gb_bssgp_fc_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gb/libosmogb.la

gb_gprs_bssgp_test_SOURCES = gb/gprs_bssgp_test.c
gb_gprs_bssgp_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gb/libosmogb.la $(LIBRARY_DL)

gb_gprs_ns_test_SOURCES = gb/gprs_ns_test.c
gb_gprs_ns_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gb/libosmogb.la $(LIBRARY_DL)

logging_logging_test_SOURCES = logging/logging_test.c
logging_logging_test_LDADD = $(top_builddir)/src/libosmocore.la

fr_fr_test_SOURCES = fr/fr_test.c
fr_fr_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gb/libosmogb.la $(LIBRARY_DL)

loggingrb_loggingrb_test_SOURCES = loggingrb/loggingrb_test.c
loggingrb_loggingrb_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/vty/libosmovty.la

strrb_strrb_test_SOURCES = strrb/strrb_test.c
strrb_strrb_test_LDADD = $(top_builddir)/src/libosmocore.la

vty_vty_test_SOURCES = vty/vty_test.c
vty_vty_test_LDADD = $(top_builddir)/src/vty/libosmovty.la $(top_builddir)/src/libosmocore.la

sim_sim_test_SOURCES = sim/sim_test.c
sim_sim_test_LDADD = $(top_builddir)/src/sim/libosmosim.la $(top_builddir)/src/libosmocore.la

# The `:;' works around a Bash 3.2 bug when the output is not writeable.
$(srcdir)/package.m4: $(top_srcdir)/configure.ac
	:;{ \
               echo '# Signature of the current package.' && \
               echo 'm4_define([AT_PACKAGE_NAME],' && \
               echo '  [$(PACKAGE_NAME)])' && \
               echo 'm4_define([AT_PACKAGE_TARNAME],' && \
               echo '  [$(PACKAGE_TARNAME)])' && \
               echo 'm4_define([AT_PACKAGE_VERSION],' && \
               echo '  [$(PACKAGE_VERSION)])' && \
               echo 'm4_define([AT_PACKAGE_STRING],' && \
               echo '  [$(PACKAGE_STRING)])' && \
               echo 'm4_define([AT_PACKAGE_BUGREPORT],' && \
               echo '  [$(PACKAGE_BUGREPORT)])'; \
               echo 'm4_define([AT_PACKAGE_URL],' && \
               echo '  [$(PACKAGE_URL)])'; \
             } >'$(srcdir)/package.m4'

EXTRA_DIST = testsuite.at $(srcdir)/package.m4 $(TESTSUITE)		\
             timer/timer_test.ok sms/sms_test.ok ussd/ussd_test.ok	\
             smscb/smscb_test.ok bits/bitrev_test.ok a5/a5_test.ok	\
             conv/conv_test.ok auth/milenage_test.ok			\
             lapd/lapd_test.ok gsm0408/gsm0408_test.ok			\
             gsm0808/gsm0808_test.ok gb/bssgp_fc_tests.err		\
             gb/bssgp_fc_tests.ok gb/bssgp_fc_tests.sh			\
             gb/gprs_bssgp_test.ok gb/gprs_ns_test.ok			\
             gprs/gprs_test.ok kasumi/kasumi_test.ok			\
             msgfile/msgfile_test.ok msgfile/msgconfig.cfg		\
             logging/logging_test.ok logging/logging_test.err		\
             fr/fr_test.ok loggingrb/logging_test.ok			\
             loggingrb/logging_test.err	strrb/strrb_test.ok		\
	     vty/vty_test.ok comp128/comp128_test.ok			\
	     utils/utils_test.ok stats/stats_test.ok			\
	     bitvec/bitvec_test.ok msgb/msgb_test.ok bits/bitcomp_test.ok \
	     sim/sim_test.ok

DISTCLEANFILES = atconfig

TESTSUITE = $(srcdir)/testsuite

check-local: atconfig $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' $(TESTSUITEFLAGS)

installcheck-local: atconfig $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' AUTOTEST_PATH='$(bindir)' \
		$(TESTSUITEFLAGS)

clean-local:
	test ! -f '$(TESTSUITE)' || \
		$(SHELL) '$(TESTSUITE)' --clean

AUTOM4TE = $(SHELL) $(top_srcdir)/missing --run autom4te
AUTOTEST = $(AUTOM4TE) --language=autotest
$(TESTSUITE): $(srcdir)/testsuite.at $(srcdir)/package.m4
	$(AUTOTEST) -I '$(srcdir)' -o $@.tmp $@.at
	mv $@.tmp $@
