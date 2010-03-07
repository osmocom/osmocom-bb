
#ifndef _DEFINES_H
#define _DEFINES_H

/* type properties */
#define __packed     __attribute__((packed))
#define __aligned(alignment) __attribute__((aligned(alignment)))

/* linkage */
#define __section(name) __attribute__((section(name)))

/* force placement in zero-waitstate memory */
/* XXX: these are placeholders */
#define __fast_text
#define __fast_data
#define __fast_bss

#endif /* !_DEFINES_H */
