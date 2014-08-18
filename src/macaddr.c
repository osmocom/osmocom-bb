#include <stdint.h>
#include <string.h>
#include <stdlib.h>


int osmo_macaddr_parse(uint8_t *out, const char *in)
{
	/* 00:00:00:00:00:00 */
	char tmp[18];
	char *tok;
	unsigned int i = 0;

	if (strlen(in) < 17)
		return -1;

	strncpy(tmp, in, sizeof(tmp)-1);
	tmp[sizeof(tmp)-1] = '\0';

	for (tok = strtok(tmp, ":"); tok && (i < 6); tok = strtok(NULL, ":")) {
		unsigned long ul = strtoul(tok, NULL, 16);
		out[i++] = ul & 0xff;
	}

	return 0;
}

#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>

int osmo_get_macaddr(uint8_t *mac_out, const char *dev_name)
{
	int fd, rc;
	struct ifreq ifr;

	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0)
		return fd;

	memset(&ifr, 0, sizeof(ifr));
	memcpy(&ifr.ifr_name, dev_name, sizeof(ifr.ifr_name));
	rc = ioctl(fd, SIOCGIFHWADDR, &ifr);
	close(fd);

	if (rc < 0)
		return rc;

	memcpy(mac_out, ifr.ifr_hwaddr.sa_data, 6);

	return 0;
}
