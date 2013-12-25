#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/core/talloc.h>
#include <l1ctl_proto.h>

#define LIBOSMOSIM_DEBUG 0

#define libosmosim_dbg(fmt, ...) \
            do { if (LIBOSMOSIM_DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#define _GNU_SOURCE
#include <unistd.h>

#include "libosmosim.h"

// layer 2 connection variables
static char *layer2_socket_path = "/tmp/osmocom_l2";
static void *l23_ctx = NULL;
static struct osmocom_ms *ms = NULL;

// locking variables
struct log_target *stderr_target;
static char lckfile[] = "/tmp/.osmosim_lock";
static FILE *flckfile;

// fake stuff that is needed in l1ctl.c and is never used in our code
struct gsmtap_inst *gsmtap_inst = NULL;
const uint16_t gsm610_bitorder[] = {};

// sim present in phone identifier
static int sim_present = 0;

// global APDU buffer
static uint8_t apdu_resp[255];
static size_t apdu_length = 0;

/*
 * helper locking functions to ensure only 1 operations is performed by a Osmocom BB phone at the same time 
 */

static int in_shutdown = 0;
int i = 0;
void unlock() {
	int j = i++;
	libosmosim_dbg("unlock() called, tring to unlock %d\n", j);
	funlockfile(flckfile);
	libosmosim_dbg("unlocked %d, unlock() exiting\n", j);
}

void dolock() {
        int j = i++;
        libosmosim_dbg("dolock() called, tring to lock %d\n", j);
        flockfile(flckfile);
        libosmosim_dbg("locked %d, dolock() exiting\n", j);
}

void lock() {
	libosmosim_dbg("%s\n", "lock() called");
	if (in_shutdown) {
		libosmosim_dbg("%s\n", "lock() waiting as in_shutdown is 1");
		sleep(1337); // just wait forever as we're shutdowning already, no action is allowed.
	}
	dolock();
	libosmosim_dbg("%s\n", "lock() exiting");
}

/*
 * callback functions
 */

/* this function is called-back when L1CTL_SIM_ATR message is received (when SIM needs to return ATR) */
int osmosim_sim_up_resp(struct osmocom_ms *ms, struct msgb *msg) {
	libosmosim_dbg("%s\n", "sim_up_resp() called");
        uint16_t len = msg->len - sizeof(struct l1ctl_hdr);
	if (len > 0) {
		sim_present = 1;
	}

	uint8_t *data = msg->data;
	int length = msg->len;
	LOGP(DSIM, LOGL_INFO, "SIM ATR (len=%d) : %s\n", length, osmo_hexdump(data, length));
	apdu_length = length;
	memcpy(apdu_resp, data, length);
	unlock(); // unlock a lock performed by osmo_sim_powerup() and osmosim_reset()
	libosmosim_dbg("%s\n", "sim_up_resp() exiting");
	return len;
}

/* this function is called-back when L1CTL_SIM_CONF message is received (when SIM needs to return a response to APDU) */
int osmosim_sim_apdu_resp(struct osmocom_ms *ms, struct msgb *msg) {
	libosmosim_dbg("%s\n", "sim_apdu_resp() called");
	uint8_t *data = msg->data;
	int length = msg->len;
	uint8_t sw1, sw2;
	/* process status */
        if (length < 2) {
                msgb_free(msg);
                return 0;
        }
        sw1 = data[length - 2];
        sw2 = data[length - 1];
        LOGP(DSIM, LOGL_INFO, "received APDU (len=%d sw1=0x%02x sw2=0x%02x)\n", length, sw1, sw2);

	apdu_length = length;
	memcpy(apdu_resp, data, length);

	unlock();
	libosmosim_dbg("%s\n", "sim_apdu_resp() exiting");
	return 0;
}

/*
 * osmosim_* functions that communicate over layer 1
 */

/* initialize stderr_target and set default logging to all known debug symbols + LOGL_FATAL */
void osmosim_log_init() {
	libosmosim_dbg("%s\n", "osmosim_log_init() called");
	log_init(&log_info, NULL);
	stderr_target = NULL;
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);
	log_set_all_filter(stderr_target, 1);
	const char *debug_default = "DCS:DNB:DPLMN:DRR:DMM:DSIM:DCC:DMNCC:DSS:DLSMS:DPAG:DSUM:DL1C";
	log_parse_category_mask(stderr_target, debug_default);
	log_set_log_level(stderr_target, LOGL_FATAL);
	libosmosim_dbg("%s\n", "osmosim_log_init() exiting");
}

/* open layer2 socket and prepare ms struct for communication with Osmocom BB phone */
int osmosim_init()
{
	libosmosim_dbg("%s\n", "osmosim_init() called");
	int rc;

	flckfile = fopen(lckfile, "w"); // open a lock file
	if (!stderr_target) {
		osmosim_log_init();
	}

	l23_ctx = talloc_named_const(NULL, 1, "layer2 context");

	ms = talloc_zero(l23_ctx, struct osmocom_ms);
	if (!ms) {
		fprintf(stderr, "Failed to allocate MS\n");
		return 0;
	}

	sprintf(ms->name, "1");

	rc = layer2_open(ms, layer2_socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during layer2_open()\n");
		return 0;
	}

	libosmosim_dbg("%s\n", "osmosim_init() exiting");	
	return 1;
}

/* sets a loglevel if log_level != 0 as zero means "don't set loglevel only return the current one" */
int osmosim_loglevel(int log_level) {
	libosmosim_dbg("%s%s%s\n", "osmosim_log_level() called (log_level = ", log_level_str(log_level), ")");
	if (!stderr_target) {

		osmosim_log_init();
	}

	if (log_level) {
		log_set_log_level(stderr_target, log_level);
	}

	libosmosim_dbg("%s\n", "osmosim_log_level() exiting");
	return stderr_target->loglevel;
}

/* power up the sim (actual voltage) */
int osmosim_powerup() {
	libosmosim_dbg("%s\n", "osmosim_powerup() called");
	in_shutdown = 0;
	lock();
	l1ctl_tx_sim_powerup(ms);
	osmo_select_main(0); // wait for write to the socket
	osmo_select_main(0); // wait for read from the socket

	libosmosim_dbg("%s\n", "osmosim_powerup() exiting");
	return sim_present; // 1 if sim is in the phone, 0 if sim is absent
}

/* power down the sim (actual voltage) */
void osmosim_powerdown() {
	libosmosim_dbg("%s\n", "osmosim_powerdown() called");
	in_shutdown = 1;
	dolock();
	l1ctl_tx_sim_powerdown(ms);
	osmo_select_main(0); // wait for write to the socket (no response required)
	unlock();
	libosmosim_dbg("%s\n", "osmosim_powerdown() exiting");
}

/* trigger SIM reset (actuall reset pin on the sim) */
int osmosim_reset() {
	libosmosim_dbg("%s\n", "osmosim_reset() called");
	lock();
	l1ctl_tx_sim_reset(ms);
	osmo_select_main(0); // wait for write to the socket
	osmo_select_main(0); // wait for read from the socket

	libosmosim_dbg("%s\n", "osmosim_reset() exiting");
	return sim_present;
}

/* transmit a APDU to the SIM and wait for response to arrive (sim_apdu_resp), then read it out of a global buffer) */
int osmosim_transmit(char* data, unsigned int len, char** out)
{
	libosmosim_dbg("%s\n", "osmosim_transmit() called");
        lock();
	LOGP(DSIM, LOGL_INFO, "sending APDU (class 0x%02x, ins 0x%02x)\n", data[0], data[1]);
	l1ctl_tx_sim_req(ms, (uint8_t*)data, len);
	osmo_select_main(0);	
	osmo_select_main(0);

	if (out) {
		*out = (char*)&apdu_resp;
	}
	libosmosim_dbg("%s\n", "osmosim_transmit() exiting");
	return apdu_length;
}

/* perform lock without checking for shutdown as this is initiated during shutdown */
void osmosim_exit() {
	libosmosim_dbg("%s\n", "osmosim_exit() called");
	dolock();
	layer2_close(ms);
	talloc_free(ms);
	talloc_free(l23_ctx);
	log_target_destroy(stderr_target);
	stderr_target = NULL;
	unlock();
	fclose(flckfile);
	libosmosim_dbg("%s\n", "osmosim_exit() exiting");
}

/*
 * Java highlevel API wrappers for functions above
 */

JNIEXPORT jboolean JNICALL Java_de_srlabs_simlib_osmocardprovider_OsmoJNI_init (JNIEnv *env, jobject obj) {
	return osmosim_init();
}

JNIEXPORT jint JNICALL Java_de_srlabs_simlib_osmocardprovider_OsmoJNI_loglevel (JNIEnv *env, jobject obj, jint log_level) {
	return osmosim_loglevel(log_level);
}

JNIEXPORT jbyteArray JNICALL Java_de_srlabs_simlib_osmocardprovider_OsmoJNI_simPowerup (JNIEnv *env, jobject obj) {

	osmosim_powerup();

	jbyteArray result;
	result = (*env)->NewByteArray(env, apdu_length);
	(*env)->SetByteArrayRegion(env, result, 0, apdu_length, (jbyte*)apdu_resp);
	
	return result;
}

JNIEXPORT void JNICALL Java_de_srlabs_simlib_osmocardprovider_OsmoJNI_simPowerdown (JNIEnv *env, jobject obj) {
	osmosim_powerdown();
}

JNIEXPORT jboolean JNICALL Java_de_srlabs_simlib_osmocardprovider_OsmoJNI_simReset (JNIEnv *env, jobject obj) {
	return osmosim_reset();
}

JNIEXPORT jbyteArray JNICALL Java_de_srlabs_simlib_osmocardprovider_OsmoJNI_transmit (JNIEnv *env, jobject obj, jbyteArray data) {

	char req[255];
	int len;
	char *resp;

	len = (*env)->GetArrayLength(env, data);
	(*env)->GetByteArrayRegion(env, data, 0, len, (jbyte*)req);

	len = osmosim_transmit(req, len, &resp);

	jbyteArray result;
	result = (*env)->NewByteArray(env, len);
	(*env)->SetByteArrayRegion(env, result, 0, len, (jbyte*)resp);

	return result;
}

JNIEXPORT void JNICALL Java_de_srlabs_simlib_osmocardprovider_OsmoJNI_exit (JNIEnv *env, jobject obj) {
	osmosim_exit();
}
