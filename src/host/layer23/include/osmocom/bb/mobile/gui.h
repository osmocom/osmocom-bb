#ifndef _gui_h
#define _gui_h

#include <osmocom/bb/ui/ui.h>

struct status_screen {
	const char		*feature;
	const char		*feature_vty;
	const char		*feature_help;
	int			default_en;
	int			lines;
	int	 		(*display_func)(struct osmocom_ms *ms,
								char *text);
};

extern struct status_screen status_screen[];

#define GUI_NUM_STATUS	9	/* number of status infos */
#define GUI_NUM_STATUS_LINES 32	/* total number of lines of all status infos */

struct gsm_ui {
	struct ui_inst ui;	/* user interface instance */
	int menu;		/* current menu */
	const char *status_lines[GUI_NUM_STATUS_LINES];
				/* list of status lines */
	char status_text[GUI_NUM_STATUS_LINES * (UI_COLS + 1) + 128];
				/* memory for status lines (extra 128 bytes
				 * in case of overflow */
	struct osmo_timer_list timer;
				/* refresh timer */
	char dialing[33];	/* dailing buffer */
	int selected_call;	/* call that is selected */

	/* select menus */
	void *select_menu;	/* current menu */
	void *choose_menu;	/* current choose item in menu */

	/* supserv */
	int ss_lines;		/* number of lines we display */
	int ss_pending, ss_active;
				/* state of transaction */
};

void gui_init_status_config(void);
int gui_start(struct osmocom_ms *ms);
int gui_stop(struct osmocom_ms *ms);
int gui_notify_call(struct osmocom_ms *ms);
int gui_notify_ss(struct osmocom_ms *ms, const char *fmt, ...);

#endif /* _gui_h */
