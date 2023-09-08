/* VGCS/VBS call control */
/*
 * (C) 2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * SPDX-License-Identifier: AGPL-3.0+
 *
 * Author: Andreas Eversberg
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#define GSM44068_ALLOC_SIZE        2048
#define GSM44068_ALLOC_HEADROOM    256

static inline struct msgb *gsm44068_msgb_alloc_name(const char *name)
{
	return msgb_alloc_headroom(GSM44068_ALLOC_SIZE, GSM44068_ALLOC_HEADROOM, name);
}

int gsm44068_gcc_init(struct osmocom_ms *ms);
int gsm44068_gcc_exit(struct osmocom_ms *ms);
int gsm44068_rcv_gcc_bcc(struct osmocom_ms *ms, struct msgb *msg);
int gsm44068_rcv_mm_idle(struct osmocom_ms *ms);
struct gsm_trans *trans_find_ongoing_gcc_bcc(struct osmocom_ms *ms);
int gcc_bcc_call(struct osmocom_ms *ms, uint8_t protocol, const char *number);
int gcc_leave(struct osmocom_ms *ms);
int gcc_bcc_hangup(struct osmocom_ms *ms);
int gcc_talk(struct osmocom_ms *ms);
int gcc_listen(struct osmocom_ms *ms);
int gsm44068_dump_calls(struct osmocom_ms *ms, void (*print)(void *, const char *, ...), void *priv);
