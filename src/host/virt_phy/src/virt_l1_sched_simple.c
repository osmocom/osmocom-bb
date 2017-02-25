#include <virtphy/virt_l1_sched.h>
#include <osmocom/core/linuxlist.h>
#include <virtphy/virt_l1_model.h>
#include <virtphy/logging.h>
#include <time.h>
#include <talloc.h>

static struct l1_model_ms *l1_model_ms = NULL;

static LLIST_HEAD(mframe_item_list);

/**
 * @brief Initialize schedulers data structures.
 */
void virt_l1_sched_init(struct l1_model_ms * model)
{
	l1_model_ms = model;
}

/**
 * @brief Clear scheduler queue and completely restart scheduler.
 */
int virt_l1_sched_restart(struct gsm_time time)
{
	virt_l1_sched_stop();
	return virt_l1_sched_start(time);
}

/**
 * @brief Start scheduler thread based on current gsm time from model
 */
int virt_l1_sched_start(struct gsm_time time)
{
	virt_l1_sched_sync_time(time, 1);
	return 0;
}

/**
 * @brief Sync scheduler with given time.
 */
void virt_l1_sched_sync_time(struct gsm_time time, uint8_t hard_reset)
{
	l1_model_ms->state->current_time = time;
}

/**
 * @brief Stop the scheduler thread and cleanup mframe items queue
 */
void virt_l1_sched_stop()
{
	struct virt_l1_sched_mframe_item *mi_next, *mi_tmp;

	/* Empty tdma and mframe sched items lists */
	llist_for_each_entry_safe(mi_next, mi_tmp, &mframe_item_list, mframe_item_entry)
	{
		struct virt_l1_sched_tdma_item *ti_next, *ti_tmp;
		llist_for_each_entry_safe(ti_next, ti_tmp, &mi_next->tdma_item_list, tdma_item_entry)
		{
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
void virt_l1_sched_execute(uint32_t fn)
{
	struct virt_l1_sched_mframe_item *mi_next, *mi_tmp;
	// FIXME: change of hyperframe and thus restarting fn at 0 may cause messages in the queue that are never handled
	llist_for_each_entry_safe(mi_next, mi_tmp, &mframe_item_list, mframe_item_entry)
	{
		if (mi_next->fn <= fn) {
			struct virt_l1_sched_tdma_item *ti_next, *ti_tmp;
			// run through all scheduled tdma sched items for that frame number
			llist_for_each_entry_safe(ti_next, ti_tmp, &mi_next->tdma_item_list, tdma_item_entry)
			{
				// exec tdma sched item's handler callback
				// TODO: we do not have a tdma scheduler currently and execute alle scheduled tdma items here at once
				ti_next->handler_cb(ti_next->msg);
				// remove handled tdma sched item
				llist_del(&ti_next->tdma_item_entry);
			}
			// remove handled mframe sched item
			llist_del(&mi_next->mframe_item_entry);
			talloc_free(mi_next);
		} else if (mi_next->fn > fn) {
			/* break the loop as our list is ordered */
			break;
		}
	}
}

/**
 * @brief Schedule a msg to the given framenumber and timeslot.
 */
void virt_l1_sched_schedule(struct msgb * msg, uint32_t fn, uint8_t ts,
                            virt_l1_sched_cb * handler_cb)
{
	struct virt_l1_sched_mframe_item *mi_next = NULL, *mi_tmp = NULL,
	                *mi_fn = NULL;
	struct virt_l1_sched_tdma_item *ti_new = NULL;

	llist_for_each_entry_safe(mi_next, mi_tmp, &mframe_item_list, mframe_item_entry)
	{
		if (mi_next->fn == fn) {
			mi_fn = mi_next;
			break;
		} else if (mi_next->fn > fn) {
			break;
		}
	}
	if (!mi_fn) {
		// list did not contain mframe item with needed fn
		mi_fn = talloc_zero(NULL, struct virt_l1_sched_mframe_item);
		mi_fn->fn = fn;
		// need to manually init the struct content.... no so happy
		mi_fn->tdma_item_list.prev = &mi_fn->tdma_item_list;
		mi_fn->tdma_item_list.next = &mi_fn->tdma_item_list;

		// TODO: check if we get an error if list is empty...
		llist_add(&mi_fn->mframe_item_entry,
		          mi_next->mframe_item_entry.prev);

	}
	ti_new = talloc_zero(mi_fn, struct virt_l1_sched_tdma_item);
	ti_new->msg = msg;
	ti_new->handler_cb = handler_cb;
	ti_new->ts = ts;
	// simply add at end, no ordering for tdma sched items currently
	llist_add_tail(&ti_new->tdma_item_entry, &mi_fn->tdma_item_list); // TODO: ordered insert needed if tdma scheduler should be implemented
}
