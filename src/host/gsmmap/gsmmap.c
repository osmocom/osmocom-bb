/* Conversion of logged cells to KML file */

/* (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#warning todo bsic
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#define GSM_TA_M 553.85
#define PI 3.1415926536

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/common/logging.h>

#include "log.h"
#include "geo.h"
#include "locate.h"

/*
 * structure of power and cell infos
 */

struct power power;
struct sysinfo sysinfo;
static struct node_power *node_power_first = NULL;
static struct node_power **node_power_last_p = &node_power_first;
struct node_mcc *node_mcc_first = NULL;
int log_lines = 0, log_debug = 0;


static void nomem(void)
{
	fprintf(stderr, "No mem!\n");
	exit(-ENOMEM);
}

static void add_power()
{
	struct node_power *node_power;

//	printf("New Power\n");
	/* append or insert to list */
	node_power = calloc(1, sizeof(struct node_power));
	if (!node_power)
		nomem();
	*node_power_last_p = node_power;
	node_power_last_p = &node_power->next;
	memcpy(&node_power->power, &power, sizeof(power));
}

static void print_si(void *priv, const char *fmt, ...)
{
	char buffer[1000];
	FILE *outfp = (FILE *)priv;
	va_list args;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
	buffer[sizeof(buffer) - 1] = '\0';
	va_end(args);

	if (buffer[0])
		fprintf(outfp, "%s", buffer);
}

static void add_sysinfo()
{
	struct gsm48_sysinfo s;
	struct node_mcc *mcc;
	struct node_mnc *mnc;
	struct node_lac *lac;
	struct node_cell *cell;
	struct node_meas *meas;

	memset(&s, 0, sizeof(s));

	/* decode sysinfo */
	if (sysinfo.si1[2])
		gsm48_decode_sysinfo1(&s,
			(struct gsm48_system_information_type_1 *) sysinfo.si1,
			23);
	if (sysinfo.si2[2])
		gsm48_decode_sysinfo2(&s,
			(struct gsm48_system_information_type_2 *) sysinfo.si2,
			23);
	if (sysinfo.si2bis[2])
		gsm48_decode_sysinfo2bis(&s,
			(struct gsm48_system_information_type_2bis *)
				sysinfo.si2bis,
			23);
	if (sysinfo.si2ter[2])
		gsm48_decode_sysinfo2ter(&s,
			(struct gsm48_system_information_type_2ter *)
				sysinfo.si2ter,
			23);
	if (sysinfo.si3[2])
		gsm48_decode_sysinfo3(&s,
			(struct gsm48_system_information_type_3 *) sysinfo.si3,
			23);
	if (sysinfo.si4[2])
		gsm48_decode_sysinfo4(&s,
			(struct gsm48_system_information_type_4 *) sysinfo.si4,
			23);
	printf("--------------------------------------------------------------------------\n");
	gsm48_sysinfo_dump(&s, sysinfo.arfcn, print_si, stdout, NULL);
	mcc = get_node_mcc(s.mcc);
	if (!mcc)
		nomem();
	mnc = get_node_mnc(mcc, s.mnc);
	if (!mnc)
		nomem();
	lac = get_node_lac(mnc, s.lac);
	if (!lac)
		nomem();
	cell = get_node_cell(lac, s.cell_id);
	if (!cell)
		nomem();
	meas = add_node_meas(cell);
	if (!meas)
		nomem();
	if (!cell->content) {
		cell->content = 1;
		memcpy(&cell->sysinfo, &sysinfo, sizeof(sysinfo));
		memcpy(&cell->s, &s, sizeof(s));
	} else {
		if (memcmp(&cell->sysinfo.si1, sysinfo.si1,
			sizeof(sysinfo.si1))) {
new_sysinfo:
			fprintf(stderr, "FIXME: the cell changed sysinfo\n");
			return;
		}
		if (memcmp(&cell->sysinfo.si2, sysinfo.si2,
			sizeof(sysinfo.si2)))
			goto new_sysinfo;
		if (memcmp(&cell->sysinfo.si2bis, sysinfo.si2bis,
			sizeof(sysinfo.si2bis)))
			goto new_sysinfo;
		if (memcmp(&cell->sysinfo.si2ter, sysinfo.si2ter,
			sizeof(sysinfo.si2ter)))
			goto new_sysinfo;
		if (memcmp(&cell->sysinfo.si3, sysinfo.si3,
			sizeof(sysinfo.si3)))
			goto new_sysinfo;
		if (memcmp(&cell->sysinfo.si4, sysinfo.si4,
			sizeof(sysinfo.si4)))
			goto new_sysinfo;
	}
}

void kml_header(FILE *outfp, char *name)
{
	/* XML header */
	fprintf(outfp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

	/* KML open tag */
	fprintf(outfp, "<kml xmlns=\"http://www.opengis.net/kml/2.2\" "
		"xmlns:gx=\"http://www.google.com/kml/ext/2.2\" "
		"xmlns:kml=\"http://www.opengis.net/kml/2.2\" "
		"xmlns:atom=\"http://www.w3.org/2005/Atom\">\n");

	/* document open tag */
	fprintf(outfp, "<Document>\n");

	/* pushpin */
	fprintf(outfp, "\t<Style id=\"sn_placemark_red_pushpin\">\n");
	fprintf(outfp, "\t\t<IconStyle>\n");
	fprintf(outfp, "\t\t\t<scale>1.1</scale>\n");
	fprintf(outfp, "\t\t\t<Icon>\n");
	fprintf(outfp, "\t\t\t\t<href>http://maps.google.com/mapfiles/kml/"
		"pushpin/red-pushpin.png</href>\n");
	fprintf(outfp, "\t\t\t</Icon>\n");
	fprintf(outfp, "\t\t</IconStyle>\n");
	fprintf(outfp, "\t\t<ListStyle>\n");
	fprintf(outfp, "\t\t</ListStyle>\n");
	fprintf(outfp, "\t</Style>\n");
	fprintf(outfp, "\t<Style id=\"sh_placemark_red_pushpin_highlight\">\n");
	fprintf(outfp, "\t\t<IconStyle>\n");
	fprintf(outfp, "\t\t\t<scale>1.3</scale>\n");
	fprintf(outfp, "\t\t\t<Icon>\n");
	fprintf(outfp, "\t\t\t\t<href>http://maps.google.com/mapfiles/kml/"
		"pushpin/red-pushpin.png</href>\n");
	fprintf(outfp, "\t\t\t</Icon>\n");
	fprintf(outfp, "\t\t</IconStyle>\n");
	fprintf(outfp, "\t\t<ListStyle>\n");
	fprintf(outfp, "\t\t</ListStyle>\n");
	fprintf(outfp, "\t</Style>\n");
	fprintf(outfp, "\t<StyleMap id=\"msn_placemark_red_pushpin\">\n");
	fprintf(outfp, "\t\t<Pair>\n");
	fprintf(outfp, "\t\t\t<key>normal</key>\n");
	fprintf(outfp, "\t\t\t<styleUrl>#sn_placemark_red_pushpin"
		"</styleUrl>\n");
	fprintf(outfp, "\t\t</Pair>\n");
	fprintf(outfp, "\t\t<Pair>\n");
	fprintf(outfp, "\t\t\t<key>highlight</key>\n");
	fprintf(outfp, "\t\t\t<styleUrl>#sh_placemark_red_pushpin_highlight"
		"</styleUrl>\n");
	fprintf(outfp, "\t\t</Pair>\n");
	fprintf(outfp, "\t</StyleMap>\n");

	fprintf(outfp, "\t<Style id=\"sn_placemark_grn_pushpin\">\n");
	fprintf(outfp, "\t\t<IconStyle>\n");
	fprintf(outfp, "\t\t\t<scale>1.1</scale>\n");
	fprintf(outfp, "\t\t\t<Icon>\n");
	fprintf(outfp, "\t\t\t\t<href>http://maps.google.com/mapfiles/kml/"
		"pushpin/grn-pushpin.png</href>\n");
	fprintf(outfp, "\t\t\t</Icon>\n");
	fprintf(outfp, "\t\t</IconStyle>\n");
	fprintf(outfp, "\t\t<ListStyle>\n");
	fprintf(outfp, "\t\t</ListStyle>\n");
	fprintf(outfp, "\t</Style>\n");
	fprintf(outfp, "\t<Style id=\"sh_placemark_grn_pushpin_highlight\">\n");
	fprintf(outfp, "\t\t<IconStyle>\n");
	fprintf(outfp, "\t\t\t<scale>1.3</scale>\n");
	fprintf(outfp, "\t\t\t<Icon>\n");
	fprintf(outfp, "\t\t\t\t<href>http://maps.google.com/mapfiles/kml/"
		"pushpin/grn-pushpin.png</href>\n");
	fprintf(outfp, "\t\t\t</Icon>\n");
	fprintf(outfp, "\t\t</IconStyle>\n");
	fprintf(outfp, "\t\t<ListStyle>\n");
	fprintf(outfp, "\t\t</ListStyle>\n");
	fprintf(outfp, "\t</Style>\n");
	fprintf(outfp, "\t<StyleMap id=\"msn_placemark_grn_pushpin\">\n");
	fprintf(outfp, "\t\t<Pair>\n");
	fprintf(outfp, "\t\t\t<key>normal</key>\n");
	fprintf(outfp, "\t\t\t<styleUrl>#sn_placemark_grn_pushpin"
		"</styleUrl>\n");
	fprintf(outfp, "\t\t</Pair>\n");
	fprintf(outfp, "\t\t<Pair>\n");
	fprintf(outfp, "\t\t\t<key>highlight</key>\n");
	fprintf(outfp, "\t\t\t<styleUrl>#sh_placemark_grn_pushpin_highlight"
		"</styleUrl>\n");
	fprintf(outfp, "\t\t</Pair>\n");
	fprintf(outfp, "\t</StyleMap>\n");

	/* circle */
	fprintf(outfp, "\t<Style id=\"sn_placemark_circle\">\n");
	fprintf(outfp, "\t\t<IconStyle>\n");
	fprintf(outfp, "\t\t\t<scale>1.0</scale>\n");
	fprintf(outfp, "\t\t\t<Icon>\n");
	fprintf(outfp, "\t\t\t\t<href>http://maps.google.com/mapfiles/kml/"
		"shapes/placemark_circle.png</href>\n");
	fprintf(outfp, "\t\t\t</Icon>\n");
	fprintf(outfp, "\t\t</IconStyle>\n");
	fprintf(outfp, "\t\t<ListStyle>\n");
	fprintf(outfp, "\t\t</ListStyle>\n");
	fprintf(outfp, "\t</Style>\n");
	fprintf(outfp, "\t<Style id=\"sh_placemark_circle_highlight\">\n");
	fprintf(outfp, "\t\t<IconStyle>\n");
	fprintf(outfp, "\t\t\t<scale>1.2</scale>\n");
	fprintf(outfp, "\t\t\t<Icon>\n");
	fprintf(outfp, "\t\t\t\t<href>http://maps.google.com/mapfiles/kml/"
		"shapes/placemark_circle_highlight.png</href>\n");
	fprintf(outfp, "\t\t\t</Icon>\n");
	fprintf(outfp, "\t\t</IconStyle>\n");
	fprintf(outfp, "\t\t<ListStyle>\n");
	fprintf(outfp, "\t\t</ListStyle>\n");
	fprintf(outfp, "\t</Style>\n");
	fprintf(outfp, "\t<StyleMap id=\"msn_placemark_circle\">\n");
	fprintf(outfp, "\t\t<Pair>\n");
	fprintf(outfp, "\t\t\t<key>normal</key>\n");
	fprintf(outfp, "\t\t\t<styleUrl>#sn_placemark_circle</styleUrl>\n");
	fprintf(outfp, "\t\t</Pair>\n");
	fprintf(outfp, "\t\t<Pair>\n");
	fprintf(outfp, "\t\t\t<key>highlight</key>\n");
	fprintf(outfp, "\t\t\t<styleUrl>#sh_placemark_circle_highlight"
		"</styleUrl>\n");
	fprintf(outfp, "\t\t</Pair>\n");
	fprintf(outfp, "\t</StyleMap>\n");
}

void kml_footer(FILE *outfp)
{
	/* document close tag */
	fprintf(outfp, "</Document>\n");

	/* KML close tag */
	fprintf(outfp, "</kml>\n");

}

void kml_meas(FILE *outfp, struct node_meas *meas, int n, uint16_t mcc,
	uint16_t mnc, uint16_t lac, uint16_t cellid)
{
	struct tm *tm = localtime(&meas->gmt);

	fprintf(outfp, "\t\t\t\t\t<Placemark>\n");
	fprintf(outfp, "\t\t\t\t\t\t<name>%d: %d</name>\n", n, meas->rxlev);
	fprintf(outfp, "\t\t\t\t\t\t<description>\n");
	fprintf(outfp, "MCC=%s MNC=%s\nLAC=%04x CELL-ID=%04x\n(%s %s)\n",
		gsm_print_mcc(mcc), gsm_print_mnc(mnc), lac, cellid,
		gsm_get_mcc(mcc), gsm_get_mnc(mcc, mnc));
	fprintf(outfp, "\n%s", asctime(tm));
	fprintf(outfp, "RX-LEV %d dBm\n", meas->rxlev);
	if (meas->ta_valid)
		fprintf(outfp, "TA=%d (%d-%d meter)\n", meas->ta,
			(int)(GSM_TA_M * meas->ta),
			(int)(GSM_TA_M * (meas->ta + 1)));
	fprintf(outfp, "\t\t\t\t\t\t</description>\n");
	fprintf(outfp, "\t\t\t\t\t\t<LookAt>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<longitude>%.8f</longitude>\n",
		meas->longitude);
	fprintf(outfp, "\t\t\t\t\t\t\t<latitude>%.8f</latitude>\n",
		meas->latitude);
	fprintf(outfp, "\t\t\t\t\t\t\t<altitude>0</altitude>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<tilt>0</tilt>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<altitudeMode>relativeToGround"
		"</altitudeMode>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<gx:altitudeMode>relativeToSeaFloor"
		"</gx:altitudeMode>\n");
	fprintf(outfp, "\t\t\t\t\t\t</LookAt>\n");
	fprintf(outfp, "\t\t\t\t\t\t<styleUrl>#msn_placemark_circle"
		"</styleUrl>\n");
	fprintf(outfp, "\t\t\t\t\t\t<Point>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<coordinates>%.8f,%.8f</coordinates>\n",
		meas->longitude, meas->latitude);
	fprintf(outfp, "\t\t\t\t\t\t</Point>\n");
	fprintf(outfp, "\t\t\t\t\t</Placemark>\n");
}

double debug_long, debug_lat, debug_x_scale;
FILE *debug_fp;

void kml_cell(FILE *outfp, struct node_cell *cell)
{
	struct node_meas *meas;
	double x, y, z, sum_x = 0, sum_y = 0, sum_z = 0, longitude, latitude;
	int n, known = 0;

	meas = cell->meas;
	n = 0;
	while (meas) {
		if (meas->gps_valid && meas->ta_valid) {
			geo2space(&x, &y, &z, meas->longitude, meas->latitude);
			sum_x += x;
			sum_y += y;
			sum_z += z;
			n++;
		}
		meas = meas->next;
	}
	if (!n)
		return;
	if (n < 3) {
		x = sum_x / n;
		y = sum_y / n;
		z = sum_z / n;
		space2geo(&longitude, &latitude, x, y, z);
	} else {
		struct probe *probe_first = NULL, *probe,
			     **probe_last_p = &probe_first;
		double x_scale;

		/* translate to flat surface */
		meas = cell->meas;
		x_scale = 1.0 / cos(meas->latitude / 180.0 * PI);
		longitude = meas->longitude;
		latitude = meas->latitude;
		debug_x_scale = x_scale;
		debug_long = longitude;
		debug_lat = latitude;
		debug_fp = outfp;
		while (meas) {
			if (meas->gps_valid && meas->ta_valid) {
				probe = calloc(1, sizeof(struct probe));
				if (!probe)
					nomem();
				probe->x = (meas->longitude - longitude) /
						x_scale;
				if (x < -180)
					x += 360;
				else if (x > 180)
					x -= 360;
				probe->y = meas->latitude - latitude;
				probe->dist = GSM_TA_M * (0.5 +
					(double)meas->ta) /
					(EQUATOR_RADIUS * PI / 180.0);
				*probe_last_p = probe;
				probe_last_p = &probe->next;
			}
			meas = meas->next;
		}

		/* locate */
		locate_cell(probe_first, &x, &y);

		/* translate from flat surface */
		longitude += x * x_scale;
		if (longitude < 0)
			longitude += 360;
		else if (longitude >= 360)
			longitude -= 360;
		latitude += y;

		/* remove probes */
		while (probe_first) {
			probe = probe_first;
			probe_first = probe->next;
			free(probe);
		}

		known = 1;
	}

	if (!known)
		return;

	fprintf(outfp, "\t\t\t\t\t<Placemark>\n");
	fprintf(outfp, "\t\t\t\t\t\t<name>MCC=%s MNC=%s\nLAC=%04x "
		"CELL-ID=%04x\n(%s %s)</name>\n", gsm_print_mcc(cell->s.mcc),
		gsm_print_mnc(cell->s.mnc), cell->s.lac, cell->s.cell_id,
		gsm_get_mcc(cell->s.mcc),
		gsm_get_mnc(cell->s.mcc, cell->s.mnc));
	fprintf(outfp, "\t\t\t\t\t\t<description>\n");
	gsm48_sysinfo_dump(&cell->s, cell->sysinfo.arfcn, print_si, outfp,
		NULL);
	fprintf(outfp, "\t\t\t\t\t\t</description>\n");
	fprintf(outfp, "\t\t\t\t\t\t<LookAt>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<longitude>%.8f</longitude>\n",
		longitude);
	fprintf(outfp, "\t\t\t\t\t\t\t<latitude>%.8f</latitude>\n", latitude);
	fprintf(outfp, "\t\t\t\t\t\t\t<altitude>0</altitude>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<tilt>0</tilt>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<altitudeMode>relativeToGround"
		"</altitudeMode>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<gx:altitudeMode>relativeToSeaFloor"
		"</gx:altitudeMode>\n");
	fprintf(outfp, "\t\t\t\t\t\t</LookAt>\n");
	if (known)
		fprintf(outfp, "\t\t\t\t\t\t<styleUrl>#msn_placemark_grn_"
			"pushpin</styleUrl>\n");
	else
		fprintf(outfp, "\t\t\t\t\t\t<styleUrl>#msn_placemark_red_"
			"pushpin</styleUrl>\n");
	fprintf(outfp, "\t\t\t\t\t\t<Point>\n");
	fprintf(outfp, "\t\t\t\t\t\t\t<coordinates>%.8f,%.8f</coordinates>\n",
		longitude, latitude);
	fprintf(outfp, "\t\t\t\t\t\t</Point>\n");
	fprintf(outfp, "\t\t\t\t\t</Placemark>\n");

	if (!log_lines)
		return;

	fprintf(outfp, "\t<Folder>\n");
	fprintf(outfp, "\t\t<name>Lines</name>\n");
	fprintf(outfp, "\t\t<open>0</open>\n");
	fprintf(outfp, "\t\t<visibility>0</visibility>\n");

	geo2space(&x, &y, &z, longitude, latitude);
	meas = cell->meas;
	n = 0;
	while (meas) {
		if (meas->gps_valid) {
			double mx, my, mz, dist;

			geo2space(&mx, &my, &mz, meas->longitude,
				meas->latitude);
			dist = distinspace(x, y, z, mx, my, mz);
			fprintf(outfp, "\t\t<Placemark>\n");
			fprintf(outfp, "\t\t\t<name>Range</name>\n");
			fprintf(outfp, "\t\t\t<description>\n");
			fprintf(outfp, "Distance: %d\n", (int)dist);
			fprintf(outfp, "TA=%d (%d-%d meter)\n", meas->ta,
				(int)(GSM_TA_M * meas->ta),
				(int)(GSM_TA_M * (meas->ta + 1)));
			fprintf(outfp, "\t\t\t</description>\n");
			fprintf(outfp, "\t\t\t<visibility>0</visibility>\n");
			fprintf(outfp, "\t\t\t<LineString>\n");
			fprintf(outfp, "\t\t\t\t<tessellate>1</tessellate>\n");
			fprintf(outfp, "\t\t\t\t<coordinates>\n");
			fprintf(outfp, "%.8f,%.8f\n", longitude, latitude);
			fprintf(outfp, "%.8f,%.8f\n", meas->longitude,
				meas->latitude);
			fprintf(outfp, "\t\t\t\t</coordinates>\n");
			fprintf(outfp, "\t\t\t</LineString>\n");
			fprintf(outfp, "\t\t</Placemark>\n");
		}
		meas = meas->next;
	}
	fprintf(outfp, "\t</Folder>\n");
}

struct log_target *stderr_target;

int main(int argc, char *argv[])
{
	FILE *infp, *outfp;
	int type, n, i;
	char *p;
	struct node_mcc *mcc;
	struct node_mnc *mnc;
	struct node_lac *lac;
	struct node_cell *cell;
	struct node_meas *meas;

	log_init(&log_info, NULL);
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);
	log_set_all_filter(stderr_target, 1);
	log_parse_category_mask(stderr_target, "Dxxx");
	log_set_log_level(stderr_target, LOGL_INFO);

	if (argc <= 2) {
usage:
		fprintf(stderr, "Usage: %s <file.log> <file.kml> "
			"[lines] [debug]\n", argv[0]);
		fprintf(stderr, "lines: Add lines between cell and "
			"Measurement point\n");
		fprintf(stderr, "debug: Add debugging of location algorithm.\n"
			);
		return 0;
	}

	for (i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "lines"))
			log_lines = 1;
		else if (!strcmp(argv[i], "debug"))
			log_debug = 1;
		else goto usage;
	}

	infp = fopen(argv[1], "r");
	if (!infp) {
		fprintf(stderr, "Failed to open '%s' for reading\n", argv[1]);
		return -EIO;
	}

	while ((type = read_log(infp))) {
		switch (type) {
		case LOG_TYPE_SYSINFO:
			add_sysinfo();
			break;
		case LOG_TYPE_POWER:
			add_power();
			break;
		}
	}

	fclose(infp);

	if (!strcmp(argv[2], "-"))
		outfp = stdout;
	else
		outfp = fopen(argv[2], "w");
	if (!outfp) {
		fprintf(stderr, "Failed to open '%s' for writing\n", argv[2]);
		return -EIO;
	}

	/* document name */
	p = argv[2];
	while (strchr(p, '/'))
		p = strchr(p, '/') + 1;

	kml_header(outfp, p);
	mcc = node_mcc_first;
	while (mcc) {
	  printf("MCC: %02x\n", mcc->mcc);
	 /* folder open */
	  fprintf(outfp, "\t<Folder>\n");
	  fprintf(outfp, "\t\t<name>MCC %s (%s)</name>\n",
		gsm_print_mcc(mcc->mcc), gsm_get_mcc(mcc->mcc));
	  fprintf(outfp, "\t\t<open>0</open>\n");
	  mnc = mcc->mnc;
	  while (mnc) {
	    printf(" MNC: %02x\n", mnc->mnc);
	    /* folder open */
	    fprintf(outfp, "\t\t<Folder>\n");
	    fprintf(outfp, "\t\t\t<name>MNC %s (%s)</name>\n",
	    	gsm_print_mnc(mnc->mnc), gsm_get_mnc(mcc->mcc, mnc->mnc));
	    fprintf(outfp, "\t\t\t<open>0</open>\n");
	    lac = mnc->lac;
	    while (lac) {
	      printf("  LAC: %04x\n", lac->lac);
	      /* folder open */
	      fprintf(outfp, "\t\t\t<Folder>\n");
	      fprintf(outfp, "\t\t\t\t<name>LAC %04x</name>\n", lac->lac);
	      fprintf(outfp, "\t\t\t\t<open>0</open>\n");
	      cell = lac->cell;
	      while (cell) {
		printf("   CELL: %04x\n", cell->cellid);
		fprintf(outfp, "\t\t\t\t<Folder>\n");
		fprintf(outfp, "\t\t\t\t\t<name>CELL-ID %04x</name>\n", cell->cellid);
		fprintf(outfp, "\t\t\t\t\t<open>0</open>\n");
		meas = cell->meas;
		n = 0;
		while (meas) {
			if (meas->ta_valid)
				printf("    TA: %d\n", meas->ta);
			if (meas->gps_valid)
				kml_meas(outfp, meas, ++n, mcc->mcc, mnc->mnc,
					lac->lac, cell->cellid);
			meas = meas->next;
		}
		kml_cell(outfp, cell);
		/* folder close */
		fprintf(outfp, "\t\t\t\t</Folder>\n");
		cell = cell->next;
	      }
	      /* folder close */
	      fprintf(outfp, "\t\t\t</Folder>\n");
	      lac = lac->next;
	    }
	    /* folder close */
	    fprintf(outfp, "\t\t</Folder>\n");
	    mnc = mnc->next;
	  }
	  /* folder close */
	  fprintf(outfp, "\t</Folder>\n");
	  mcc = mcc->next;
	}
#if 0
	FIXME: power
	/* folder open */
	fprintf(outfp, "\t<Folder>\n");
	fprintf(outfp, "\t\t<name>Power</name>\n");
	fprintf(outfp, "\t\t<open>0</open>\n");
	power = node_power_first;
	n = 0;
	while (power) {
		/* folder open */
		fprintf(outfp, "\t\t<Folder>\n");
		fprintf(outfp, "\t\t\t<name>Power %d</name>\n", ++n);
		fprintf(outfp, "\t\t\t<open>0</open>\n");
		/* folder close */
		fprintf(outfp, "\t\t</Folder>\n");
		power = power->next;
	}
	/* folder close */
	fprintf(outfp, "\t</Folder>\n");
#endif
	kml_footer(outfp);

	fclose(outfp);

	return 0;
}
