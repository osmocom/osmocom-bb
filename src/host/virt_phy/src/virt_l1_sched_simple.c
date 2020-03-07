/* (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
 * (C) 2017 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <virtphy/virt_l1_sched.h>
#include <osmocom/core/linuxlist.h>
#include <virtphy/virt_l1_model.h>
#include <virtphy/logging.h>
#include <time.h>
#include <talloc.h>

/**
 * @brief Start scheduler thread based on current gsm time from model
 */
static int virt_l1_sched_start(struct l1_model_ms *ms, struct gsm_time time)
{
	virt_l1_sched_sync_time(ms, time, 1);
	return 0;
}

/**
 * @brief Clear scheduler queue and completely restart scheduler.
 */
int virt_l1_sched_restart(struct l1_model_ms *ms, struct gsm_time time)
{
	virt_l1_sched_stop(ms);
	return virt_l1_sched_start(ms, time);
}

/**
 * @brief Sync scheduler with given time.
 */
void virt_l1_sched_sync_time(struct l1_model_ms *ms, struct gsm_time time, uint8_t hard_reset)
{
	ms->state.current_time = time;
}

/**
 * @brief Stop the scheduler thread and cleanup mframe items queue
 */
void virt_l1_sched_stop(struct l1_model_ms *ms)
{
	struct virt_l1_sched_mframe_item *mi_next, *mi_tmp;

	/* Empty tdma and mframe sched items lists */
	llist_for_each_entry_safe(mi_next, mi_tmp, &ms->state.sched.mframe_items, mframe_item_entry) {
		struct virt_l1_sched_tdma_item *ti_next, *ti_tmp;

		llist_for_each_entry_safe(ti_next, ti_tmp, &mi_next->tdma_item_list, tdma_item_entry) {
			talloc_free(ti_next->msg);
			llist_del(&ti_next->tdma_item_entry);
		}
		llist_del(&mi_next->mframe_item_entry);
		talloc_free(mi_next);
	}
}

/**
 * @brief Handle all pending scheduled items for the current frame number.
 */
void virt_l1_sched_execute(struct l1_model_ms *ms, uint32_t fn)
{
	struct l1_state_ms *l1s = &ms->state;
	struct virt_l1_sched_mframe_item *mi_next, *mi_tmp;
	uint8_t hyperframe_restart = fn < l1s->sched.last_exec_fn;

	llist_for_each_entry_safe(mi_next, mi_tmp, &l1s->sched.mframe_items, mframe_item_entry) {
		/* execute all registered handler for current mf sched item */
		uint8_t exec_now = mi_next->fn <= fn || (hyperframe_restart && mi_next->fn > l1s->sched.last_exec_fn);
		/* break loop, as we have an ordered list in case the hyperframe had not been reset */
		uint8_t break_now = mi_next->fn > fn && !hyperframe_restart;

		if (exec_now) {
			struct virt_l1_sched_tdma_item *ti_next, *ti_tmp;
			/* run through all scheduled tdma sched items for that frame number */
			llist_for_each_entry_safe(ti_next, ti_tmp, &mi_next->tdma_item_list,
						  tdma_item_entry) {
				/* exec tdma sched item's handler callback */
				/* TODO: we do not have a TDMA scheduler currently and execute
				 * all scheduled tdma items here at once */
				ti_next->handler_cb(ms, mi_next->fn, ti_next->ts, ti_next->msg);
				/* remove handled tdma sched item */
				llist_del(&ti_next->tdma_item_entry);
				talloc_free(ti_next);
			}
			/* remove handled mframe sched item */
			llist_del(&mi_next->mframe_item_entry);
			talloc_free(mi_next);
		}

		if (break_now)
			break;
	}
	l1s->sched.last_exec_fn = fn;
}

/**
 * @brief Schedule a msg to the given framenumber and timeslot.
 */
void virt_l1_sched_schedule(struct l1_model_ms *ms, struct msgb *msg, uint32_t fn, uint8_t ts,
                            virt_l1_sched_cb *handler_cb)
{
	struct virt_l1_sched_mframe_item *mi_next = NULL, *mi_tmp = NULL, *mi_fn = NULL;
	struct virt_l1_sched_tdma_item *ti_new = NULL;

	llist_for_each_entry_safe(mi_next, mi_tmp, &ms->state.sched.mframe_items, mframe_item_entry) {
		if (mi_next->fn == fn) {
			mi_fn = mi_next;
			break;
		} else if (mi_next->fn > fn)
			break;
	}
	if (!mi_fn) {
		/* list did not contain mframe item with needed fn */
		mi_fn = talloc_zero(ms, struct virt_l1_sched_mframe_item);
		mi_fn->fn = fn;
		INIT_LLIST_HEAD(&mi_fn->tdma_item_list);
		llist_add_tail(&mi_fn->mframe_item_entry, &mi_next->mframe_item_entry);
	}

	ti_new = talloc_zero(mi_fn, struct virt_l1_sched_tdma_item);
	ti_new->msg = msg;
	ti_new->handler_cb = handler_cb;
	ti_new->ts = ts;
	/* simply add at end, no ordering for tdma sched items currently */
	llist_add_tail(&ti_new->tdma_item_entry, &mi_fn->tdma_item_list);
	/* TODO: ordered insert needed if tdma scheduler should be implemented */
}
