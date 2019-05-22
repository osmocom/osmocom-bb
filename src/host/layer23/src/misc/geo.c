#include <math.h>
#include "geo.h"

void geo2space(double *x, double *y, double *z, double lon, double lat)
{
	*z = sin(lat / 180.0 * PI) * POLE_RADIUS;
	*x = sin(lon / 180.0 * PI) * cos(lat / 180.0 * PI) * EQUATOR_RADIUS;
	*y = -cos(lon / 180.0 * PI) * cos(lat / 180.0 * PI) * EQUATOR_RADIUS;
}

void space2geo(double *lon, double *lat, double x, double y, double z)
{
	double r;

	/* bring geoid to 1m radius */
	z = z / POLE_RADIUS;
	x = x / EQUATOR_RADIUS;
	y = y / EQUATOR_RADIUS;

	/* normalize */
	r = sqrt(x * x + y * y + z * z);
	z = z / r;
	x = x / r;
	y = y / r;

	*lat = asin(z) / PI * 180;
	*lon = atan2(x, -y) / PI * 180;
}

double distinspace(double x1, double y1, double z1, double x2, double y2,
	double z2)
{
	double x = x1 - x2;
	double y = y1 - y2;
	double z = z1 - z2;

	return sqrt(x * x + y * y + z * z);
}

double distonplane(double x1, double y1, double x2, double y2)
{
	double x = x1 - x2;
	double y = y1 - y2;

	return sqrt(x * x + y * y);
}

