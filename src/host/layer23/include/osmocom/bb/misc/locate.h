
struct probe {
	struct probe *next;
	double x, y, dist;
};

int locate_cell(struct probe *probe_first, double *min_x, double *min_y);

