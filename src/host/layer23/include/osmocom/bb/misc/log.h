
enum {
	LOG_TYPE_NONE = 0,
	LOG_TYPE_SYSINFO,
	LOG_TYPE_POWER,
};

struct power {
	uint8_t gps_valid;
	double longitude, latitude;
	time_t gmt;
	int8_t rxlev[1024];
};

struct node_power {
	struct node_power *next;
	struct power power;
};

struct node_mcc {
	struct node_mcc *next;
	uint16_t mcc;
	struct node_mnc *mnc;
};

struct node_mnc {
	struct node_mnc *next;
	uint16_t mnc;
	struct node_lac *lac;
};

struct node_lac {
	struct node_lac *next;
	uint16_t lac;
	struct node_cell *cell;
};

struct sysinfo {
	uint16_t arfcn;
	int8_t rxlev;
	uint8_t bsic;
	uint8_t gps_valid;
	double longitude, latitude;
	time_t gmt;
	uint8_t	si1[23];
	uint8_t	si2[23];
	uint8_t	si2bis[23];
	uint8_t	si2ter[23];
	uint8_t	si3[23];
	uint8_t	si4[23];
	uint8_t ta_valid;
	uint8_t ta;
};

struct node_cell {
	struct node_cell *next;
	uint16_t cellid;
	uint8_t content; /* indicates, if sysinfo is already applied */
	struct node_meas *meas, **meas_last_p;
	struct sysinfo sysinfo;
	struct gsm48_sysinfo s;
};

struct node_meas {
	struct node_meas *next;
	time_t gmt;
	int8_t rxlev;
	uint8_t gps_valid;
	double longitude, latitude;
	uint8_t ta_valid;
	uint8_t ta;
};

struct node_mcc *get_node_mcc(uint16_t mcc);
struct node_mnc *get_node_mnc(struct node_mcc *mcc, uint16_t mnc);
struct node_lac *get_node_lac(struct node_mnc *mnc, uint16_t lac);
struct node_cell *get_node_cell(struct node_lac *lac, uint16_t cellid);
struct node_meas *add_node_meas(struct node_cell *cell);
int read_log(FILE *infp);

