#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <sercomm.h>

#include <osmocore/linuxlist.h>
#include <osmocore/select.h>
#include <osmocore/talloc.h>

#include <arpa/inet.h>

//#include "version.h"

#define MODEM_BAUDRATE 	B115200
#define MAX_DNLOAD_SIZE	0xFFFF
#define MAX_HDR_SIZE	128

struct tool_server *tool_server_for_dlci[256];

/**
 * a connection from some other tool
 */
struct tool_connection {
	struct tool_server *server;
	struct llist_head entry;
	struct bsc_fd fd;
};

/**
 * server for a tool
 */
struct tool_server {
	struct bsc_fd bfd;
	uint8_t dlci;
	struct llist_head connections;
};


enum dnload_state {
	WAITING_PROMPT1,
	WAITING_PROMPT2,
	DOWNLOADING,
};

enum dnload_mode {
	MODE_C123,
	MODE_C123xor,
	MODE_C155,
};

struct dnload {
	enum dnload_state state;
	enum dnload_mode mode;
	struct bsc_fd serial_fd;
	char *filename;

	int print_hdlc;

	/* data to be downloaded */
	uint8_t *data;
	int data_len;

	uint8_t *write_ptr;

	struct tool_server layer2_server;
	struct tool_server loader_server;
};


static struct dnload dnload;

static const uint8_t phone_prompt1[] = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x01, 0x40 };
static const uint8_t dnload_cmd[]    = { 0x1b, 0xf6, 0x02, 0x00, 0x52, 0x01, 0x53 };
static const uint8_t phone_prompt2[] = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x02, 0x43 };
static const uint8_t phone_ack[]     = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x03, 0x42 };
static const uint8_t phone_nack_magic[]= { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x03, 0x57 };
static const uint8_t phone_nack[]    = { 0x1b, 0xf6, 0x02, 0x00, 0x45, 0x53, 0x16 };
static const uint8_t ftmtool[] = { "ftmtool" };

/* The C123 has a hard-coded check inside the ramloder that requires the following
 * bytes to be always the first four bytes of the image */
static const uint8_t data_hdr_c123[]    = { 0xee, 0x4c, 0x9f, 0x63 };

/* The C155 doesn't have some strange restriction on what the first four bytes have
 * to be, but it starts the ramloader in THUMB mode.  We use the following four bytes
 * to switch back to ARM mode:
  800100:       4778            bx      pc
  800102:       46c0            nop                     ; (mov r8, r8)
 */
static const uint8_t data_hdr_c155[]    = { 0x78, 0x47, 0xc0, 0x46 };

static const uint8_t dummy_data[]    = { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde };

static int serial_init(const char *serial_dev)
{
    struct termios options;
    int fd, v24;

    fd = open(serial_dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0)
	return fd;

    fcntl(fd, F_SETFL, 0);

    /* Configure serial interface */
    tcgetattr(fd, &options);

    cfsetispeed(&options, MODEM_BAUDRATE);
    cfsetospeed(&options, MODEM_BAUDRATE);

    /* local read */
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    /* hardware flow control off */
    options.c_cflag &= ~CRTSCTS;

    /* software flow control off */
    options.c_iflag &= ~(IXON | IXOFF | IXANY);

    /* we want raw i/o */
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(INLCR | ICRNL | IGNCR);
    options.c_oflag &= ~(ONLCR);

    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;
    options.c_cc[VINTR] = 0;
    options.c_cc[VQUIT] = 0;
    options.c_cc[VSTART] = 0;
    options.c_cc[VSTOP] = 0;
    options.c_cc[VSUSP] = 0;
    
    tcsetattr(fd, TCSANOW, &options);

    /* set ready to read/write */
    v24 = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIS, &v24);

    return fd;
}

/* Read the to-be-downloaded file, prepend header and length, append XOR sum */
int read_file(const char *filename)
{
	int fd, rc, i;
	struct stat st;
	const uint8_t *hdr = NULL;
	int hdr_len = 0;
	uint8_t *file_data;
	uint16_t tot_len;
	uint8_t nibble;
	uint8_t running_xor = 0x02;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("opening file");
		exit(1);
	}

	rc = fstat(fd, &st);
	if (st.st_size > MAX_DNLOAD_SIZE) {
		fprintf(stderr, "The maximum file size is 64kBytes (%u bytes)\n",
			MAX_DNLOAD_SIZE);
		return -EFBIG;
	}

	if (dnload.data) {
		free(dnload.data);
		dnload.data = NULL;
	}

	dnload.data = malloc(MAX_HDR_SIZE + st.st_size);
	if (!dnload.data) {
		close(fd);
		fprintf(stderr, "No memory\n");
		return -ENOMEM;
	}

	/* copy in the header, if any */
	switch (dnload.mode) {
	case MODE_C155:
		hdr = data_hdr_c155;
		hdr_len = sizeof(data_hdr_c155);
		break;
	case MODE_C123:
	case MODE_C123xor:
		hdr = data_hdr_c123;
		hdr_len = sizeof(data_hdr_c123);
		break;
	default:
		break;
	}

	if (hdr && hdr_len)
		memcpy(dnload.data, hdr, hdr_len);

	/* 2 bytes for length + header */
	file_data = dnload.data + 2 + hdr_len;

	/* write the length, keep running XOR */
	tot_len = hdr_len + st.st_size;
	nibble = tot_len >> 8;
	dnload.data[0] = nibble;
	running_xor ^= nibble;
	nibble = tot_len & 0xff;
	dnload.data[1] = nibble;
	running_xor ^= nibble;

	if (hdr_len && hdr) {
		memcpy(dnload.data+2, hdr, hdr_len);

		for (i = 0; i < hdr_len; i++)
			running_xor ^= hdr[i];
	}

	rc = read(fd, file_data, st.st_size);
	if (rc < 0) {
		perror("error reading file\n");
		free(dnload.data);
		dnload.data = NULL;
		close(fd);
		return -EIO;
	}
	if (rc < st.st_size) {
		free(dnload.data);
		dnload.data = NULL;
		close(fd);
		fprintf(stderr, "Short read of file (%d < %d)\n",
			rc, (int)st.st_size);
		return -EIO;
	}

	close(fd);

	dnload.data_len = (file_data+st.st_size) - dnload.data;

	/* calculate XOR sum */
	for (i = 0; i < st.st_size; i++)
		running_xor ^= file_data[i];

	dnload.data[dnload.data_len++] = running_xor;

	/* initialize write pointer to start of data */
	dnload.write_ptr = dnload.data;

	printf("read_file(%s): file_size=%u, hdr_len=%u, dnload_len=%u\n",
		filename, (int)st.st_size, hdr_len, dnload.data_len);

	return 0;
}

static void hexdump(const uint8_t *data, unsigned int len)
{
	const uint8_t *bufptr = data;
	int n;

	for (n=0; n < len; n++, bufptr++)
		printf("%02x ", *bufptr);
	printf("\n");
}

#define WRITE_BLOCK	4096

static int handle_write_dnload(void)
{
	int bytes_left, write_len, rc;

	printf("handle_write(): ");
	if (dnload.write_ptr == dnload.data) {
		/* no bytes have been transferred yet */
		if (dnload.mode == MODE_C155 ||
		    dnload.mode == MODE_C123xor) {
			uint8_t xor_init = 0x02;
			write(dnload.serial_fd.fd, &xor_init, 1);
		} else
			usleep(1);
	} else if (dnload.write_ptr >= dnload.data + dnload.data_len) { 
		printf("finished\n");
		dnload.write_ptr = dnload.data;
		dnload.serial_fd.when &= ~BSC_FD_WRITE;
		return 1;
	}

	/* try to write a maximum of WRITE_BLOCK bytes */
	bytes_left = (dnload.data + dnload.data_len) - dnload.write_ptr;
	write_len = WRITE_BLOCK;
	if (bytes_left < WRITE_BLOCK)
		write_len = bytes_left;

	rc = write(dnload.serial_fd.fd, dnload.write_ptr, write_len);
	if (rc < 0) {
		perror("Error during write");
		return rc;
	}

	dnload.write_ptr += rc;

	printf("%u bytes (%tu/%u)\n", rc, dnload.write_ptr - dnload.data, dnload.data_len);

	return 0;
}

static int handle_write(void)
{
	uint8_t c;

	switch (dnload.state) {
	case DOWNLOADING:
		return handle_write_dnload();
	default:
		if (sercomm_drv_pull(&c) != 0) {
			if (write(dnload.serial_fd.fd, &c, 1) != 1)
				perror("short write");
		} else
			dnload.serial_fd.when &= ~BSC_FD_WRITE;
	}

	return 0;
}

static uint8_t buffer[sizeof(phone_prompt1)];
static uint8_t *bufptr = buffer;

static void hdlc_send_to_phone(uint8_t dlci, uint8_t *data, int len)
{
	struct msgb *msg;
	uint8_t c, *dest;

	printf("hdlc_send_to_phone(dlci=%u): ", dlci);
	hexdump(data, len);

	if (len > 512) {
		fprintf(stderr, "Too much data to send. %u\n", len);
		return;
	}

	/* push the message into the stack */
	msg = sercomm_alloc_msgb(512);
	if (!msg) {
		fprintf(stderr, "Failed to create data for the frame.\n");
		return;
	}

	/* copy the data */
	dest = msgb_put(msg, len);
	memcpy(dest, data, len);

	sercomm_sendmsg(dlci, msg);

	dnload.serial_fd.when |= BSC_FD_WRITE;
}

static void hdlc_console_cb(uint8_t dlci, struct msgb *msg)
{
	write(1, msg->data, msg->len);
	msgb_free(msg);
}

static void hdlc_tool_cb(uint8_t dlci, struct msgb *msg)
{
	struct tool_server *srv = tool_server_for_dlci[dlci];

	if(srv) {
		struct tool_connection *con;
		u_int16_t *len;

		len = (u_int16_t *) msgb_push(msg, 2);
		*len = htons(msg->len - sizeof(*len));

		llist_for_each_entry(con, &srv->connections, entry) {
			if (write(con->fd.fd, msg->data, msg->len) != msg->len) {
				fprintf(stderr, "Failed to write msg to the socket..\n");
				continue;
			}
		}
	}

	msgb_free(msg);
}

static void print_hdlc(uint8_t *buffer, int length)
{
	int i;

	for (i = 0; i < length; ++i)
		if (sercomm_drv_rx_char(buffer[i]) == 0)
			printf("Dropping sample '%c'\n", buffer[i]);
}

static int handle_read(void)
{
	int rc, nbytes, buf_left;

	buf_left = sizeof(buffer) - (bufptr - buffer);
	if (buf_left <= 0) {
		memmove(buffer, buffer+1, sizeof(buffer)-1);
		bufptr -= 1;
		buf_left = 1;
	}

	nbytes = read(dnload.serial_fd.fd, bufptr, buf_left);
	if (nbytes <= 0)
		return nbytes;

	if (!dnload.print_hdlc) {
		printf("got %i bytes from modem, ", nbytes);
		printf("data looks like: ");
		hexdump(bufptr, nbytes);
	} else {
		print_hdlc(bufptr, nbytes);
        }

	if (!memcmp(buffer, phone_prompt1, sizeof(phone_prompt1))) {
		printf("Received PROMPT1 from phone, responding with CMD\n");
		dnload.print_hdlc = 0;
		dnload.state = WAITING_PROMPT2;
		rc = write(dnload.serial_fd.fd, dnload_cmd, sizeof(dnload_cmd));

		/* re-read file */
		rc = read_file(dnload.filename);
		if (rc < 0) {
			fprintf(stderr, "read_file(%s) failed with %d\n", dnload.filename, rc);
			exit(1);
		}
	} else if (!memcmp(buffer, phone_prompt2, sizeof(phone_prompt2))) {
		printf("Received PROMPT2 from phone, starting download\n");
		dnload.serial_fd.when = BSC_FD_READ | BSC_FD_WRITE;
		dnload.state = DOWNLOADING;
	} else if (!memcmp(buffer, phone_ack, sizeof(phone_ack))) {
		printf("Received DOWNLOAD ACK from phone, your code is running now!\n");
		dnload.serial_fd.when = BSC_FD_READ;
		dnload.state = WAITING_PROMPT1;
		dnload.write_ptr = dnload.data;
		dnload.print_hdlc = 1;
	} else if (!memcmp(buffer, phone_nack, sizeof(phone_nack))) {
		printf("Received DOWNLOAD NACK from phone, something went wrong :(\n");
		dnload.serial_fd.when = BSC_FD_READ;
		dnload.state = WAITING_PROMPT1;
		dnload.write_ptr = dnload.data;
	} else if (!memcmp(buffer, phone_nack_magic, sizeof(phone_nack_magic))) {
		printf("Received MAGIC NACK from phone, you need to have \"1003\" at 0x803ce0\n");
		dnload.serial_fd.when = BSC_FD_READ;
		dnload.state = WAITING_PROMPT1;
		dnload.write_ptr = dnload.data;
	} else if (!memcmp(buffer, ftmtool, sizeof(ftmtool))) {
		printf("Received FTMTOOL from phone, ramloader has aborted\n");
		dnload.serial_fd.when = BSC_FD_READ;
		dnload.state = WAITING_PROMPT1;
		dnload.write_ptr = dnload.data;
	}
	bufptr += nbytes;

	return nbytes;
}

static int serial_read(struct bsc_fd *fd, unsigned int flags)
{
	int rc;

	if (flags & BSC_FD_READ) {
		rc = handle_read();
		if (rc == 0)
			exit(2);
	}

	if (flags & BSC_FD_WRITE) {
		rc = handle_write();
		if (rc == 1)
			dnload.state = WAITING_PROMPT1;
	}
	return 0;
}

static int parse_mode(const char *arg)
{
	if (!strcasecmp(arg, "c123") ||
	    !strcasecmp(arg, "c140"))
		return MODE_C123;
	else if (!strcasecmp(arg, "c123xor"))
		return MODE_C123xor;
	else if (!strcasecmp(arg, "c155"))
		return MODE_C155;

	return -1;
}


static int usage(const char *name)
{
	printf("\nUsage: %s [ -v | -h ] [ -p /dev/ttyXXXX ] [ -s /tmp/osmocom_l2 ] [ -l /tmp/osmocom_loader ] [ -m {c123,c123xor,c155} ] file.bin\n", name);
	printf("\t* Open serial port /dev/ttyXXXX (connected to your phone)\n"
		"\t* Perform handshaking with the ramloader in the phone\n"
		"\t* Download file.bin to the attached phone (base address 0x00800100)\n");
	exit(2);
}

static int version(const char *name)
{
	//printf("\n%s version %s\n", name, VERSION);
	exit(2);
}

static int un_tool_read(struct bsc_fd *fd, unsigned int flags)
{
	int rc, c;
	u_int16_t length = 0xffff;
	u_int8_t buf[4096];
	struct tool_connection *con = (struct tool_connection *)fd->data;

	c = 0;
	while(c < 2) {
		rc = read(fd->fd, &buf + c, 2 - c);
		if(rc == 0) {
			// disconnect
			goto close;
		}
		if(rc < 0) {
			if(errno == EAGAIN) {
				continue;
			}
			fprintf(stderr, "Err from socket: %s\n", strerror(errno));
			goto close;
		}
		c += rc;
	}

	length = ntohs(*(u_int16_t*)buf);

	c = 0;
	while(c < length) {
		rc = read(fd->fd, &buf + c, length - c);
		if(rc == 0) {
			// disconnect
			goto close;
		}
		if(rc < 0) {
			if(errno == EAGAIN) {
				continue;
			}
			fprintf(stderr, "Err from socket: %s\n", strerror(errno));
			goto close;
		}
		c += rc;
	}

	hdlc_send_to_phone(con->server->dlci, buf, length);

	return 0;
close:

	close(fd->fd);
	bsc_unregister_fd(fd);
	llist_del(&con->entry);
	talloc_free(con);
	return -1;
}

/* accept a new connection */
static int tool_accept(struct bsc_fd *fd, unsigned int flags)
{
	struct tool_server *srv = (struct tool_server *)fd->data;
	struct tool_connection *con;
	struct sockaddr_un un_addr;
	socklen_t len;
	int rc;

	len = sizeof(un_addr);
	rc = accept(fd->fd, (struct sockaddr *) &un_addr, &len);
	if (rc < 0) {
		fprintf(stderr, "Failed to accept a new connection.\n");
		return -1;
	}

	con = talloc_zero(NULL, struct tool_connection);
	if (!con) {
		fprintf(stderr, "Failed to create tool connection.\n");
		return -1;
	}

	con->server = srv;

	con->fd.fd = rc;
	con->fd.when = BSC_FD_READ;
	con->fd.cb = un_tool_read;
	con->fd.data = con;
	if (bsc_register_fd(&con->fd) != 0) {
		fprintf(stderr, "Failed to register the fd.\n");
		return -1;
	}

	llist_add(&con->entry, &srv->connections);
	return 0;
}

/*
 * Register and start a tool server
 */
static int register_tool_server(struct tool_server *ts,
								const char *path,
								uint8_t dlci)
{
	struct bsc_fd *bfd = &ts->bfd;
	struct sockaddr_un local;
	unsigned int namelen;
	int rc;

	bfd->fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (bfd->fd < 0) {
		fprintf(stderr, "Failed to create Unix Domain Socket.\n");
		return -1;
	}

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path) - 1] = '\0';
	unlink(local.sun_path);

	/* we use the same magic that X11 uses in Xtranssock.c for
	 * calculating the proper length of the sockaddr */
#if defined(BSD44SOCKETS) || defined(__UNIXWARE__)
	local.sun_len = strlen(local.sun_path);
#endif
#if defined(BSD44SOCKETS) || defined(SUN_LEN)
	namelen = SUN_LEN(&local);
#else
	namelen = strlen(local.sun_path) +
		  offsetof(struct sockaddr_un, sun_path);
#endif

	rc = bind(bfd->fd, (struct sockaddr *) &local, namelen);
	if (rc != 0) {
		fprintf(stderr, "Failed to bind the unix domain socket. '%s'\n",
			local.sun_path);
		return -1;
	}

	if (listen(bfd->fd, 0) != 0) {
		fprintf(stderr, "Failed to listen.\n");
		return -1;
	}

	bfd->when = BSC_FD_READ;
	bfd->cb = tool_accept;
	bfd->data = ts;

	ts->dlci = dlci;
	INIT_LLIST_HEAD(&ts->connections);

	tool_server_for_dlci[dlci] = ts;

	sercomm_register_rx_cb(dlci, hdlc_tool_cb);

	if (bsc_register_fd(bfd) != 0) {
		fprintf(stderr, "Failed to register the bfd.\n");
		return -1;
	}

	return 0;
}

extern void hdlc_tpudbg_cb(uint8_t dlci, struct msgb *msg);

int main(int argc, char **argv)
{
	int opt, flags;
	const char *serial_dev = "/dev/ttyUSB1";
	const char *layer2_un_path = "/tmp/osmocom_l2";
	const char *loader_un_path = "/tmp/osmocom_loader";

	dnload.mode = MODE_C123;

	while ((opt = getopt(argc, argv, "hl:p:m:s:v")) != -1) {
		switch (opt) {
		case 'p':
			serial_dev = optarg;
			break;
		case 'm':
			dnload.mode = parse_mode(optarg);
			if (dnload.mode < 0)
				usage(argv[0]);
			break;
		case 's':
			layer2_un_path = optarg;
			break;
		case 'l':
			loader_un_path = optarg;
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

	if (argc <= optind) {
		fprintf(stderr, "You have to specify the filename\n");
		usage(argv[0]);
	}

	dnload.filename = argv[optind];

	dnload.serial_fd.fd = serial_init(serial_dev);
	if (dnload.serial_fd.fd < 0) {
		fprintf(stderr, "Cannot open serial device %s\n", serial_dev);
		exit(1);
	}

	if (bsc_register_fd(&dnload.serial_fd) != 0) {
		fprintf(stderr, "Failed to register the serial.\n");
		exit(1);
	}

	/* Set serial socket to non-blocking mode of operation */
	flags = fcntl(dnload.serial_fd.fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(dnload.serial_fd.fd, F_SETFL, flags);

	dnload.serial_fd.when = BSC_FD_READ;
	dnload.serial_fd.cb = serial_read;

	/* initialize the HDLC layer */
	sercomm_init();
	sercomm_register_rx_cb(SC_DLCI_CONSOLE, hdlc_console_cb);
	sercomm_register_rx_cb(SC_DLCI_DEBUG, hdlc_tpudbg_cb);

	/* unix domain socket handling */
	if (register_tool_server(&dnload.layer2_server, layer2_un_path, SC_DLCI_L1A_L23) != 0) {
		exit(1);
	}
	if (register_tool_server(&dnload.loader_server, loader_un_path, SC_DLCI_LOADER) != 0) {
		exit(1);
	}

	while (1)
		bsc_select_main(0);

	close(dnload.serial_fd.fd);

	exit(0);
}
