#ifndef _CTORS_H
#define _CTORS_H

/* iterate over list of constructor functions and call each element */
void do_global_ctors(const char *ctors_start, const char *ctors_end);

#endif
