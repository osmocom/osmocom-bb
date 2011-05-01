#ifndef _LINUX_LLIST_H
#define _LINUX_LLIST_H

#include <stddef.h>

#ifndef inline
#define inline __inline__
#endif

static inline void prefetch(__attribute__((unused)) const void *x) {;}

/**
 * container_of - cast a member of a structure out to the containing structure
 *
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({			\
        const typeof( ((type *)0)->member ) *__mptr = (typeof( ((type *)0)->member ) *)(ptr);	\
        (type *)( (char *)__mptr - offsetof(type, member) );})


/*
 * These are non-NULL pointers that will result in page faults
 * under normal circumstances, used to verify that nobody uses
 * non-initialized llist entries.
 */
#define LLIST_POISON1  ((void *) 0x00100100)
#define LLIST_POISON2  ((void *) 0x00200200)

/*
 * Simple doubly linked llist implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole llists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

struct llist_head {
	struct llist_head *next, *prev;
};

#define LLIST_HEAD_INIT(name) { &(name), &(name) }

#define LLIST_HEAD(name) \
	struct llist_head name = LLIST_HEAD_INIT(name)

#define INIT_LLIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/*
 * Insert a new entry between two known consecutive entries. 
 *
 * This is only for internal llist manipulation where we know
 * the prev/next entries already!
 */
static inline void __llist_add(struct llist_head *_new,
			      struct llist_head *prev,
			      struct llist_head *next)
{
	next->prev = _new;
	_new->next = next;
	_new->prev = prev;
	prev->next = _new;
}

/**
 * llist_add - add a new entry
 * @new: new entry to be added
 * @head: llist head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void llist_add(struct llist_head *_new, struct llist_head *head)
{
	__llist_add(_new, head, head->next);
}

/**
 * llist_add_tail - add a new entry
 * @new: new entry to be added
 * @head: llist head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void llist_add_tail(struct llist_head *_new, struct llist_head *head)
{
	__llist_add(_new, head->prev, head);
}

/*
 * Delete a llist entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal llist manipulation where we know
 * the prev/next entries already!
 */
static inline void __llist_del(struct llist_head * prev, struct llist_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * llist_del - deletes entry from llist.
 * @entry: the element to delete from the llist.
 * Note: llist_empty on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void llist_del(struct llist_head *entry)
{
	__llist_del(entry->prev, entry->next);
	entry->next = (struct llist_head *)LLIST_POISON1;
	entry->prev = (struct llist_head *)LLIST_POISON2;
}

/**
 * llist_del_init - deletes entry from llist and reinitialize it.
 * @entry: the element to delete from the llist.
 */
static inline void llist_del_init(struct llist_head *entry)
{
	__llist_del(entry->prev, entry->next);
	INIT_LLIST_HEAD(entry); 
}

/**
 * llist_move - delete from one llist and add as another's head
 * @llist: the entry to move
 * @head: the head that will precede our entry
 */
static inline void llist_move(struct llist_head *llist, struct llist_head *head)
{
        __llist_del(llist->prev, llist->next);
        llist_add(llist, head);
}

/**
 * llist_move_tail - delete from one llist and add as another's tail
 * @llist: the entry to move
 * @head: the head that will follow our entry
 */
static inline void llist_move_tail(struct llist_head *llist,
				  struct llist_head *head)
{
        __llist_del(llist->prev, llist->next);
        llist_add_tail(llist, head);
}

/**
 * llist_empty - tests whether a llist is empty
 * @head: the llist to test.
 */
static inline int llist_empty(const struct llist_head *head)
{
	return head->next == head;
}

static inline void __llist_splice(struct llist_head *llist,
				 struct llist_head *head)
{
	struct llist_head *first = llist->next;
	struct llist_head *last = llist->prev;
	struct llist_head *at = head->next;

	first->prev = head;
	head->next = first;

	last->next = at;
	at->prev = last;
}

/**
 * llist_splice - join two llists
 * @llist: the new llist to add.
 * @head: the place to add it in the first llist.
 */
static inline void llist_splice(struct llist_head *llist, struct llist_head *head)
{
	if (!llist_empty(llist))
		__llist_splice(llist, head);
}

/**
 * llist_splice_init - join two llists and reinitialise the emptied llist.
 * @llist: the new llist to add.
 * @head: the place to add it in the first llist.
 *
 * The llist at @llist is reinitialised
 */
static inline void llist_splice_init(struct llist_head *llist,
				    struct llist_head *head)
{
	if (!llist_empty(llist)) {
		__llist_splice(llist, head);
		INIT_LLIST_HEAD(llist);
	}
}

/**
 * llist_entry - get the struct for this entry
 * @ptr:	the &struct llist_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the llist_struct within the struct.
 */
#define llist_entry(ptr, type, member) \
	container_of(ptr, type, member)

/**
 * llist_for_each	-	iterate over a llist
 * @pos:	the &struct llist_head to use as a loop counter.
 * @head:	the head for your llist.
 */
#define llist_for_each(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
        	pos = pos->next, prefetch(pos->next))

/**
 * __llist_for_each	-	iterate over a llist
 * @pos:	the &struct llist_head to use as a loop counter.
 * @head:	the head for your llist.
 *
 * This variant differs from llist_for_each() in that it's the
 * simplest possible llist iteration code, no prefetching is done.
 * Use this for code that knows the llist to be very short (empty
 * or 1 entry) most of the time.
 */
#define __llist_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * llist_for_each_prev	-	iterate over a llist backwards
 * @pos:	the &struct llist_head to use as a loop counter.
 * @head:	the head for your llist.
 */
#define llist_for_each_prev(pos, head) \
	for (pos = (head)->prev, prefetch(pos->prev); pos != (head); \
        	pos = pos->prev, prefetch(pos->prev))
        	
/**
 * llist_for_each_safe	-	iterate over a llist safe against removal of llist entry
 * @pos:	the &struct llist_head to use as a loop counter.
 * @n:		another &struct llist_head to use as temporary storage
 * @head:	the head for your llist.
 */
#define llist_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * llist_for_each_entry	-	iterate over llist of given type
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your llist.
 * @member:	the name of the llist_struct within the struct.
 */
#define llist_for_each_entry(pos, head, member)				\
	for (pos = llist_entry((head)->next, typeof(*pos), member),	\
		     prefetch(pos->member.next);			\
	     &pos->member != (head); 					\
	     pos = llist_entry(pos->member.next, typeof(*pos), member),	\
		     prefetch(pos->member.next))

/**
 * llist_for_each_entry_reverse - iterate backwards over llist of given type.
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your llist.
 * @member:	the name of the llist_struct within the struct.
 */
#define llist_for_each_entry_reverse(pos, head, member)			\
	for (pos = llist_entry((head)->prev, typeof(*pos), member),	\
		     prefetch(pos->member.prev);			\
	     &pos->member != (head); 					\
	     pos = llist_entry(pos->member.prev, typeof(*pos), member),	\
		     prefetch(pos->member.prev))

/**
 * llist_for_each_entry_continue -	iterate over llist of given type
 *			continuing after existing point
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your llist.
 * @member:	the name of the llist_struct within the struct.
 */
#define llist_for_each_entry_continue(pos, head, member) 		\
	for (pos = llist_entry(pos->member.next, typeof(*pos), member),	\
		     prefetch(pos->member.next);			\
	     &pos->member != (head);					\
	     pos = llist_entry(pos->member.next, typeof(*pos), member),	\
		     prefetch(pos->member.next))

/**
 * llist_for_each_entry_safe - iterate over llist of given type safe against removal of llist entry
 * @pos:	the type * to use as a loop counter.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your llist.
 * @member:	the name of the llist_struct within the struct.
 */
#define llist_for_each_entry_safe(pos, n, head, member)			\
	for (pos = llist_entry((head)->next, typeof(*pos), member),	\
		n = llist_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = llist_entry(n->member.next, typeof(*n), member))

/**
 * llist_for_each_rcu	-	iterate over an rcu-protected llist
 * @pos:	the &struct llist_head to use as a loop counter.
 * @head:	the head for your llist.
 */
#define llist_for_each_rcu(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
        	pos = pos->next, ({ smp_read_barrier_depends(); 0;}), prefetch(pos->next))
        	
#define __llist_for_each_rcu(pos, head) \
	for (pos = (head)->next; pos != (head); \
        	pos = pos->next, ({ smp_read_barrier_depends(); 0;}))
        	
/**
 * llist_for_each_safe_rcu	-	iterate over an rcu-protected llist safe
 *					against removal of llist entry
 * @pos:	the &struct llist_head to use as a loop counter.
 * @n:		another &struct llist_head to use as temporary storage
 * @head:	the head for your llist.
 */
#define llist_for_each_safe_rcu(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, ({ smp_read_barrier_depends(); 0;}), n = pos->next)

/**
 * llist_for_each_entry_rcu	-	iterate over rcu llist of given type
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your llist.
 * @member:	the name of the llist_struct within the struct.
 */
#define llist_for_each_entry_rcu(pos, head, member)			\
	for (pos = llist_entry((head)->next, typeof(*pos), member),	\
		     prefetch(pos->member.next);			\
	     &pos->member != (head); 					\
	     pos = llist_entry(pos->member.next, typeof(*pos), member),	\
		     ({ smp_read_barrier_depends(); 0;}),		\
		     prefetch(pos->member.next))


/**
 * llist_for_each_continue_rcu	-	iterate over an rcu-protected llist 
 *			continuing after existing point.
 * @pos:	the &struct llist_head to use as a loop counter.
 * @head:	the head for your llist.
 */
#define llist_for_each_continue_rcu(pos, head) \
	for ((pos) = (pos)->next, prefetch((pos)->next); (pos) != (head); \
        	(pos) = (pos)->next, ({ smp_read_barrier_depends(); 0;}), prefetch((pos)->next))


#endif
