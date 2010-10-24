/* Algorithm to locate a destination by distance measurement:
 */

#include <stdio.h>
#include <errno.h>
#include <math.h>

#include "geo.h"
#include "locate.h"

#define CIRCLE_PROBE	30.0
#define FINETUNE_RADIUS	5.0

extern double debug_long, debug_lat, debug_x_scale;
extern FILE *debug_fp;
extern int log_debug;

static double finetune_x[6], finetune_y[6], finetune_dist[6];

int locate_cell(struct probe *probe_first, double *min_x, double *min_y)
{
	struct probe *probe, *min_probe;
	int i, test_steps, optimized;
	double min_dist, dist, x, y, rad, temp;
	double circle_probe, finetune_radius;

	/* convert meters into degrees */
	circle_probe = CIRCLE_PROBE / (EQUATOR_RADIUS * PI / 180.0);
	finetune_radius = FINETUNE_RADIUS / (EQUATOR_RADIUS * PI / 180.0);

	if (log_debug) {
		fprintf(debug_fp, "<Folder>\n");
		fprintf(debug_fp, "\t<name>Debug Locator</name>\n");
		fprintf(debug_fp, "\t<open>0</open>\n");
		fprintf(debug_fp, "\t<visibility>0</visibility>\n");
	}

	/* get probe of minimum distance */
	min_probe = NULL;
	probe = probe_first;
	min_dist = 42;
	i = 0;
	while (probe) {
		if (log_debug) {
			fprintf(debug_fp, "\t<Placemark>\n");
			fprintf(debug_fp, "\t\t<name>MEAS</name>\n");
			fprintf(debug_fp, "\t\t<visibility>0</visibility>\n");
			fprintf(debug_fp, "\t\t<LineString>\n");
			fprintf(debug_fp, "\t\t\t<tessellate>1</tessellate>\n");
			fprintf(debug_fp, "\t\t\t<coordinates>\n");
			rad = 2.0 * 3.1415927 / 35;
			for (i = 0; i < 35; i++) {
				x = probe->x + probe->dist * sin(rad * i);
				y = probe->y + probe->dist * cos(rad * i);
				fprintf(debug_fp, "%.8f,%.8f\n", debug_long +
					x * debug_x_scale, debug_lat + y);
			}
			fprintf(debug_fp, "\t\t\t</coordinates>\n");
			fprintf(debug_fp, "\t\t</LineString>\n");
			fprintf(debug_fp, "\t</Placemark>\n");
		}

		if (!min_probe || probe->dist < min_dist) {
			min_probe = probe;
			min_dist = probe->dist;
		}
		probe = probe->next;
		i++;
	}

	if (i < 3) {
		fprintf(stderr, "Need at least 3 points\n");
		return -EINVAL;
	}

	/* calculate the number of steps to search for destination point */
	test_steps = 2.0 * 3.1415927 * min_probe->dist / circle_probe;
	rad = 2.0 * 3.1415927 / test_steps;

	if (log_debug) {
		fprintf(debug_fp, "\t<Placemark>\n");
		fprintf(debug_fp, "\t\t<name>Smallest MEAS</name>\n");
		fprintf(debug_fp, "\t\t<visibility>0</visibility>\n");
		fprintf(debug_fp, "\t\t<LineString>\n");
		fprintf(debug_fp, "\t\t\t<tessellate>1</tessellate>\n");
		fprintf(debug_fp, "\t\t\t<coordinates>\n");
	}

	/* search on a circle for the location of the lowest distance
	 * to the radius with the greatest distance */
	min_dist = 42;
	*min_x = *min_y = 42;
	for (i = 0; i < test_steps; i++) {
		x = min_probe->x + min_probe->dist * sin(rad * i);
		y = min_probe->y + min_probe->dist * cos(rad * i);
		if (log_debug)
			fprintf(debug_fp, "%.8f,%.8f\n", debug_long +
				x * debug_x_scale, debug_lat + y);
		/* look for greatest distance */
		dist = 0;
		probe = probe_first;
		while (probe) {
			if (probe != min_probe) {
				/* distance to the radius */
				temp = distonplane(probe->x, probe->y, x, y);
				temp -= probe->dist;
				if (temp < 0)
					temp = -temp;
				if (temp > dist)
					dist = temp;
			}
			probe = probe->next;
		}
		if (i == 0 || dist < min_dist) {
			min_dist = dist;
			*min_x = x;
			*min_y = y;
		}
	}

	if (log_debug) {
		fprintf(debug_fp, "\t\t\t</coordinates>\n");
		fprintf(debug_fp, "\t\t</LineString>\n");
		fprintf(debug_fp, "\t</Placemark>\n");

		fprintf(debug_fp, "\t<Placemark>\n");
		fprintf(debug_fp, "\t\t<name>Finetune</name>\n");
		fprintf(debug_fp, "\t\t<visibility>0</visibility>\n");
		fprintf(debug_fp, "\t\t<LineString>\n");
		fprintf(debug_fp, "\t\t\t<tessellate>1</tessellate>\n");
		fprintf(debug_fp, "\t\t\t<coordinates>\n");
	}

	min_dist = 9999999999.0;
tune_again:
	if (log_debug)
		fprintf(debug_fp, "%.8f,%.8f\n", debug_long +
			*min_x * debug_x_scale, debug_lat + *min_y);

	/* finetune the point */
	rad = 2.0 * 3.1415927 / 6;
	for (i = 0; i < 6; i++) {
		x = *min_x + finetune_radius * sin(rad * i);
		y = *min_y + finetune_radius * cos(rad * i);
		/* search for the point with the lowest sum of distances */
		dist = 0;
		probe = probe_first;
		while (probe) {
			/* distance to the radius */
			temp = distonplane(probe->x, probe->y, x, y);
			temp -= probe->dist;
			if (temp < 0)
				temp = -temp;
			dist += temp;
			probe = probe->next;
		}
		finetune_dist[i] = dist;
		finetune_x[i] = x;
		finetune_y[i] = y;
	}

	optimized = 0;
	for (i = 0; i < 6; i++) {
		if (finetune_dist[i] < min_dist) {
			min_dist = finetune_dist[i];
			*min_x = finetune_x[i];
			*min_y = finetune_y[i];
			optimized = 1;
		}
	}
	if (optimized)
		goto tune_again;

	if (log_debug) {
		fprintf(debug_fp, "\t\t\t</coordinates>\n");
		fprintf(debug_fp, "\t\t</LineString>\n");
		fprintf(debug_fp, "\t</Placemark>\n");
		fprintf(debug_fp, "</Folder>\n");
	}

	return 0;
}
