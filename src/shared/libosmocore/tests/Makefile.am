INCLUDES = $(all_includes) -I$(top_srcdir)/include

check_PROGRAMS = timer/timer_test sms/sms_test ussd/ussd_test		\
                 smscb/smscb_test bits/bitrev_test a5/a5_test		\
                 conv/conv_test auth/milenage_test lapd/lapd_test	\
                 gsm0808/gsm0808_test gsm0408/gsm0408_test		\
		 gb/bssgp_fc_test logging/logging_test
if ENABLE_MSGFILE
check_PROGRAMS += msgfile/msgfile_test
endif

a5_a5_test_SOURCES = a5/a5_test.c
a5_a5_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

auth_milenage_test_SOURCES = auth/milenage_test.c
auth_milenage_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

bits_bitrev_test_SOURCES = bits/bitrev_test.c
bits_bitrev_test_LDADD = $(top_builddir)/src/libosmocore.la

conv_conv_test_SOURCES = conv/conv_test.c
conv_conv_test_LDADD = $(top_builddir)/src/libosmocore.la

gsm0808_gsm0808_test_SOURCES = gsm0808/gsm0808_test.c
gsm0808_gsm0808_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

gsm0408_gsm0408_test_SOURCES = gsm0408/gsm0408_test.c
gsm0408_gsm0408_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

lapd_lapd_test_SOURCES = lapd/lapd_test.c
lapd_lapd_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

msgfile_msgfile_test_SOURCES = msgfile/msgfile_test.c
msgfile_msgfile_test_LDADD = $(top_builddir)/src/libosmocore.la

smscb_smscb_test_SOURCES = smscb/smscb_test.c
smscb_smscb_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

sms_sms_test_SOURCES = sms/sms_test.c
sms_sms_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

timer_timer_test_SOURCES = timer/timer_test.c
timer_timer_test_LDADD = $(top_builddir)/src/libosmocore.la

ussd_ussd_test_SOURCES = ussd/ussd_test.c
ussd_ussd_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

gb_bssgp_fc_test_SOURCES = gb/bssgp_fc_test.c
gb_bssgp_fc_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gb/libosmogb.la

logging_logging_test_SOURCES = logging/logging_test.c
logging_logging_test_LDADD = $(top_builddir)/src/libosmocore.la

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
             msgfile/msgfile_test.ok msgfile/msgconfig.cfg		\
             logging/logging_test.ok logging/logging_test.err

TESTSUITE = $(srcdir)/testsuite

check-local: atconfig $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' $(TESTSUITEFLAGS)

installcheck-local: atconfig $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' AUTOTEST_PATH='$(bindir)' \
		$(TESTSUITEFLAGS)

clean-local:
	test ! -f '$(TESTSUITE)' || \
		$(SHELL) '$(TESTSUITE)' --clean
	$(RM) -f atconfig

AUTOM4TE = $(SHELL) $(top_srcdir)/missing --run autom4te
AUTOTEST = $(AUTOM4TE) --language=autotest
$(TESTSUITE): $(srcdir)/testsuite.at $(srcdir)/package.m4
	$(AUTOTEST) -I '$(srcdir)' -o $@.tmp $@.at
	mv $@.tmp $@
