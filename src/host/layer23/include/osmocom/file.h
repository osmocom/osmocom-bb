#ifndef _OSMOCOM_FILE_H
#define _OSMOCOM_FILE_H

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#define OSMOCOM_CONFDIR "/etc/osmocom/"
#define OSMOCOM_PERM 0644
#define OSMOCOM_FILE FILE
static inline OSMOCOM_FILE *osmocom_fopen(const char *name, const char *mode)
{
	char filename[strlen(OSMOCOM_CONFDIR) + strlen(name) + 1];

	if (mode[0] != 'r') {
		int rc;

		rc = mkdir(OSMOCOM_CONFDIR, OSMOCOM_PERM);
		if (rc < 1 && errno != EEXIST)
			return NULL;
	}

	strcpy(filename, OSMOCOM_CONFDIR);
	strcat(filename, name);
	
	return fopen(filename, mode);
}
#define osmocom_fread fread
#define osmocom_fgets fgets
#define osmocom_fwrite fwrite
#define osmocom_feof feof
#define osmocom_fclose fclose

#endif /* _OSMOCOM_FILE_H */
