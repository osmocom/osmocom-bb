/*
 * serial.c
 *
 * Utility functions to deal with serial ports
 *
 * Copyright (C) 2011  Sylvain Munaut <tnt@246tNt.com>
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
 */

/*! \addtogroup serial
 *  @{
 */

/*! \file serial.c
 *  \file Osmocom serial port helpers
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef __linux__
#include <linux/serial.h>
#endif

#include <osmocom/core/serial.h>


#if 0
# define dbg_perror(x) perror(x)
#else
# define dbg_perror(x) do { } while (0)
#endif

/*! \brief Open serial device and does base init
 *  \param[in] dev Path to the device node to open
 *  \param[in] baudrate Baudrate constant (speed_t: B9600, B...)
 *  \returns >=0 file descriptor in case of success or negative errno.
 */
int
osmo_serial_init(const char *dev, speed_t baudrate)
{
	int rc, fd=0, v24;
	struct termios tio;

	/* Open device */
	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		dbg_perror("open");
		return -errno;
	}

	/* Configure serial interface */
	rc = tcgetattr(fd, &tio);
	if (rc < 0) {
		dbg_perror("tcgetattr()");
		rc = -errno;
		goto error;
	}

	cfsetispeed(&tio, baudrate);
	cfsetospeed(&tio, baudrate);

	tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
	tio.c_cflag |=  (CREAD | CLOCAL | CS8);
	tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	tio.c_iflag |=  (INPCK | ISTRIP);
	tio.c_iflag &= ~(ISTRIP | IXON | IXOFF | IGNBRK | INLCR | ICRNL | IGNCR);
	tio.c_oflag &= ~(OPOST | ONLCR);

	rc = tcsetattr(fd, TCSANOW, &tio);
	if (rc < 0) {
		dbg_perror("tcsetattr()");
		rc = -errno;
		goto error;
	}

	/* Set ready to read/write */
	v24 = TIOCM_DTR | TIOCM_RTS;
	rc = ioctl(fd, TIOCMBIS, &v24);
	if (rc < 0) {
		dbg_perror("ioctl(TIOCMBIS)");
		rc = -errno;
		goto error;
	}

	return fd;

error:
	if (fd)
		close(fd);
	return rc;
}

static int
_osmo_serial_set_baudrate(int fd, speed_t baudrate)
{
	int rc;
	struct termios tio;

	rc = tcgetattr(fd, &tio);
	if (rc < 0) {
		dbg_perror("tcgetattr()");
		return -errno;
	}
	cfsetispeed(&tio, baudrate);
	cfsetospeed(&tio, baudrate);

	rc = tcsetattr(fd, TCSANOW, &tio);
	if (rc < 0) {
		dbg_perror("tcgetattr()");
		return -errno;
	}

	return 0;
}

/*! \brief Change current baudrate
 *  \param[in] fd File descriptor of the open device
 *  \param[in] baudrate Baudrate constant (speed_t: B9600, B...)
 *  \returns 0 for success or negative errno.
 */
int
osmo_serial_set_baudrate(int fd, speed_t baudrate)
{
	osmo_serial_clear_custom_baudrate(fd);
	return _osmo_serial_set_baudrate(fd, baudrate);
}

/*! \brief Change current baudrate to a custom one using OS specific method
 *  \param[in] fd File descriptor of the open device
 *  \param[in] baudrate Baudrate as integer
 *  \returns 0 for success or negative errno.
 *
 *  This function might not work on all OS or with all type of serial adapters
 */
int
osmo_serial_set_custom_baudrate(int fd, int baudrate)
{
#ifdef __linux__
	int rc;
	struct serial_struct ser_info;

	rc = ioctl(fd, TIOCGSERIAL, &ser_info);
	if (rc < 0) {
		dbg_perror("ioctl(TIOCGSERIAL)");
		return -errno;
	}

	ser_info.flags = ASYNC_SPD_CUST | ASYNC_LOW_LATENCY;
	ser_info.custom_divisor = ser_info.baud_base / baudrate;

	rc = ioctl(fd, TIOCSSERIAL, &ser_info);
	if (rc < 0) {
		dbg_perror("ioctl(TIOCSSERIAL)");
		return -errno;
	}

	return _osmo_serial_set_baudrate(fd, B38400); /* 38400 is a kind of magic ... */
#elif defined(__APPLE__)
#ifndef IOSSIOSPEED
#define IOSSIOSPEED    _IOW('T', 2, speed_t)
#endif
	int rc;

	unsigned int speed = baudrate;
	rc = ioctl(fd, IOSSIOSPEED, &speed);
	if (rc < 0) {
		dbg_perror("ioctl(IOSSIOSPEED)");
		return -errno;
	}
	return 0;
#else
#warning osmo_serial_set_custom_baudrate: unsupported platform
	return 0;
#endif
}

/*! \brief Clear any custom baudrate
 *  \param[in] fd File descriptor of the open device
 *  \returns 0 for success or negative errno.
 *
 *  This function might not work on all OS or with all type of serial adapters
 */
int
osmo_serial_clear_custom_baudrate(int fd)
{
#ifdef __linux__
	int rc;
	struct serial_struct ser_info;

	rc = ioctl(fd, TIOCGSERIAL, &ser_info);
	if (rc < 0) {
		dbg_perror("ioctl(TIOCGSERIAL)");
		return -errno;
	}

	ser_info.flags = ASYNC_LOW_LATENCY;
	ser_info.custom_divisor = 0;

	rc = ioctl(fd, TIOCSSERIAL, &ser_info);
	if (rc < 0) {
		dbg_perror("ioctl(TIOCSSERIAL)");
		return -errno;
	}
#endif
	return 0;
}

/*! @} */
