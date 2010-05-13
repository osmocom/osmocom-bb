#ifndef _OSMOCOM_FILE_H
#define _OSMOCOM_FILE_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
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
static inline int OSMOCOM_UNLINK(const char *name)
{
	char filename[strlen(OSMOCOM_CONFDIR) + strlen(name) + 1];

	strcpy(filename, OSMOCOM_CONFDIR);
	strcat(filename, name);
	
	return unlink(filename);
}
static inline int OSMOCOM_LINK(const char *oldname, const char *newname)
{
	char oldfilename[strlen(OSMOCOM_CONFDIR) + strlen(oldname) + 1];
	char newfilename[strlen(OSMOCOM_CONFDIR) + strlen(newname) + 1];

	strcpy(oldfilename, OSMOCOM_CONFDIR);
	strcat(oldfilename, oldname);
	strcpy(newfilename, OSMOCOM_CONFDIR);
	strcat(newfilename, newname);
	return link(oldfilename, newfilename);
}
static inline int OSMOCOM_MKSTEMP(char *name)
{
	char filename[strlen(OSMOCOM_CONFDIR) + strlen(name) + 1];
	int rc;

	strcpy(filename, OSMOCOM_CONFDIR);
	strcat(filename, name);
	
	rc = mkstemp(filename);

	memcpy(name + strlen(name) - 6, filename + strlen(filename) - 6, 6);

	return rc;
}
static inline int OSMOCOM_CHMOD(const char *name, mode_t mode)
{
	char filename[strlen(OSMOCOM_CONFDIR) + strlen(name) + 1];

	strcpy(filename, OSMOCOM_CONFDIR);
	strcat(filename, name);
	
	return chmod(filename, mode);
}

#endif /* _OSMOCOM_FILE_H */
