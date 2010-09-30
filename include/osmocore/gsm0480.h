#ifndef gsm0480_h
#define gsm0480_h

#include "msgb.h"

struct msgb *gsm0480_create_unstructuredSS_Notify(int alertPattern, const char *text);
struct msgb *gsm0480_create_notifySS(const char *text);

int gsm0480_wrap_invoke(struct msgb *msg, int op, int link_id);
int gsm0480_wrap_facility(struct msgb *msg);

#endif
