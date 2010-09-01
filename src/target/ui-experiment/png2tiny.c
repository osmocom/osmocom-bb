
#include <stdlib.h>

#include <SDL_image.h>

enum {
	FORMAT_NONE,
	FORMAT_C
};

void
version(const char *name) {
	puts(name);
	//printf("%s rev %s\n", name, REVISION);
	exit(2);
}

void
usage(const char *name) {
	printf("Usage: %s [-hv] [-f outfmt] [-s outsym] <infile> <outfile>\n");
	exit(2);
}

int
main(int argc, char **argv) {
	int opt, outfmt;
	const char *outsym = NULL;
	SDL_Surface *img;

	while((opt = getopt(argc, argv, "f:s:hv")) != -1) {
		switch(opt) {
		case 'f':
			if(!strcmp(optarg, "c")) {
				outfmt = FORMAT_C;
			}
			break;
		case 's':
			outsym = optarg;
			break;
		case 'v':
			version(argv[0]);
			break;
		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	return 0;
}
