/* WGS 84 */
#define EQUATOR_RADIUS	6378137.0
#define POLE_RADIUS	6356752.314

#define PI		3.1415926536

void geo2space(double *x, double *y, double *z, double lat, double lon);
void space2geo(double *lat, double *lon, double x, double y, double z);
double distinspace(double x1, double y1, double z1, double x2, double y2,
	double z2);
double distonplane(double x1, double y1, double x2, double y2);

