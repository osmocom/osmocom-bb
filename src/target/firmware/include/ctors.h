#ifndef _CTORS_H
#define _CTORS_H

#if 0
/* only supported by gcc 3.4 or later */
#define __ctor_data	__attribute__ ((constructor) (100))
#define __ctor_board	__attribute__ ((constructor) (200))
#else
#define __ctor_data	__attribute__ ((constructor))
#define __ctor_board	__attribute__ ((constructor))
#endif

/* iterate over list of constructor functions and call each element */
void do_global_ctors(const char *ctors_start, const char *ctors_end);

#endif
