/*
 * (C) 2008 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#include <stdio.h>

#include <osmocom/core/timer.h>
#include <osmocom/core/select.h>

#include "../../config.h"

static void timer_fired(void *data);

static struct osmo_timer_list timer_one = {
    .cb = timer_fired,
    .data = (void*)1,
};

static struct osmo_timer_list timer_two = {
    .cb = timer_fired,
    .data = (void*)2,
};

static struct osmo_timer_list timer_three = {
    .cb = timer_fired,
    .data = (void*)3,
};

static void timer_fired(void *_data)
{
    unsigned long data = (unsigned long) _data;
    printf("Fired timer: %lu\n", data);

    if (data == 1) {
        osmo_timer_schedule(&timer_one, 3, 0);
        osmo_timer_del(&timer_two);
    } else if (data == 2) {
        printf("Should not be fired... bug in del_timer\n");
    } else if (data == 3) {
        printf("Timer fired not registering again\n");
    } else  {
        printf("wtf... wrong data\n");
    }
}

int main(int argc, char** argv)
{
    printf("Starting... timer\n");

    osmo_timer_schedule(&timer_one, 3, 0);
    osmo_timer_schedule(&timer_two, 5, 0);
    osmo_timer_schedule(&timer_three, 4, 0);

#ifdef HAVE_SYS_SELECT_H
    while (1) {
        osmo_select_main(0);
    }
#else
    printf("Select not supported on this platform!\n");
#endif
}
