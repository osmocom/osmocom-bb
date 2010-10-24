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

#include <stdio.h>
#include <stdlib.h>

#include <osmocom/bb/common/osmocom_data.h>

#include "log.h"

extern struct power power;
extern struct sysinfo sysinfo;
extern struct node_power *node_power_first;
extern struct node_power **node_power_last_p;
extern struct node_mcc *node_mcc_first;

struct node_mcc *get_node_mcc(uint16_t mcc)
{
	struct node_mcc *node_mcc;
	struct node_mcc **node_mcc_p = &node_mcc_first;

//printf("add mcc %d\n", mcc);
	while (*node_mcc_p) {
		/* found in list */
		if ((*node_mcc_p)->mcc == mcc)
			return *node_mcc_p;
		/* insert into list */
		if ((*node_mcc_p)->mcc > mcc)
			break;
		node_mcc_p = &((*node_mcc_p)->next);
	}

//printf("new mcc %d\n", mcc);
	/* append or insert to list */
	node_mcc = calloc(1, sizeof(struct node_mcc));
	if (!node_mcc)
		return NULL;
	node_mcc->mcc = mcc;
	node_mcc->next = *node_mcc_p;
	*node_mcc_p = node_mcc;
	return node_mcc;
}

struct node_mnc *get_node_mnc(struct node_mcc *mcc, uint16_t mnc)
{
	struct node_mnc *node_mnc;
	struct node_mnc **node_mnc_p = &mcc->mnc;

	while (*node_mnc_p) {
		/* found in list */
		if ((*node_mnc_p)->mnc == mnc)
			return *node_mnc_p;
		/* insert into list */
		if ((*node_mnc_p)->mnc > mnc)
			break;
		node_mnc_p = &((*node_mnc_p)->next);
	}

	/* append or insert to list */
	node_mnc = calloc(1, sizeof(struct node_mnc));
	if (!node_mnc)
		return NULL;
	node_mnc->mnc = mnc;
	node_mnc->next = *node_mnc_p;
	*node_mnc_p = node_mnc;
	return node_mnc;
}

struct node_lac *get_node_lac(struct node_mnc *mnc, uint16_t lac)
{
	struct node_lac *node_lac;
	struct node_lac **node_lac_p = &mnc->lac;

	while (*node_lac_p) {
		/* found in list */
		if ((*node_lac_p)->lac == lac)
			return *node_lac_p;
		/* insert into list */
		if ((*node_lac_p)->lac > lac)
			break;
		node_lac_p = &((*node_lac_p)->next);
	}

	/* append or insert to list */
	node_lac = calloc(1, sizeof(struct node_lac));
	if (!node_lac)
		return NULL;
	node_lac->lac = lac;
	node_lac->next = *node_lac_p;
	*node_lac_p = node_lac;
	return node_lac;
}

struct node_cell *get_node_cell(struct node_lac *lac, uint16_t cellid)
{
	struct node_cell *node_cell;
	struct node_cell **node_cell_p = &lac->cell;

	while (*node_cell_p) {
		/* found in list */
		if ((*node_cell_p)->cellid == cellid)
			return *node_cell_p;
		/* insert into list */
		if ((*node_cell_p)->cellid > cellid)
			break;
		node_cell_p = &((*node_cell_p)->next);
	}

	/* append or insert to list */
	node_cell = calloc(1, sizeof(struct node_cell));
	if (!node_cell)
		return NULL;
	node_cell->meas_last_p = &node_cell->meas;
	node_cell->cellid = cellid;
	node_cell->next = *node_cell_p;
	*node_cell_p = node_cell;
	return node_cell;
}

struct node_meas *add_node_meas(struct node_cell *cell)
{
	struct node_meas *node_meas;

	/* append to list */
	node_meas = calloc(1, sizeof(struct node_meas));
	if (!node_meas)
		return NULL;
	node_meas->gmt = sysinfo.gmt;
	node_meas->rxlev = sysinfo.rxlev;
	if (sysinfo.ta_valid) {
		node_meas->ta_valid = 1;
		node_meas->ta = sysinfo.ta;
	}
	if (sysinfo.gps_valid) {
		node_meas->gps_valid = 1;
		node_meas->longitude = sysinfo.longitude;
		node_meas->latitude = sysinfo.latitude;
	}
	*cell->meas_last_p = node_meas;
	cell->meas_last_p = &node_meas->next;
	return node_meas;
}

/* read "<ncc>,<bcc>" */
static void read_log_bsic(char *buffer)
{
	char *p;
	uint8_t bsic;

	/* skip first spaces */
	while (*buffer == ' ')
		buffer++;

	/* read ncc */
	p = buffer;
	while (*p > ' ' && *p != ',')
		p++;
	if (*p == '\0')
		return; /* no value */
	*p++ = '\0';
	bsic = atoi(buffer) << 3;
	buffer = p;

	/* read latitude */
	bsic |= atoi(buffer);

	sysinfo.bsic = bsic;
}

/* read "<longitude> <latitude>" */
static void read_log_pos(char *buffer, double *longitude, double *latitude,
	uint8_t *valid)
{
	char *p;

	/* skip first spaces */
	while (*buffer == ' ')
		buffer++;

	/* read longitude */
	p = buffer;
	while (*p > ' ')
		p++;
	if (*p == '\0')
		return; /* no value after longitude */
	*p++ = '\0';
	*longitude = atof(buffer);
	buffer = p;

	/* skip second spaces */
	while (*buffer == ' ')
		buffer++;

	/* read latitude */
	*latitude = atof(buffer);

	*valid = 1;
}

/* read "<arfcn> <value> <next value> ...." */
static void read_log_power(char *buffer)
{
	char *p;
	int arfcn;

	/* skip first spaces */
	while (*buffer == ' ')
		buffer++;

	/* read arfcn */
	p = buffer;
	while (*p > ' ')
		p++;
	if (*p == '\0')
		return; /* no value after arfcn */
	*p++ = '\0';
	arfcn = atoi(buffer);
	buffer = p;

	while (*buffer) {
		/* wrong arfcn */
		if (arfcn < 0 || arfcn > 1023)
			break;
		/* skip spaces */
		while (*buffer == ' ')
			buffer++;
		/* get value */
		p = buffer;
		while (*p > ' ')
			p++;
		/* last value */
		if (*p == '\0') {
			power.rxlev[arfcn] = atoi(buffer);
			break;
		}
		*p++ = '\0';
		power.rxlev[arfcn] = atoi(buffer);
		arfcn++;
		buffer = p;
	}
}

/* read "xx xx xx xx xx...." */
static void read_log_si(char *buffer, uint8_t *data)
{
	uint8_t si[23];
	int i;

//	printf("%s ", buffer);
	for (i = 0; i < 23; i++) {
		while (*buffer == ' ')
			buffer++;
		if (*buffer >= '0' && *buffer <= '9')
			si[i] = (*buffer - '0') << 4;
		else if (*buffer >= 'a' && *buffer <= 'f')
			si[i] = (*buffer - 'a' + 10) << 4;
		else if (*buffer >= 'A' && *buffer <= 'F')
			si[i] = (*buffer - 'A' + 10) << 4;
		else
			break;
		buffer++;
		if (*buffer >= '0' && *buffer <= '9')
			si[i] += *buffer - '0';
		else if (*buffer >= 'a' && *buffer <= 'f')
			si[i] += *buffer - 'a' + 10;
		else if (*buffer >= 'A' && *buffer <= 'F')
			si[i] += *buffer - 'A' + 10;
		else
			break;
		buffer++;
//		printf("%02x ", si[i]);
	}
//	printf("\n");

	if (i == 23)
		memcpy(data, si, 23);
}

/* read next record from log file */
int read_log(FILE *infp)
{
	static int type = LOG_TYPE_NONE, ret;
	char buffer[256];

	memset(&sysinfo, 0, sizeof(sysinfo));
	memset(&power, 0, sizeof(power));
	memset(&power.rxlev, -128, sizeof(power.rxlev));

	if (feof(infp))
		return LOG_TYPE_NONE;

	while (fgets(buffer, sizeof(buffer), infp)) {
		buffer[sizeof(buffer) - 1] = 0;
		if (buffer[0])
			buffer[strlen(buffer) - 1] = '\0';
		if (buffer[0] == '[') {
			if (!strcmp(buffer, "[sysinfo]")) {
				ret = type;
				type = LOG_TYPE_SYSINFO;
				if (ret != LOG_TYPE_NONE)
					return ret;
			} else
			if (!strcmp(buffer, "[power]")) {
				ret = type;
				type = LOG_TYPE_POWER;
				if (ret != LOG_TYPE_NONE)
					return ret;
			} else {
				type = LOG_TYPE_NONE;
			}
			continue;
		}
		switch (type) {
		case LOG_TYPE_SYSINFO:
			if (!strncmp(buffer, "arfcn ", 6))
				sysinfo.arfcn = atoi(buffer + 6);
			else if (!strncmp(buffer, "si1 ", 4))
				read_log_si(buffer + 4, sysinfo.si1);
			else if (!strncmp(buffer, "si2 ", 4))
				read_log_si(buffer + 4, sysinfo.si2);
			else if (!strncmp(buffer, "si2bis ", 7))
				read_log_si(buffer + 7, sysinfo.si2bis);
			else if (!strncmp(buffer, "si2ter ", 7))
				read_log_si(buffer + 7, sysinfo.si2ter);
			else if (!strncmp(buffer, "si3 ", 4))
				read_log_si(buffer + 4, sysinfo.si3);
			else if (!strncmp(buffer, "si4 ", 4))
				read_log_si(buffer + 4, sysinfo.si4);
			else if (!strncmp(buffer, "time ", 5))
				sysinfo.gmt = strtoul(buffer + 5, NULL, 0);
			else if (!strncmp(buffer, "position ", 9))
				read_log_pos(buffer + 9, &sysinfo.longitude,
					&sysinfo.latitude, &sysinfo.gps_valid);
			else if (!strncmp(buffer, "rxlev ", 5))
				sysinfo.rxlev =
					strtoul(buffer + 5, NULL, 0);
			else if (!strncmp(buffer, "bsic ", 5))
				read_log_bsic(buffer + 5);
			else if (!strncmp(buffer, "ta ", 3)) {
				sysinfo.ta_valid = 1;
				sysinfo.ta = atoi(buffer + 3);
			}
			break;
		case LOG_TYPE_POWER:
			if (!strncmp(buffer, "arfcn ", 6))
				read_log_power(buffer + 6);
			else if (!strncmp(buffer, "time ", 5))
				power.gmt = strtoul(buffer + 5, NULL, 0);
			else if (!strncmp(buffer, "position ", 9))
				read_log_pos(buffer + 9, &power.longitude,
					&power.latitude, &sysinfo.gps_valid);
			break;
		}
	}

	return type;
}

