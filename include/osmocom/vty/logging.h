#ifndef _VTY_LOGGING_H
#define _VTY_LOGGING_H

#define LOGGING_STR	"Configure log message to this terminal\n"
#define FILTER_STR	"Filter log messages\n"

void logging_vty_add_cmds(void);

struct log_target *osmo_log_vty2tgt(struct vty *vty);

#endif /* _VTY_LOGGING_H */
