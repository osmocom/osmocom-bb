/* SNMP-like status interface
 *
 * (C) 2010-2011 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2010-2011 by On-Waves
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

#include "config.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <netinet/in.h>
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <osmocom/ctrl/control_cmd.h>
#include <osmocom/ctrl/control_if.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/core/select.h>
#include <osmocom/core/statistics.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/socket.h>

#include <osmocom/gsm/protocol/ipaccess.h>
#include <osmocom/gsm/ipa.h>

#include <osmocom/vty/command.h>
#include <osmocom/vty/vector.h>

vector ctrl_node_vec;

int ctrl_parse_get_num(vector vline, int i, long *num)
{
	char *token, *tmp;

	if (i >= vector_active(vline))
		return 0;
	token = vector_slot(vline, i);

	errno = 0;
	if (token[0] == '\0')
		return 0;

	*num = strtol(token, &tmp, 10);
	if (tmp[0] != '\0' || errno != 0)
		return 0;

	return 1;
}

/* Send command to all  */
int ctrl_cmd_send_to_all(struct ctrl_handle *ctrl, struct ctrl_cmd *cmd)
{
	struct ctrl_connection *ccon;
	int ret = 0;

	llist_for_each_entry(ccon, &ctrl->ccon_list, list_entry) {
		if (ccon == cmd->ccon)
			continue;
		if (ctrl_cmd_send(&ccon->write_queue, cmd))
			ret++;
	}
	return ret;
}

int ctrl_cmd_send(struct osmo_wqueue *queue, struct ctrl_cmd *cmd)
{
	int ret;
	struct msgb *msg;

	msg = ctrl_cmd_make(cmd);
	if (!msg) {
		LOGP(DLCTRL, LOGL_ERROR, "Could not generate msg\n");
		return -1;
	}

	ipa_prepend_header_ext(msg, IPAC_PROTO_EXT_CTRL);
	ipa_prepend_header(msg, IPAC_PROTO_OSMO);

	ret = osmo_wqueue_enqueue(queue, msg);
	if (ret != 0) {
		LOGP(DLCTRL, LOGL_ERROR, "Failed to enqueue the command.\n");
		msgb_free(msg);
	}
	return ret;
}

struct ctrl_cmd *ctrl_cmd_trap(struct ctrl_cmd *cmd)
{
	struct ctrl_cmd *trap;

	trap = ctrl_cmd_cpy(cmd, cmd);
	if (!trap)
		return NULL;

	trap->ccon = cmd->ccon;
	trap->type = CTRL_TYPE_TRAP;
	return trap;
}

static void control_close_conn(struct ctrl_connection *ccon)
{
	struct ctrl_cmd_def *cd, *cd2;

	osmo_wqueue_clear(&ccon->write_queue);
	close(ccon->write_queue.bfd.fd);
	osmo_fd_unregister(&ccon->write_queue.bfd);
	llist_del(&ccon->list_entry);
	if (ccon->closed_cb)
		ccon->closed_cb(ccon);
	msgb_free(ccon->pending_msg);

	/* clean up deferred commands */
	llist_for_each_entry_safe(cd, cd2, &ccon->def_cmds, list) {
		/* delete from list of def_cmds for this ccon */
		llist_del(&cd->list);
		/* not strictly needed as this is a slave to the ccon which we
		 * are about to free anyway */
		talloc_free(cd->cmd);
		/* set the CMD to null, this is the indication to the user that
		 * the connection for this command has gone */
		cd->cmd = NULL;
	}

	talloc_free(ccon);
}

int ctrl_cmd_handle(struct ctrl_handle *ctrl, struct ctrl_cmd *cmd,
		    void *data)
{
	char *request;
	int i, j, ret, node;

	vector vline, cmdvec, cmds_vec;

	ret = CTRL_CMD_ERROR;
	cmd->reply = NULL;
	node = CTRL_NODE_ROOT;
	cmd->node = data;

	request = talloc_strdup(cmd, cmd->variable);
	if (!request)
		goto err;

	for (i=0;i<strlen(request);i++) {
		if (request[i] == '.')
			request[i] = ' ';
	}

	vline = cmd_make_strvec(request);
	talloc_free(request);
	if (!vline) {
		cmd->reply = "cmd_make_strvec failed.";
		goto err;
	}

	for (i=0;i<vector_active(vline);i++) {
		int rc;

		if (ctrl->lookup)
			rc = ctrl->lookup(data, vline, &node, &cmd->node, &i);
		else
			rc = 0;

		if (rc == 1) {
			/* do nothing */
		} else if (rc == -ENODEV)
			goto err_missing;
		else if (rc == -ERANGE)
			goto err_index;
		else {
			/* If we're here the rest must be the command */
			cmdvec = vector_init(vector_active(vline)-i);
			for (j=i; j<vector_active(vline); j++) {
				vector_set(cmdvec, vector_slot(vline, j));
			}

			/* Get the command vector of the right node */
			cmds_vec = vector_lookup(ctrl_node_vec, node);

			if (!cmds_vec) {
				cmd->reply = "Command not found.";
				vector_free(cmdvec);
				break;
			}

			ret = ctrl_cmd_exec(cmdvec, cmd, cmds_vec, data);

			vector_free(cmdvec);
			break;
		}

		if (i+1 == vector_active(vline))
			cmd->reply = "Command not present.";
	}

	cmd_free_strvec(vline);

err:
	if (!cmd->reply) {
		if (ret == CTRL_CMD_ERROR) {
			cmd->reply = "An error has occured.";
			LOGP(DLCTRL, LOGL_NOTICE,
			     "%s: cmd->reply has not been set (ERROR).\n",
			     cmd->variable);
		} else if (ret == CTRL_CMD_REPLY) {
			LOGP(DLCTRL, LOGL_NOTICE,
			     "%s: cmd->reply has not been set (type = %d).\n",
			     cmd->variable, cmd->type);
			cmd->reply = "";
		} else {
			cmd->reply = "Command has been handled.";
		}
	}

	if (ret == CTRL_CMD_ERROR)
		cmd->type = CTRL_TYPE_ERROR;
	return ret;

err_missing:
	cmd_free_strvec(vline);
	cmd->type = CTRL_TYPE_ERROR;
	cmd->reply = "Error while resolving object";
	return ret;
err_index:
	cmd_free_strvec(vline);
	cmd->type = CTRL_TYPE_ERROR;
	cmd->reply = "Error while parsing the index.";
	return ret;
}


static int handle_control_read(struct osmo_fd * bfd)
{
	int ret = -1;
	struct osmo_wqueue *queue;
	struct ctrl_connection *ccon;
	struct ipaccess_head *iph;
	struct ipaccess_head_ext *iph_ext;
	struct msgb *msg = NULL;
	struct ctrl_cmd *cmd;
	struct ctrl_handle *ctrl = bfd->data;

	queue = container_of(bfd, struct osmo_wqueue, bfd);
	ccon = container_of(queue, struct ctrl_connection, write_queue);

	ret = ipa_msg_recv_buffered(bfd->fd, &msg, &ccon->pending_msg);
	if (ret <= 0) {
		if (ret == -EAGAIN)
			return 0;
		if (ret == 0)
			LOGP(DLCTRL, LOGL_INFO, "The control connection was closed\n");
		else
			LOGP(DLCTRL, LOGL_ERROR, "Failed to parse ip access message: %d\n", ret);

		goto err;
	}

	if (msg->len < sizeof(*iph) + sizeof(*iph_ext)) {
		LOGP(DLCTRL, LOGL_ERROR, "The message is too short.\n");
		goto err;
	}

	iph = (struct ipaccess_head *) msg->data;
	if (iph->proto != IPAC_PROTO_OSMO) {
		LOGP(DLCTRL, LOGL_ERROR, "Protocol mismatch. We got 0x%x\n", iph->proto);
		goto err;
	}

	iph_ext = (struct ipaccess_head_ext *) iph->data;
	if (iph_ext->proto != IPAC_PROTO_EXT_CTRL) {
		LOGP(DLCTRL, LOGL_ERROR, "Extended protocol mismatch. We got 0x%x\n", iph_ext->proto);
		goto err;
	}

	msg->l2h = iph_ext->data;

	cmd = ctrl_cmd_parse(ccon, msg);

	if (cmd) {
		cmd->ccon = ccon;
		if (ctrl_cmd_handle(ctrl, cmd, ctrl->data) != CTRL_CMD_HANDLED) {
			ctrl_cmd_send(queue, cmd);
			talloc_free(cmd);
		}
	} else {
		cmd = talloc_zero(ccon, struct ctrl_cmd);
		if (!cmd)
			goto err;
		LOGP(DLCTRL, LOGL_ERROR, "Command parser error.\n");
		cmd->type = CTRL_TYPE_ERROR;
		cmd->id = "err";
		cmd->reply = "Command parser error.";
		ctrl_cmd_send(queue, cmd);
		talloc_free(cmd);
	}

	msgb_free(msg);
	return 0;

err:
	control_close_conn(ccon);
	msgb_free(msg);
	return ret;
}

static int control_write_cb(struct osmo_fd *bfd, struct msgb *msg)
{
	int rc;

	rc = write(bfd->fd, msg->data, msg->len);
	if (rc != msg->len)
		LOGP(DLCTRL, LOGL_ERROR, "Failed to write message to the control connection.\n");

	return rc;
}

static struct ctrl_connection *ctrl_connection_alloc(void *ctx)
{
	struct ctrl_connection *ccon = talloc_zero(ctx, struct ctrl_connection);
	if (!ccon)
		return NULL;

	osmo_wqueue_init(&ccon->write_queue, 100);
	/* Error handling here? */

	INIT_LLIST_HEAD(&ccon->cmds);
	INIT_LLIST_HEAD(&ccon->def_cmds);

	return ccon;
}

static int listen_fd_cb(struct osmo_fd *listen_bfd, unsigned int what)
{
	int ret, fd, on;
	struct ctrl_handle *ctrl;
	struct ctrl_connection *ccon;
	struct sockaddr_in sa;
	socklen_t sa_len = sizeof(sa);


	if (!(what & BSC_FD_READ))
		return 0;

	fd = accept(listen_bfd->fd, (struct sockaddr *) &sa, &sa_len);
	if (fd < 0) {
		perror("accept");
		return fd;
	}
	LOGP(DLCTRL, LOGL_INFO, "accept()ed new control connection from %s\n",
		inet_ntoa(sa.sin_addr));

#ifdef TCP_NODELAY
	on = 1;
	ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
	if (ret != 0) {
		LOGP(DLCTRL, LOGL_ERROR, "Failed to set TCP_NODELAY: %s\n", strerror(errno));
		close(fd);
		return ret;
	}
#endif
	ccon = ctrl_connection_alloc(listen_bfd->data);
	if (!ccon) {
		LOGP(DLCTRL, LOGL_ERROR, "Failed to allocate.\n");
		close(fd);
		return -1;
	}

	ctrl = listen_bfd->data;
	ccon->write_queue.bfd.data = ctrl;
	ccon->write_queue.bfd.fd = fd;
	ccon->write_queue.bfd.when = BSC_FD_READ;
	ccon->write_queue.read_cb = handle_control_read;
	ccon->write_queue.write_cb = control_write_cb;

	ret = osmo_fd_register(&ccon->write_queue.bfd);
	if (ret < 0) {
		LOGP(DLCTRL, LOGL_ERROR, "Could not register FD.\n");
		close(ccon->write_queue.bfd.fd);
		talloc_free(ccon);
	}

	llist_add(&ccon->list_entry, &ctrl->ccon_list);

	return ret;
}

static uint64_t get_rate_ctr_value(const struct rate_ctr *ctr, int intv)
{
	if (intv >= RATE_CTR_INTV_NUM)
		return 0;

	/* Absolute value */
	if (intv == -1) {
		return  ctr->current;
	} else {
		return ctr->intv[intv].rate;
	}
}

static char *get_all_rate_ctr_in_group(void *ctx, const struct rate_ctr_group *ctrg, int intv)
{
	int i;
	char *counters = talloc_strdup(ctx, "");
	if (!counters)
		return NULL;

	for (i=0;i<ctrg->desc->num_ctr;i++) {
		counters = talloc_asprintf_append(counters, "\n%s.%u.%s %"PRIu64,
			ctrg->desc->group_name_prefix, ctrg->idx,
			ctrg->desc->ctr_desc[i].name,
			get_rate_ctr_value(&ctrg->ctr[i], intv));
		if (!counters)
			return NULL;
	}
	return counters;
}

static int get_rate_ctr_group(const char *ctr_group, int intv, struct ctrl_cmd *cmd)
{
	int i;
	char *counters;
	struct rate_ctr_group *ctrg;

	cmd->reply = talloc_asprintf(cmd, "All counters in group %s", ctr_group);
	if (!cmd->reply)
		goto oom;

	for (i=0;;i++) {
		ctrg = rate_ctr_get_group_by_name_idx(ctr_group, i);
		if (!ctrg)
			break;

		counters = get_all_rate_ctr_in_group(cmd, ctrg, intv);
		if (!counters)
			goto oom;

		cmd->reply = talloc_asprintf_append(cmd->reply, "%s", counters);
		talloc_free(counters);
		if (!cmd->reply)
			goto oom;
	}

	/* We found no counter group by that name */
	if (i == 0) {
		cmd->reply = talloc_asprintf(cmd, "No counter group with name %s.", ctr_group);
		return CTRL_CMD_ERROR;
	}

	return CTRL_CMD_REPLY;
oom:
	cmd->reply = "OOM.";
	return CTRL_CMD_ERROR;
}

static int get_rate_ctr_group_idx(const struct rate_ctr_group *ctrg, int intv, struct ctrl_cmd *cmd)
{
	char *counters;

	counters = get_all_rate_ctr_in_group(cmd, ctrg, intv);
	if (!counters)
		goto oom;

	cmd->reply = talloc_asprintf(cmd, "All counters in %s.%u%s",
			ctrg->desc->group_name_prefix, ctrg->idx, counters);
	talloc_free(counters);
	if (!cmd->reply)
		goto oom;

	return CTRL_CMD_REPLY;
oom:
	cmd->reply = "OOM.";
	return CTRL_CMD_ERROR;
}

/* rate_ctr */
CTRL_CMD_DEFINE(rate_ctr, "rate_ctr *");
static int get_rate_ctr(struct ctrl_cmd *cmd, void *data)
{
	int intv;
	unsigned int idx;
	char *ctr_group, *ctr_idx, *ctr_name, *tmp, *dup, *saveptr, *interval;
	struct rate_ctr_group *ctrg;
	const struct rate_ctr *ctr;

	dup = talloc_strdup(cmd, cmd->variable);
	if (!dup)
		goto oom;

	/* Skip over possible prefixes (net.) */
	tmp = strstr(dup, "rate_ctr");
	if (!tmp) {
		talloc_free(dup);
		cmd->reply = "rate_ctr not a token in rate_ctr command!";
		goto err;
	}

	strtok_r(tmp, ".", &saveptr);
	interval = strtok_r(NULL, ".", &saveptr);
	if (!interval) {
		talloc_free(dup);
		cmd->reply = "Missing interval.";
		goto err;
	}

	if (!strcmp(interval, "abs")) {
		intv = -1;
	} else if (!strcmp(interval, "per_sec")) {
		intv = RATE_CTR_INTV_SEC;
	} else if (!strcmp(interval, "per_min")) {
		intv = RATE_CTR_INTV_MIN;
	} else if (!strcmp(interval, "per_hour")) {
		intv = RATE_CTR_INTV_HOUR;
	} else if (!strcmp(interval, "per_day")) {
		intv = RATE_CTR_INTV_DAY;
	} else {
		talloc_free(dup);
		cmd->reply = "Wrong interval.";
		goto err;
	}

	ctr_group = strtok_r(NULL, ".", &saveptr);
	tmp = strtok_r(NULL, ".", &saveptr);
	if (!ctr_group || !tmp) {
		talloc_free(dup);
		cmd->reply = "Counter group must be of form a.b";
		goto err;
	}
	ctr_group[strlen(ctr_group)] = '.';

	ctr_idx = strtok_r(NULL, ".", &saveptr);
	if (!ctr_idx) {
		talloc_free(dup);
		return get_rate_ctr_group(ctr_group, intv, cmd);
	}
	idx = atoi(ctr_idx);

	ctrg = rate_ctr_get_group_by_name_idx(ctr_group, idx);
	if (!ctrg) {
		talloc_free(dup);
		cmd->reply = "Counter group not found.";
		goto err;
	}

	ctr_name = strtok_r(NULL, "\0", &saveptr);
	if (!ctr_name) {
		talloc_free(dup);
		return get_rate_ctr_group_idx(ctrg, intv, cmd);
	}

	ctr = rate_ctr_get_by_name(ctrg, ctr_name);
	if (!ctr) {
		cmd->reply = "Counter name not found.";
		talloc_free(dup);
		goto err;
	}

	talloc_free(dup);

	cmd->reply = talloc_asprintf(cmd, "%"PRIu64, get_rate_ctr_value(ctr, intv));
	if (!cmd->reply)
		goto oom;

	return CTRL_CMD_REPLY;
oom:
	cmd->reply = "OOM";
err:
	return CTRL_CMD_ERROR;
}

static int set_rate_ctr(struct ctrl_cmd *cmd, void *data)
{
	cmd->reply = "Can't set rate counter.";

	return CTRL_CMD_ERROR;
}

static int verify_rate_ctr(struct ctrl_cmd *cmd, const char *value, void *data)
{
	return 0;
}

/* counter */
CTRL_CMD_DEFINE(counter, "counter *");
static int get_counter(struct ctrl_cmd *cmd, void *data)
{
	char *ctr_name, *tmp, *dup, *saveptr;
	struct osmo_counter *counter;

	cmd->reply = "OOM";
	dup = talloc_strdup(cmd, cmd->variable);
	if (!dup)
		goto err;


	tmp = strstr(dup, "counter");
	if (!tmp) {
		talloc_free(dup);
		goto err;
	}

	strtok_r(tmp, ".", &saveptr);
	ctr_name = strtok_r(NULL, "\0", &saveptr);

	if (!ctr_name)
		goto err;

	counter = osmo_counter_get_by_name(ctr_name);
	if (!counter) {
		cmd->reply = "Counter name not found.";
		talloc_free(dup);
		goto err;
	}

	talloc_free(dup);

	cmd->reply = talloc_asprintf(cmd, "%lu", counter->value);
	if (!cmd->reply) {
		cmd->reply = "OOM";
		goto err;
	}

	return CTRL_CMD_REPLY;
err:
	return CTRL_CMD_ERROR;
}

static int set_counter(struct ctrl_cmd *cmd, void *data)
{

	cmd->reply = "Can't set counter.";

	return CTRL_CMD_ERROR;
}

static int verify_counter(struct ctrl_cmd *cmd, const char *value, void *data)
{
	return 0;
}

struct ctrl_handle *ctrl_interface_setup(void *data, uint16_t port,
					 ctrl_cmd_lookup lookup)
{
	return ctrl_interface_setup_dynip(data, "127.0.0.1", port, lookup);
}

struct ctrl_handle *ctrl_interface_setup_dynip(void *data,
					       const char *bind_addr,
					       uint16_t port,
					       ctrl_cmd_lookup lookup)
{
	int ret;
	struct ctrl_handle *ctrl;

	ctrl = talloc_zero(data, struct ctrl_handle);
	if (!ctrl)
		return NULL;

	INIT_LLIST_HEAD(&ctrl->ccon_list);

	ctrl->data = data;
	ctrl->lookup = lookup;

	ctrl_node_vec = vector_init(5);
	if (!ctrl_node_vec)
		goto err;

	/* Listen for control connections */
	ctrl->listen_fd.cb = listen_fd_cb;
	ctrl->listen_fd.data = ctrl;
	ret = osmo_sock_init_ofd(&ctrl->listen_fd, AF_INET, SOCK_STREAM, IPPROTO_TCP,
				 bind_addr, port, OSMO_SOCK_F_BIND);
	if (ret < 0)
		goto err_vec;

	ret = ctrl_cmd_install(CTRL_NODE_ROOT, &cmd_rate_ctr);
	if (ret)
		goto err_vec;
	ret = ctrl_cmd_install(CTRL_NODE_ROOT, &cmd_counter);
	if (ret)
		goto err_vec;

	return ctrl;
err_vec:
	vector_free(ctrl_node_vec);
	ctrl_node_vec = NULL;
err:
	talloc_free(ctrl);
	return NULL;
}
