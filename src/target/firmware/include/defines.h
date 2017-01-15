
#ifndef _DEFINES_H
#define _DEFINES_H

#define __attribute_const__	__attribute__((__const__))

/* type properties */
#ifndef __packed
#define __packed		__attribute__((packed))
#endif

#ifndef __aligned
#define __aligned(alignment)	__attribute__((aligned(alignment)))
#endif

#ifndef __unused
#define __unused		__attribute__((unused))
#endif

/* linkage */
#ifndef __section
#define __section(name) __attribute__((section(name)))
#endif

/* force placement in zero-waitstate memory */
#define __ramtext __section(".ramtext")

#endif /* !_DEFINES_H */
