all: osmocon

OSMOCOM_SRC=../libosmocom/src/select.c ../libosmocom/src/timer.c \
	    ../libosmocom/src/msgb.c ../libosmocom/src/talloc.c  \
	    ../libosmocom/src/debug.c
SERCOMM_SRC=../../target/firmware/comm/sercomm.c
INCLUDES=-I../libosmocom/include -I../../target/firmware/include/comm

.PHONY: version.h
version.h:
	@echo -n \#define VERSION \"git- > version.h
	@git log --oneline -n1 osmocon.c|cut -d ' ' -f 1 |tr -d '\n' >> version.h
	@echo \" >> version.h

osmocon: version.h $(OSMOCOM_SRC) $(SERCOMM_SRC)
	$(CC) $(CFLAGS) -DHOST_BUILD $(INCLUDES) -o $@ $(OSMOCOM_SRC) $(SERCOMM_SRC) osmocon.c

clean:
	@rm -f osmocon version.h
