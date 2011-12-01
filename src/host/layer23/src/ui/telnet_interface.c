/* minimalistic telnet/network interface it might turn into a wire interface */
/* (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/telnet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/select.h>
#include <osmocom/vty/buffer.h>

#include <osmocom/bb/ui/ui.h>
#include <osmocom/bb/ui/telnet_interface.h>

/* Send WILL TELOPT_ECHO to remote server. */
static void vty_will_echo(struct ui_inst *ui)
{
	unsigned char cmd[] = { IAC, WILL, TELOPT_ECHO, '\0' };
	ui_telnet_puts(ui, (char *)cmd);
}

/* Make suppress Go-Ahead telnet option. */
static void vty_will_suppress_go_ahead(struct ui_inst *ui)
{
	unsigned char cmd[] = { IAC, WILL, TELOPT_SGA, '\0' };
	ui_telnet_puts(ui, (char *)cmd);
}

/* Make don't use linemode over telnet. */
static void vty_dont_linemode(struct ui_inst *ui)
{
	unsigned char cmd[] = { IAC, DONT, TELOPT_LINEMODE, '\0' };
	ui_telnet_puts(ui, (char *)cmd);
}

/* Use window size. */
static void vty_do_window_size(struct ui_inst *ui)
{
	unsigned char cmd[] = { IAC, DO, TELOPT_NAWS, '\0' };
	ui_telnet_puts(ui, (char *)cmd);
}

static int telnet_new_connection(struct osmo_fd *fd, unsigned int what);

int ui_telnet_init(struct ui_inst *ui, void *tall_ctx, int port)
{
	struct sockaddr_in sock_addr;
	int fd, rc, on = 1;

	ui->tall_telnet_ctx = talloc_named_const(tall_ctx, 1,
					     "ui_telnet_connection");

	INIT_LLIST_HEAD(&ui->active_connections);

	/* FIXME: use new socket.c code of libosmocore */
	fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (fd < 0) {
		LOGP(0, LOGL_ERROR, "Telnet interface socket creation "
			"failed\n");
		talloc_free(ui->tall_telnet_ctx);
		return fd;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	memset(&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);
	sock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	rc = bind(fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
	if (rc < 0) {
		LOGP(0, LOGL_ERROR, "Telnet interface failed to bind\n");
		close(fd);
		talloc_free(ui->tall_telnet_ctx);
		return rc;
	}

	rc = listen(fd, 0);
	if (rc < 0) {
		LOGP(0, LOGL_ERROR, "Telnet interface failed to listen\n");
		close(fd);
		talloc_free(ui->tall_telnet_ctx);
		return rc;
	}

	ui->server_socket.when = BSC_FD_READ;
	ui->server_socket.cb = telnet_new_connection;
	ui->server_socket.priv_nr = 0;
	ui->server_socket.data = ui;
	ui->server_socket.fd = fd;
	osmo_fd_register(&ui->server_socket);

	return 0;
}

int ui_telnet_puts(struct ui_inst *ui, const char *text)
{
	struct ui_telnet_connection *conn;

	llist_for_each_entry(conn, &ui->active_connections, entry) {
		buffer_put(conn->obuf, text, strlen(text));
		conn->fd.when |= BSC_FD_WRITE;
	}

	return 0;
}

static int telnet_close_client(struct osmo_fd *fd)
{
	struct ui_telnet_connection *conn =
		(struct ui_telnet_connection*)fd->data;
	struct ui_inst *ui = conn->ui;

	close(fd->fd);
	osmo_fd_unregister(fd);

	buffer_free(conn->obuf);

	llist_del(&conn->entry);
	talloc_free(conn);

	/* notify about connection */
	ui->telnet_cb(ui);

	return 0;
}

//#define DEBUG_SEQEUENCES
static int telnet_getc(struct ui_telnet_connection *conn, unsigned char p)
{
#ifdef DEBUG_SEQEUENCES
	printf("k: %d\n", p);
#endif
	
	if (conn->sb) {
		if (p == SE) {
#ifdef DEBUG_SEQEUENCES
			puts("se");
#endif
			conn->sb = 0;
			conn->iac = 0;
		}
		return 0;
	}
	if (conn->iac) {
		if (conn->iac == 1) {
			if (p == SB) {
				conn->sb = 1;
#ifdef DEBUG_SEQEUENCES
				puts("sb");
#endif
				return 0;
			}
			if (p == IAC) {
				conn->iac = 0;
#ifdef DEBUG_SEQEUENCES
				puts("iac iac (ende)");
#endif
				return 0;
			}
			conn->iac = 2;
			return 0;
		}
		conn->iac = 0;
#ifdef DEBUG_SEQEUENCES
		puts("iac ende");
#endif
		return 0;
	}
	if (p == IAC) {
		conn->iac = 1;
#ifdef DEBUG_SEQEUENCES
		puts("iac");
#endif
		return 0;
	}

	if (conn->esc) {
		if (conn->esc == 1) {
			if (p == 91) {
				conn->esc = 2;
				return 0;
			}
			if (p == 79) {
				conn->esc = 3;
				return 0;
			}
			conn->esc = 0;
#ifdef DEBUG_SEQEUENCES
			puts("esc abort");
#endif
			return 0;
		}
		if (conn->esc == 2) {
			if (p == 65)
				ui_inst_keypad(conn->ui, UI_KEY_UP);
			if (p == 66)
				ui_inst_keypad(conn->ui, UI_KEY_DOWN);
			if (p == 67)
				ui_inst_keypad(conn->ui, UI_KEY_RIGHT);
			if (p == 68)
				ui_inst_keypad(conn->ui, UI_KEY_LEFT);
			if (p == 72)
				ui_inst_keypad(conn->ui, UI_KEY_PICKUP);
			if (p == 70)
				ui_inst_keypad(conn->ui, UI_KEY_HANGUP);
		}
		if (conn->esc == 3) {
			if (p == 80)
				ui_inst_keypad(conn->ui, UI_KEY_F1);
			if (p == 81)
				ui_inst_keypad(conn->ui, UI_KEY_F2);
		}
		conn->esc = 0;
#ifdef DEBUG_SEQEUENCES
		puts("esc ende");
#endif
		return 0;
	}
	if (p == 27) {
		conn->esc = 1;
#ifdef DEBUG_SEQEUENCES
		puts("esc");
#endif
		return 0;
	}

	if (p == 3 || p == 4)
		return -1;

	/* refresh */
	if (p == 12) {
		ui_inst_refresh(conn->ui);
		return 0;
	}

	ui_inst_keypad(conn->ui, p);

	return 0;
}

static int client_data(struct osmo_fd *fd, unsigned int what)
{
	struct ui_telnet_connection *conn = fd->data;
	int rc = 0;

	if (what & BSC_FD_READ) {
		char buffer[16], *p = buffer;
		int nbytes;
		nbytes = read(fd->fd, buffer, sizeof(buffer));
		if (nbytes == 0) {
			conn->ui = NULL;
			telnet_close_client(fd);
			return rc;
		}
		if (nbytes > 0) {
			while (nbytes--) {
				rc = telnet_getc(conn, *p++);
				if (rc < 0) {
					telnet_close_client(&conn->fd);
					/* telnet conn is gone, must exit! */
					return 0;
				}
			}
		}
	}

	if (what & BSC_FD_WRITE) {
		rc = buffer_flush_all(conn->obuf, fd->fd);
		if (rc == BUFFER_EMPTY)
			conn->fd.when &= ~BSC_FD_WRITE;
	}

	return rc;
}

static int telnet_new_connection(struct osmo_fd *fd, unsigned int what)
{
	struct ui_telnet_connection *conn;
	struct sockaddr_in sockaddr;
	socklen_t len = sizeof(sockaddr);
	int new_connection = accept(fd->fd, (struct sockaddr*)&sockaddr, &len);
	struct ui_inst *ui = (struct ui_inst *) fd->data;

	if (new_connection < 0) {
		LOGP(0, LOGL_ERROR, "telnet accept failed\n");
		return new_connection;
	}

	conn = talloc_zero(ui->tall_telnet_ctx, struct ui_telnet_connection);
	conn->ui = ui;
	conn->fd.data = conn;
	conn->fd.fd = new_connection;
	conn->fd.when = BSC_FD_READ;
	conn->fd.cb = client_data;
	osmo_fd_register(&conn->fd);
	llist_add_tail(&conn->entry, &ui->active_connections);

	conn->obuf = buffer_new(ui->tall_telnet_ctx, 0);

	vty_will_echo(ui);
	vty_will_suppress_go_ahead(ui);
	vty_dont_linemode(ui);
	vty_do_window_size(ui);

	/* notify about connection */
	ui->telnet_cb(ui);

	return 0;
}

void ui_telnet_exit(struct ui_inst *ui) 
{
	struct ui_telnet_connection *tc, *tc2;

	if (ui->server_socket.fd <= 0)
		return;

	llist_for_each_entry_safe(tc, tc2, &ui->active_connections, entry)
		telnet_close_client(&tc->fd);

	osmo_fd_unregister(&ui->server_socket);
	close(ui->server_socket.fd);
	ui->server_socket.fd = -1;
	talloc_free(ui->tall_telnet_ctx);
}

