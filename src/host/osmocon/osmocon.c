/* osmocon */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2010 by Steve Markgraf <steve@steve-m.de>
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

#include <ctype.h>
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

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>
#include <osmocom/core/serial.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>

#include <arpa/inet.h>

#define MODEM_BAUDRATE		B115200
#define MAX_DNLOAD_SIZE		0xFFFF
#define MAX_HDR_SIZE		128
#define MAGIC_OFFSET		0x3be2

#define DEFAULT_BEACON_INTERVAL	50000
#define ROMLOAD_INIT_BAUDRATE	B19200
#define ROMLOAD_DL_BAUDRATE	B115200
#define ROMLOAD_BLOCK_HDR_LEN	10
#define ROMLOAD_ADDRESS		0x820000

#define MTK_INIT_BAUDRATE	B19200
#define MTK_ADDRESS		0x40001400
#define MTK_BLOCK_SIZE		1024

struct tool_server *tool_server_for_dlci[256];

/**
 * a connection from some other tool
 */
struct tool_connection {
	struct tool_server *server;
	struct llist_head entry;
	struct osmo_fd fd;
};

/**
 * server for a tool
 */
struct tool_server {
	struct osmo_fd bfd;
	uint8_t dlci;
	struct llist_head connections;
};


enum dnload_state {
	WAITING_PROMPT1,
	WAITING_PROMPT2,
	DOWNLOADING,
};

enum romload_state {
	WAITING_IDENTIFICATION,
	WAITING_PARAM_ACK,
	SENDING_BLOCKS,
	SENDING_LAST_BLOCK,
	LAST_BLOCK_SENT,
	WAITING_BLOCK_ACK,
	WAITING_CHECKSUM_ACK,
	WAITING_BRANCH_ACK,
	FINISHED,
};

enum mtk_state {
	MTK_INIT_1,
	MTK_INIT_2,
	MTK_INIT_3,
	MTK_INIT_4,
	MTK_WAIT_WRITE_ACK,
	MTK_WAIT_ADDR_ACK,
	MTK_WAIT_SIZE_ACK,
	MTK_SENDING_BLOCKS,
	MTK_WAIT_BRANCH_CMD_ACK,
	MTK_WAIT_BRANCH_ADDR_ACK,
	MTK_FINISHED,
};

enum dnload_mode {
	MODE_C123,
	MODE_C123xor,
	MODE_C140,
	MODE_C140xor,
	MODE_C155,
	MODE_ROMLOAD,
	MODE_MTK,
	MODE_INVALID,
};

struct dnload {
	enum dnload_state state;
	enum romload_state romload_state;
	enum mtk_state mtk_state;
	enum dnload_mode mode, previous_mode;
	struct osmo_fd serial_fd;
	char *filename;

	int expect_hdlc;
	int do_chainload;

	int dump_rx;
	int dump_tx;
	int beacon_interval;

	/* data to be downloaded */
	uint8_t *data;
	int data_len;

	uint8_t *write_ptr;

	/* romload: block to be downloaded */
	uint8_t *block;
	int block_len;
	uint8_t block_number;
	uint16_t block_payload_size;
	int romload_dl_checksum;
	uint8_t *block_ptr;
	uint8_t load_address[4];

	uint8_t mtk_send_size[4];
	int block_count;
	int echo_bytecount;

	struct tool_server layer2_server;
	struct tool_server loader_server;
};


static struct dnload dnload;
static struct osmo_timer_list tick_timer;

/* Compal ramloader specific */
static const uint8_t phone_prompt1[] = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x01, 0x40 };
static const uint8_t dnload_cmd[]    = { 0x1b, 0xf6, 0x02, 0x00, 0x52, 0x01, 0x53 };
static const uint8_t phone_prompt2[] = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x02, 0x43 };
static const uint8_t phone_ack[]     = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x03, 0x42 };
static const uint8_t phone_nack_magic[]= { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x03, 0x57 };
static const uint8_t phone_nack[]    = { 0x1b, 0xf6, 0x02, 0x00, 0x45, 0x53, 0x16 };
static const uint8_t ftmtool[] = { 0x66, 0x74, 0x6d, 0x74, 0x6f, 0x6f, 0x6c };
static const uint8_t phone_magic[] = { 0x31, 0x30, 0x30, 0x33 }; /* "1003" */

/* The C123 has a hard-coded check inside the ramloader that requires the
 * following bytes to be always the first four bytes of the image */
static const uint8_t data_hdr_c123[]    = { 0xee, 0x4c, 0x9f, 0x63 };

/* The C155 doesn't have some strange restriction on what the first four bytes
 *  have to be, but it starts the ramloader in THUMB mode. We use the following
 * four bytes to switch back to ARM mode:
  800100:       4778            bx      pc
  800102:       46c0            nop                     ; (mov r8, r8)
 */
static const uint8_t data_hdr_c155[]    = { 0x78, 0x47, 0xc0, 0x46 };

/* small loader that enables the bootrom and executes the TI romloader:
 * _start:
 *	ldr	r1, =0x000a0000
 * wait:
 *	subs	r1, r1, #1
 *	bne	wait
 *	ldr	r1, =0xfffffb10
 *	ldr	r2, =0x100
 *	strh	r2, [r1]
 *	ldr	pc, =0x0
 */
static const uint8_t chainloader[] = {
	0x0a, 0x18, 0xa0, 0xe3, 0x01, 0x10, 0x51, 0xe2, 0xfd, 0xff, 0xff,
	0x1a, 0x08, 0x10, 0x9f, 0xe5, 0x01, 0x2c, 0xa0, 0xe3, 0xb0, 0x20,
	0xc1, 0xe1, 0x00, 0xf0, 0xa0, 0xe3, 0x10, 0xfb, 0xff, 0xff,
};

/* Calypso romloader specific */
static const uint8_t romload_ident_cmd[] =	{ 0x3c, 0x69 };	/* <i */
static const uint8_t romload_abort_cmd[] =	{ 0x3c, 0x61 };	/* <a */
static const uint8_t romload_write_cmd[] =	{ 0x3c, 0x77 };	/* <w */
static const uint8_t romload_checksum_cmd[] =	{ 0x3c, 0x63 };	/* <c */
static const uint8_t romload_branch_cmd[] =	{ 0x3c, 0x62 };	/* <b */
static const uint8_t romload_ident_ack[] =	{ 0x3e, 0x69 };	/* >i */
static const uint8_t romload_param_ack[] =	{ 0x3e, 0x70 };	/* >p */
static const uint8_t romload_param_nack[] =	{ 0x3e, 0x50 };	/* >P */
static const uint8_t romload_block_ack[] =	{ 0x3e, 0x77 };	/* >w */
static const uint8_t romload_block_nack[] =	{ 0x3e, 0x57 };	/* >W */
static const uint8_t romload_checksum_ack[] =	{ 0x3e, 0x63 };	/* >c */
static const uint8_t romload_checksum_nack[] =	{ 0x3e, 0x43 };	/* >C */
static const uint8_t romload_branch_ack[] =	{ 0x3e, 0x62 };	/* >b */
static const uint8_t romload_branch_nack[] =	{ 0x3e, 0x42 };	/* >B */

/* romload_param: {"<p", uint8_t baudrate, uint8_t dpll, uint16_t memory_config,
 * uint8_t strobe_af, uint32_t uart_timeout} */

static const uint8_t romload_param[] = { 0x3c, 0x70, 0x00, 0x00, 0x00, 0x04,
					 0x00, 0x00, 0x00, 0x00, 0x00 };

/* MTK romloader specific */
static const uint8_t mtk_init_cmd[] =	{ 0xa0, 0x0a, 0x50, 0x05 };
static const uint8_t mtk_init_resp[] =	{ 0x5f, 0xf5, 0xaf, 0xfa };
static const uint8_t mtk_command[] =	{ 0xa1, 0xa2, 0xa4, 0xa8 };

static void beacon_timer_cb(void *p)
{
	int rc;

	if (dnload.romload_state == WAITING_IDENTIFICATION) {
		rc = write(dnload.serial_fd.fd, romload_ident_cmd,
			   sizeof(romload_ident_cmd));

		if (!(rc == sizeof(romload_ident_cmd)))
			printf("Error sending identification beacon\n");

		osmo_timer_schedule(p, 0, dnload.beacon_interval);
	}
}

static void mtk_timer_cb(void *p)
{
	int rc;

	if (dnload.mtk_state == MTK_INIT_1) {
		printf("Sending MTK romloader beacon...\n");
		rc = write(dnload.serial_fd.fd, &mtk_init_cmd[0], 1);

		if (!(rc == 1))
			printf("Error sending identification beacon\n");

		osmo_timer_schedule(p, 0, dnload.beacon_interval);
	}
}

/* Read the to-be-downloaded file, prepend header and length, append XOR sum */
int read_file(const char *filename, int chainload)
{
	int fd, rc, i;
	struct stat st;
	const uint8_t *hdr = NULL;
	int payload_size;
	int hdr_len = 0;
	uint8_t *file_data;
	uint16_t tot_len;
	uint8_t nibble;
	uint8_t running_xor = 0x02;

	if (!chainload) {
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			perror("opening file");
			exit(1);
		}

		rc = fstat(fd, &st);
		if ((st.st_size > MAX_DNLOAD_SIZE) && (dnload.mode != MODE_ROMLOAD)) {
			fprintf(stderr, "The maximum file size is 64kBytes (%u bytes)\n",
				MAX_DNLOAD_SIZE);
			return -EFBIG;
		}
	} else {
		st.st_size = sizeof(chainloader);
	}

	free(dnload.data);
	dnload.data = NULL;

	if (dnload.mode == MODE_C140 || dnload.mode == MODE_C140xor) {
		if (st.st_size < (MAGIC_OFFSET + sizeof(phone_magic)))
			payload_size = MAGIC_OFFSET + sizeof(phone_magic);
		else {
			printf("\nThe filesize is larger than 15kb, code on "
				"the magic address will be overwritten!\nUse "
				"loader.bin and upload the application with "
				"osmoload instead!\n\n");
			payload_size = st.st_size;
		}
	} else
		payload_size = st.st_size;

	dnload.data = malloc(MAX_HDR_SIZE + payload_size);

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
	case MODE_C140:
	case MODE_C140xor:
	case MODE_C123:
	case MODE_C123xor:
		hdr = data_hdr_c123;
		hdr_len = sizeof(data_hdr_c123);
		break;
	case MODE_ROMLOAD:
		break;
	default:
		break;
	}

	if (hdr && hdr_len)
		memcpy(dnload.data, hdr, hdr_len);

	/* 2 bytes for length + header */
	file_data = dnload.data + 2 + hdr_len;

	/* write the length, keep running XOR */
	tot_len = hdr_len + payload_size;
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

	if (!chainload) {
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
	} else {
		memcpy(file_data, chainloader, st.st_size);
	}

	dnload.data_len = (file_data+payload_size) - dnload.data;

	/* fill memory between data end and magic, add magic */
	if(dnload.mode == MODE_C140 || dnload.mode == MODE_C140xor) {
		if (st.st_size < MAGIC_OFFSET)
			memset(file_data + st.st_size, 0x00,
				payload_size - st.st_size);
		memcpy(dnload.data + MAGIC_OFFSET, phone_magic,
			sizeof(phone_magic));
	}

	/* calculate XOR sum */
	for (i = 0; i < payload_size; i++)
		running_xor ^= file_data[i];

	dnload.data[dnload.data_len++] = running_xor;

	/* initialize write pointer to start of data */
	dnload.write_ptr = dnload.data;

	printf("read_file(%s): file_size=%u, hdr_len=%u, dnload_len=%u\n",
		chainload ? "chainloader" : filename, (int)st.st_size,
		hdr_len, dnload.data_len);

	return 0;
}

static void osmocon_osmo_hexdump(const uint8_t *data, unsigned int len)
{
	int n;

	for (n=0; n < len; n++)
		printf("%02x ", data[n]);
	printf(" ");
	for (n=0; n < len; n++)
		if (isprint(data[n]))
			putchar(data[n]);
		else
			putchar('.');
	printf("\n");
}

static int romload_prepare_block(void)
{
	int i;

	int block_checksum = 5;
	int remaining_bytes;
	int fill_bytes;
	uint8_t *block_data;
	uint32_t block_address;

	dnload.block_len = ROMLOAD_BLOCK_HDR_LEN + dnload.block_payload_size;

	/* if first block, allocate memory */
	if (!dnload.block_number) {
		dnload.block = malloc(dnload.block_len);
		if (!dnload.block) {
			fprintf(stderr, "No memory\n");
			return -ENOMEM;
		}
		dnload.romload_dl_checksum = 0;
		/* initialize write pointer to start of data */
		dnload.write_ptr = dnload.data;
	}

	block_address = ROMLOAD_ADDRESS +
			(dnload.block_number * dnload.block_payload_size);

	/* prepare our block header (10 bytes) */
	memcpy(dnload.block, romload_write_cmd, sizeof(romload_write_cmd));
	dnload.block[2] = 0x01; /* block index */
	/* should normally be the block number, but hangs when sending !0x01 */
	dnload.block[3] = 0x01;	/* dnload.block_number+1 */
	dnload.block[4] = (dnload.block_payload_size >> 8) & 0xff;
	dnload.block[5] = dnload.block_payload_size & 0xff;
	dnload.block[6] = (block_address >> 24) & 0xff;
	dnload.block[7] = (block_address >> 16) & 0xff;
	dnload.block[8] = (block_address >> 8) & 0xff;
	dnload.block[9] = block_address & 0xff;

	block_data = dnload.block + ROMLOAD_BLOCK_HDR_LEN;
	dnload.write_ptr = dnload.data + 2 +
			(dnload.block_payload_size * dnload.block_number);

	remaining_bytes = dnload.data_len - 3 -
			(dnload.block_payload_size * dnload.block_number);

	memcpy(block_data, dnload.write_ptr, dnload.block_payload_size);

	if (remaining_bytes <= dnload.block_payload_size) {
		fill_bytes = (dnload.block_payload_size - remaining_bytes);
		memset(block_data + remaining_bytes, 0x00, fill_bytes);
		dnload.romload_state = SENDING_LAST_BLOCK;
	} else {
		dnload.romload_state = SENDING_BLOCKS;
	}

	/* block checksum is lsb of ~(5 + block_size_lsb +  all bytes of
	 * block_address + all data bytes) */
	for (i = 5; i < ROMLOAD_BLOCK_HDR_LEN + dnload.block_payload_size; i++)
		block_checksum += dnload.block[i];

	/* checksum is lsb of ~(sum of LSBs of all block checksums) */
	dnload.romload_dl_checksum += ~(block_checksum) & 0xff;

	/* initialize block pointer to start of block */
	dnload.block_ptr = dnload.block;

	dnload.block_number++;
	dnload.serial_fd.when = BSC_FD_READ | BSC_FD_WRITE;
	return 0;
}

static int mtk_prepare_block(void)
{
	int rc, i;

	int remaining_bytes;
	int fill_bytes;
	uint8_t *block_data;
	uint8_t tmp_byteswap;
	uint32_t tmp_size;

	dnload.block_len = MTK_BLOCK_SIZE;
	dnload.echo_bytecount = 0;

	/* if first block, allocate memory */
	if (!dnload.block_number) {
		dnload.block = malloc(dnload.block_len);
		if (!dnload.block) {
			fprintf(stderr, "No memory\n");
			return -ENOMEM;
		}

		/* calculate the number of blocks we need to send */
		dnload.block_count = (dnload.data_len-3) / MTK_BLOCK_SIZE;
		/* add one more block if no multiple of blocksize */
		if((dnload.data_len-3) % MTK_BLOCK_SIZE)
			dnload.block_count++;

		/* divide by 2, since we have to tell the mtk loader the size
		 * as count of uint16 (odd transfer sizes are not possible) */
		tmp_size = (dnload.block_count * MTK_BLOCK_SIZE)/2;
		dnload.mtk_send_size[0] = (tmp_size >> 24) & 0xff;
		dnload.mtk_send_size[1] = (tmp_size >> 16) & 0xff;
		dnload.mtk_send_size[2] = (tmp_size >> 8) & 0xff;
		dnload.mtk_send_size[3] = tmp_size & 0xff;

		/* initialize write pointer to start of data */
		dnload.write_ptr = dnload.data;
	}

	block_data = dnload.block;
	dnload.write_ptr = dnload.data + 2 +
			(dnload.block_len * dnload.block_number);

	remaining_bytes = dnload.data_len - 3 -
			(dnload.block_len * dnload.block_number);

	memcpy(block_data, dnload.write_ptr, MTK_BLOCK_SIZE);

	if (remaining_bytes <= MTK_BLOCK_SIZE) {
		fill_bytes = (MTK_BLOCK_SIZE - remaining_bytes);
		printf("Preparing the last block, filling %i bytes\n",
			fill_bytes);
		memset(block_data + remaining_bytes, 0x00, fill_bytes);
		dnload.romload_state = SENDING_LAST_BLOCK;
	} else {
		dnload.romload_state = SENDING_BLOCKS;
		printf("Preparing block %i\n", dnload.block_number+1);
	}

	/* for the mtk romloader we need to swap MSB <-> LSB */
	for (i = 0; i < dnload.block_len; i += 2) {
		tmp_byteswap = dnload.block[i];
		dnload.block[i] = dnload.block[i+1];
		dnload.block[i+1] = tmp_byteswap;
	}

	/* initialize block pointer to start of block */
	dnload.block_ptr = dnload.block;

	dnload.block_number++;
	return rc;
}

static int handle_write_block(void)
{
	int bytes_left, write_len, rc;
	int progress = 100 * (dnload.block_number * dnload.block_payload_size)
		       / dnload.data_len;

	if (dnload.block_ptr >= dnload.block + dnload.block_len) {
		printf("Progress: %i%%\r", progress);
		fflush(stdout);
		dnload.write_ptr = dnload.data;
		dnload.serial_fd.when &= ~BSC_FD_WRITE;
		if (dnload.romload_state == SENDING_LAST_BLOCK) {
			dnload.romload_state = LAST_BLOCK_SENT;
			printf("Finished, sent %i blocks in total\n",
				dnload.block_number);
		} else {
			dnload.romload_state = WAITING_BLOCK_ACK;
		}

		return 0;
	}

	/* try to write a maximum of block_len bytes */
	bytes_left = (dnload.block + dnload.block_len) - dnload.block_ptr;
	write_len = dnload.block_len;
	if (bytes_left < dnload.block_len)
		write_len = bytes_left;

	rc = write(dnload.serial_fd.fd, dnload.block_ptr, write_len);
	if (rc < 0) {
		perror("Error during write");
		return rc;
	}

	dnload.block_ptr += rc;

	return 0;
}

#define WRITE_BLOCK	4096

static int handle_write_dnload(void)
{
	int bytes_left, write_len, rc;
	uint8_t xor_init = 0x02;

	printf("handle_write(): ");
	if (dnload.write_ptr == dnload.data) {
		/* no bytes have been transferred yet */
		switch (dnload.mode) {
		case MODE_C155:
		case MODE_C140xor:
		case MODE_C123xor:
			rc = write(dnload.serial_fd.fd, &xor_init, 1);
			break;
		default:
			break;
		}
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

	printf("%u bytes (%u/%u)\n", rc, dnload.write_ptr - dnload.data,
		dnload.data_len);

	return 0;
}

static int handle_sercomm_write(void)
{
	uint8_t buffer[256];
	int i, count = 0, end = 0;

	for (i = 0; i < sizeof(buffer); i++) {
		if (sercomm_drv_pull(&buffer[i]) == 0) {
			end = 1;
			break;
		}
		count++;
	}

	if (count) {
		if (write(dnload.serial_fd.fd, buffer, count) != count)
			perror("short write");
	}

	if (end)
		dnload.serial_fd.when &= ~BSC_FD_WRITE;

	return 0;
}

static int handle_write(void)
{
	/* TODO: simplify this again (global state: downloading, sercomm) */
	switch (dnload.mode) {
	case MODE_ROMLOAD:
		switch (dnload.romload_state) {
		case SENDING_BLOCKS:
		case SENDING_LAST_BLOCK:
			return handle_write_block();
		default:
			return handle_sercomm_write();
		}
		break;
	case MODE_MTK:
		switch (dnload.mtk_state) {
		case MTK_SENDING_BLOCKS:
			return handle_write_block();
		default:
			return handle_sercomm_write();
		}
		break;
	default:
		switch (dnload.state) {
		case DOWNLOADING:
			return handle_write_dnload();
		default:
			return handle_sercomm_write();
		}
	}

	return 0;
}

static uint8_t buffer[sizeof(phone_prompt1)];
static uint8_t *bufptr = buffer;

static void hdlc_send_to_phone(uint8_t dlci, uint8_t *data, int len)
{
	struct msgb *msg;
	uint8_t *dest;

	if(dnload.dump_tx) {
		printf("hdlc_send(dlci=%u): ", dlci);
		osmocon_osmo_hexdump(data, len);
	}

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

	if(dnload.dump_rx) {
		printf("hdlc_recv(dlci=%u): ", dlci);
		osmocon_osmo_hexdump(msg->data, msg->len);
	}

	if(srv) {
		struct tool_connection *con;
		uint16_t *len;

		len = (uint16_t *) msgb_push(msg, 2);
		*len = htons(msg->len - sizeof(*len));

		llist_for_each_entry(con, &srv->connections, entry) {
			if (write(con->fd.fd, msg->data, msg->len) != msg->len) {
				fprintf(stderr,
					"Failed to write msg to the socket..\n");
				continue;
			}
		}
	}

	msgb_free(msg);
}

static int handle_buffer(int buf_used_len)
{
	int nbytes, buf_left, i;

	buf_left = buf_used_len - (bufptr - buffer);
	if (buf_left <= 0) {
		memmove(buffer, buffer+1, buf_used_len-1);
		bufptr -= 1;
		buf_left = 1;
	}

	nbytes = read(dnload.serial_fd.fd, bufptr, buf_left);
	if (nbytes <= 0)
		return nbytes;

	if (!dnload.expect_hdlc) {
		printf("got %i bytes from modem, ", nbytes);
		printf("data looks like: ");
		osmocon_osmo_hexdump(bufptr, nbytes);
	} else {
		for (i = 0; i < nbytes; ++i)
			if (sercomm_drv_rx_char(bufptr[i]) == 0)
				printf("Dropping sample '%c'\n", bufptr[i]);
	}

	return nbytes;
}

/* Compal ramloader */
static int handle_read(void)
{
	int rc, nbytes;

	nbytes = handle_buffer(sizeof(buffer));
	if (nbytes <= 0)
		return nbytes;

	if (!memcmp(buffer, phone_prompt1, sizeof(phone_prompt1))) {
		printf("Received PROMPT1 from phone, responding with CMD\n");
		dnload.expect_hdlc = 0;
		dnload.state = WAITING_PROMPT2;
		if(dnload.filename) {
			rc = write(dnload.serial_fd.fd, dnload_cmd, sizeof(dnload_cmd));

			/* re-read file */
			rc = read_file(dnload.filename, dnload.do_chainload);
			if (rc < 0) {
				fprintf(stderr, "read_file(%s) failed with %d\n",
						dnload.filename, rc);
				exit(1);
			}
		}
	} else if (!memcmp(buffer, phone_prompt2, sizeof(phone_prompt2))) {
		printf("Received PROMPT2 from phone, starting download\n");
		dnload.serial_fd.when = BSC_FD_READ | BSC_FD_WRITE;
		dnload.state = DOWNLOADING;
	} else if (!memcmp(buffer, phone_ack, sizeof(phone_ack))) {
		printf("Received DOWNLOAD ACK from phone, your code is"
			" running now!\n");
		dnload.serial_fd.when = BSC_FD_READ;
		dnload.state = WAITING_PROMPT1;
		dnload.write_ptr = dnload.data;
		dnload.expect_hdlc = 1;

		/* check for romloader chainloading mode used as a workaround
		 * for the magic on the C139/C140 and J100i */
		if (dnload.do_chainload) {
			printf("Enabled Compal ramloader -> Calypso romloader"
				" chainloading mode\n");
			bufptr = buffer;
			dnload.previous_mode = dnload.mode;
			dnload.mode = MODE_ROMLOAD;
			osmo_serial_set_baudrate(dnload.serial_fd.fd, ROMLOAD_INIT_BAUDRATE);
			tick_timer.cb = &beacon_timer_cb;
			tick_timer.data = &tick_timer;
			osmo_timer_schedule(&tick_timer, 0, dnload.beacon_interval);
		}
	} else if (!memcmp(buffer, phone_nack, sizeof(phone_nack))) {
		printf("Received DOWNLOAD NACK from phone, something went"
			" wrong :(\n");
		dnload.serial_fd.when = BSC_FD_READ;
		dnload.state = WAITING_PROMPT1;
		dnload.write_ptr = dnload.data;
	} else if (!memcmp(buffer, phone_nack_magic, sizeof(phone_nack_magic))) {
		printf("Received MAGIC NACK from phone, you need to"
			" have \"1003\" at 0x803ce0\n");
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

/* "Calypso non-secure romloader" */
static int handle_read_romload(void)
{
	int rc, nbytes, buf_used_len;

	/* virtually limit buffer length for romloader, since responses
	 * are shorter and vary in length */

	switch (dnload.romload_state) {
	case WAITING_PARAM_ACK:
		buf_used_len = 4;	/* ">p" + uint16_t len */
		break;
	case WAITING_CHECKSUM_ACK:
		buf_used_len = 3;	/* ">c" + uint8_t checksum */
		break;
	case FINISHED:
		buf_used_len = sizeof(buffer);
		break;
	default:
		buf_used_len = 2;	/* ">*" */
	}

	nbytes = handle_buffer(buf_used_len);
	if (nbytes <= 0)
		return nbytes;

	switch (dnload.romload_state) {
	case WAITING_IDENTIFICATION:
		if (memcmp(buffer, romload_ident_ack,
			    sizeof(romload_ident_ack)))
			break;

		printf("Received ident ack from phone, sending "
			"parameter sequence\n");
		dnload.expect_hdlc = 1;
		dnload.romload_state = WAITING_PARAM_ACK;
		rc = write(dnload.serial_fd.fd, romload_param,
			   sizeof(romload_param));
		/* re-read file */
		rc = read_file(dnload.filename, 0);
		if (rc < 0) {
			fprintf(stderr, "read_file(%s) failed with %d\n",
				dnload.filename, rc);
			exit(1);
		}
		break;
	case WAITING_PARAM_ACK:
		if (memcmp(buffer, romload_param_ack,
			    sizeof(romload_param_ack)))
			break;

		printf("Received parameter ack from phone, "
			"starting download\n");
		osmo_serial_set_baudrate(dnload.serial_fd.fd, ROMLOAD_DL_BAUDRATE);

		/* using the max blocksize the phone tells us */
		dnload.block_payload_size = ((buffer[3] << 8) + buffer[2]);
		dnload.block_payload_size -= ROMLOAD_BLOCK_HDR_LEN;
		dnload.romload_state = SENDING_BLOCKS;
		dnload.block_number = 0;
		romload_prepare_block();
		bufptr -= 2;
		break;
	case WAITING_BLOCK_ACK:
	case LAST_BLOCK_SENT:
		if (!memcmp(buffer, romload_block_ack,
			    sizeof(romload_block_ack))) {
			if (dnload.romload_state == LAST_BLOCK_SENT) {
				/* send the checksum */
				uint8_t final_checksum =
					(~(dnload.romload_dl_checksum) & 0xff);
				rc = write(dnload.serial_fd.fd,
					   romload_checksum_cmd,
					   sizeof(romload_checksum_cmd));
				rc = write(dnload.serial_fd.fd,
					   &final_checksum, 1);
				dnload.romload_state = WAITING_CHECKSUM_ACK;
			} else
				romload_prepare_block();
		} else if (!memcmp(buffer, romload_block_nack,
				   sizeof(romload_block_nack))) {
			printf("Received block nack from phone, "
				"something went wrong, aborting\n");
			osmo_serial_set_baudrate(dnload.serial_fd.fd, ROMLOAD_INIT_BAUDRATE);
			dnload.romload_state = WAITING_IDENTIFICATION;
			osmo_timer_schedule(&tick_timer, 0, dnload.beacon_interval);
		}
		break;
	case WAITING_CHECKSUM_ACK:
		if (!memcmp(buffer, romload_checksum_ack,
			    sizeof(romload_checksum_ack))) {

			rc = write(dnload.serial_fd.fd, romload_branch_cmd,
				   sizeof(romload_branch_cmd));
			rc = write(dnload.serial_fd.fd, &dnload.load_address,
				   sizeof(dnload.load_address));
			dnload.romload_state = WAITING_BRANCH_ACK;
			bufptr -= 1;
		} else if (!memcmp(buffer, romload_checksum_nack,
				   sizeof(romload_checksum_nack))) {
			printf("Checksum on phone side (0x%02x) doesn't "
				"match ours, aborting\n", ~buffer[2]);
			osmo_serial_set_baudrate(dnload.serial_fd.fd, ROMLOAD_INIT_BAUDRATE);
			dnload.romload_state = WAITING_IDENTIFICATION;
			osmo_timer_schedule(&tick_timer, 0, dnload.beacon_interval);
			bufptr -= 1;
		}
		break;
	case WAITING_BRANCH_ACK:
		if (!memcmp(buffer, romload_branch_ack,
			    sizeof(romload_branch_ack))) {
			printf("Received branch ack, your code is running now!\n");
			dnload.serial_fd.when = BSC_FD_READ;
			dnload.romload_state = FINISHED;
			dnload.write_ptr = dnload.data;
			dnload.expect_hdlc = 1;

			if (!dnload.do_chainload)
				break;

			/* if using chainloading mode, switch back to the Compal
			 * ramloader settings to make sure the auto-reload
			 * feature works */
			bufptr = buffer;
			dnload.mode = dnload.previous_mode;
			dnload.romload_state = WAITING_IDENTIFICATION;
			osmo_serial_set_baudrate(dnload.serial_fd.fd, MODEM_BAUDRATE);
		} else if (!memcmp(buffer, romload_branch_nack,
			   sizeof(romload_branch_nack))) {
			printf("Received branch nack, aborting\n");
			osmo_serial_set_baudrate(dnload.serial_fd.fd, ROMLOAD_INIT_BAUDRATE);
			dnload.romload_state = WAITING_IDENTIFICATION;
			osmo_timer_schedule(&tick_timer, 0, dnload.beacon_interval);
		}
		break;
	default:
		break;
	}

	bufptr += nbytes;
	return nbytes;
}

/* MTK romloader */
static int handle_read_mtk(void)
{
	int rc, nbytes, buf_used_len;

	switch (dnload.mtk_state) {
	case MTK_WAIT_ADDR_ACK:
	case MTK_WAIT_SIZE_ACK:
	case MTK_WAIT_BRANCH_ADDR_ACK:
		buf_used_len = 4;
		break;
	case MTK_FINISHED:
		buf_used_len = sizeof(buffer);
		break;
	default:
		buf_used_len = 1;
	}

	nbytes = handle_buffer(buf_used_len);
	if (nbytes <= 0)
		return nbytes;

	switch (dnload.mtk_state) {
	case MTK_INIT_1:
		if (!(buffer[0] == mtk_init_resp[0]))
			break;
		dnload.mtk_state = MTK_INIT_2;
		printf("Received init magic byte 1\n");
		rc = write(dnload.serial_fd.fd, &mtk_init_cmd[1], 1);
		break;
	case MTK_INIT_2:
		if (!(buffer[0] == mtk_init_resp[1]))
			break;
		dnload.mtk_state = MTK_INIT_3;
		printf("Received init magic byte 2\n");
		rc = write(dnload.serial_fd.fd, &mtk_init_cmd[2], 1);
		break;
	case MTK_INIT_3:
		if (!(buffer[0] == mtk_init_resp[2]))
			break;
		dnload.mtk_state = MTK_INIT_4;
		printf("Received init magic byte 3\n");
		rc = write(dnload.serial_fd.fd, &mtk_init_cmd[3], 1);
		break;
	case MTK_INIT_4:
		if (!(buffer[0] == mtk_init_resp[3]))
			break;
		dnload.mtk_state = MTK_WAIT_WRITE_ACK;
		printf("Received init magic byte 4, requesting write\n");
		rc = write(dnload.serial_fd.fd, &mtk_command[0], 1);
		break;
	case MTK_WAIT_WRITE_ACK:
		if (!(buffer[0] == mtk_command[0]))
			break;
		dnload.mtk_state = MTK_WAIT_ADDR_ACK;
		printf("Received write ack, sending load address\n");

		rc = write(dnload.serial_fd.fd, &dnload.load_address,
			   sizeof(dnload.load_address));
		break;
	case MTK_WAIT_ADDR_ACK:
		if (memcmp(buffer, dnload.load_address,
			    sizeof(dnload.load_address)))
			break;
		printf("Received address ack from phone, sending loadsize\n");
		/* re-read file */
		rc = read_file(dnload.filename, 0);
		if (rc < 0) {
			fprintf(stderr, "read_file(%s) failed with %d\n",
				dnload.filename, rc);
			exit(1);
		}
		dnload.block_number = 0;
		mtk_prepare_block();
		dnload.mtk_state = MTK_WAIT_SIZE_ACK;
		rc = write(dnload.serial_fd.fd, &dnload.mtk_send_size,
			   sizeof(dnload.mtk_send_size));
		break;
	case MTK_WAIT_SIZE_ACK:
		if (memcmp(buffer, dnload.mtk_send_size,
			    sizeof(dnload.mtk_send_size)))
			break;
		printf("Received size ack\n");
		dnload.expect_hdlc = 1;
		dnload.mtk_state = MTK_SENDING_BLOCKS;
		dnload.serial_fd.when = BSC_FD_READ | BSC_FD_WRITE;
		bufptr -= 3;
		break;
	case MTK_SENDING_BLOCKS:
		if (!(buffer[0] == dnload.block[dnload.echo_bytecount]))
			printf("Warning: Byte %i of Block %i doesn't match,"
				" check your serial connection!\n",
				dnload.echo_bytecount, dnload.block_number);
		dnload.echo_bytecount++;

		if ((dnload.echo_bytecount+1) > MTK_BLOCK_SIZE) {
			if ( dnload.block_number == dnload.block_count) {
				rc = write(dnload.serial_fd.fd,
					   &mtk_command[3], 1);
				printf("Sending branch command\n");
				dnload.expect_hdlc = 0;
				dnload.mtk_state = MTK_WAIT_BRANCH_CMD_ACK;
				break;
			}
			printf("Received Block %i preparing next block\n",
				dnload.block_number);
			mtk_prepare_block();
			dnload.serial_fd.when = BSC_FD_READ | BSC_FD_WRITE;
		}
		break;
	case MTK_WAIT_BRANCH_CMD_ACK:
		if (!(buffer[0] == mtk_command[3]))
			break;
		dnload.mtk_state = MTK_WAIT_BRANCH_ADDR_ACK;
		printf("Received branch command ack, sending address\n");

		rc = write(dnload.serial_fd.fd, &dnload.load_address,
			   sizeof(dnload.load_address));
		break;
	case MTK_WAIT_BRANCH_ADDR_ACK:
		if (memcmp(buffer, dnload.load_address,
			    sizeof(dnload.load_address)))
			break;
		printf("Received branch address ack, code should run now\n");
		osmo_serial_set_baudrate(dnload.serial_fd.fd, MODEM_BAUDRATE);
		dnload.serial_fd.when = BSC_FD_READ;
		dnload.mtk_state = MTK_FINISHED;
		dnload.write_ptr = dnload.data;
		dnload.expect_hdlc = 1;
		break;
	default:
		break;
	}

	bufptr += nbytes;
	return nbytes;
}

static int serial_read(struct osmo_fd *fd, unsigned int flags)
{
	int rc;
	if (flags & BSC_FD_READ) {
		switch (dnload.mode) {
			case MODE_ROMLOAD:
				while ((rc = handle_read_romload()) > 0);
				break;
			case MODE_MTK:
				while ((rc = handle_read_mtk()) > 0);
				break;
			default:
				while ((rc = handle_read()) > 0);
				break;
		}
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
	if (!strcasecmp(arg, "c123"))
		return MODE_C123;
	else if (!strcasecmp(arg, "c123xor"))
		return MODE_C123xor;
	else if (!strcasecmp(arg, "c140"))
		return MODE_C140;
	else if (!strcasecmp(arg, "c140xor"))
		return MODE_C140xor;
	else if (!strcasecmp(arg, "c155"))
		return MODE_C155;
	else if (!strcasecmp(arg, "romload"))
		return MODE_ROMLOAD;
	else if (!strcasecmp(arg, "mtk"))
		return MODE_MTK;

	return MODE_INVALID;
}

#define HELP_TEXT \
	"[ -v | -h ] [ -d [t][r] ] [ -p /dev/ttyXXXX ]\n" \
	"\t\t [ -c ] (enable chainloading of highram-images)\n" \
	"\t\t [ -s /tmp/osmocom_l2 ]\n" \
	"\t\t [ -l /tmp/osmocom_loader ]\n" \
	"\t\t [ -m {c123,c123xor,c140,c140xor,c155,romload,mtk} ]\n" \
	"\t\t [ -i beacon-interval (mS) ]\n" \
	"\t\t  file.bin\n\n" \
	"* Open serial port /dev/ttyXXXX (connected to your phone)\n" \
	"* Perform handshaking with the ramloader in the phone\n" \
	"* Download file.bin to the attached phone (base address 0x00800100)\n"

static int usage(const char *name)
{
	printf("Usage: %s ", name);
	printf(HELP_TEXT);
	exit(2);
}

static int version(const char *name)
{
	printf("%s version %s\n", name, PACKAGE_VERSION);
	exit(2);
}

static int un_tool_read(struct osmo_fd *fd, unsigned int flags)
{
	int rc, c;
	uint16_t length = 0xffff;
	uint8_t buf[4096];
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

	memcpy(&length, buf, sizeof length);
	length = ntohs(length);

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
	osmo_fd_unregister(fd);
	llist_del(&con->entry);
	talloc_free(con);
	return -1;
}

/* accept a new connection */
static int tool_accept(struct osmo_fd *fd, unsigned int flags)
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
	if (osmo_fd_register(&con->fd) != 0) {
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
	struct osmo_fd *bfd = &ts->bfd;
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

	if (osmo_fd_register(bfd) != 0) {
		fprintf(stderr, "Failed to register the bfd.\n");
		return -1;
	}

	return 0;
}

extern void hdlc_tpudbg_cb(uint8_t dlci, struct msgb *msg);

void parse_debug(const char *str)
{
	while(*str) {
		switch(*str) {
		case 't':
			dnload.dump_tx = 1;
			break;
		case 'r':
			dnload.dump_rx = 1;
			break;
		default:
			printf("Unknown debug flag %c\n", *str);
			abort();
			break;
		}
		str++;
	}
}

int main(int argc, char **argv)
{
	int opt, flags;
	uint32_t tmp_load_address = ROMLOAD_ADDRESS;
	const char *serial_dev = "/dev/ttyUSB1";
	const char *layer2_un_path = "/tmp/osmocom_l2";
	const char *loader_un_path = "/tmp/osmocom_loader";

	dnload.mode = MODE_C123;
	dnload.beacon_interval = DEFAULT_BEACON_INTERVAL;
	dnload.do_chainload = 0;

	while ((opt = getopt(argc, argv, "d:hl:p:m:cs:i:v")) != -1) {
		switch (opt) {
		case 'p':
			serial_dev = optarg;
			break;
		case 'm':
			dnload.mode = parse_mode(optarg);
			if (dnload.mode == MODE_INVALID)
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
		case 'd':
			parse_debug(optarg);
			break;
		case 'c':
			dnload.do_chainload = 1;
			break;
		case 'i':
			dnload.beacon_interval = atoi(optarg) * 1000;
			break;
		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	if (argc <= optind) {
		dnload.filename = NULL;
	} else {
		dnload.filename = argv[optind];
	}

	dnload.serial_fd.fd = osmo_serial_init(serial_dev, MODEM_BAUDRATE);
	if (dnload.serial_fd.fd < 0) {
		fprintf(stderr, "Cannot open serial device %s\n", serial_dev);
		exit(1);
	}

	if (osmo_fd_register(&dnload.serial_fd) != 0) {
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
	if (register_tool_server(&dnload.layer2_server, layer2_un_path,
				 SC_DLCI_L1A_L23) != 0)
		exit(1);

	if (register_tool_server(&dnload.loader_server, loader_un_path,
				 SC_DLCI_LOADER) != 0)
		exit(1);

	/* if in romload mode, start our beacon timer */
	if (dnload.mode == MODE_ROMLOAD) {
		tmp_load_address = ROMLOAD_ADDRESS;
		osmo_serial_set_baudrate(dnload.serial_fd.fd, ROMLOAD_INIT_BAUDRATE);
		tick_timer.cb = &beacon_timer_cb;
		tick_timer.data = &tick_timer;
		osmo_timer_schedule(&tick_timer, 0, dnload.beacon_interval);
	}
	else if (dnload.mode == MODE_MTK) {
		tmp_load_address = MTK_ADDRESS;
		osmo_serial_set_baudrate(dnload.serial_fd.fd, MTK_INIT_BAUDRATE);
		tick_timer.cb = &mtk_timer_cb;
		tick_timer.data = &tick_timer;
		osmo_timer_schedule(&tick_timer, 0, dnload.beacon_interval);
	}

	dnload.load_address[0] = (tmp_load_address >> 24) & 0xff;
	dnload.load_address[1] = (tmp_load_address >> 16) & 0xff;
	dnload.load_address[2] = (tmp_load_address >> 8) & 0xff;
	dnload.load_address[3] = tmp_load_address & 0xff;

	while (1) {
		if (osmo_select_main(0) < 0)
			break;
	}

	close(dnload.serial_fd.fd);

	exit(0);
}
