#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

/* asm-arm/linkage.h */

#define __ALIGN .align 0
#define __ALIGN_STR ".align 0"

/* linux/linkage.h */

#define ALIGN __ALIGN

#define ENTRY(name) \
  .globl name; \
  ALIGN; \
  name:

#endif
