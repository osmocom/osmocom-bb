#ifndef _OSMO_BACKTRACE_H_
#define _OSMO_BACKTRACE_H_

void osmo_generate_backtrace(void);
void osmo_log_backtrace(int subsys, int level);

#endif
