#ifndef _LIBUI_TELNET_IF_H
#define _LIBUI_TELNET_IF_H

struct ui_telnet_connection {
	struct llist_head entry;
	void *priv;
	struct osmo_fd fd;
	struct ui_inst *ui;
	struct buffer *obuf;
	int iac, sb, esc;
};

int ui_telnet_init(struct ui_inst *ui, void *tall_ctx, int port);
int ui_telnet_puts(struct ui_inst *ui, const char *text);
void ui_telnet_exit(struct ui_inst *ui);

#endif /* _LIBUI_TELNET_IF_H */
