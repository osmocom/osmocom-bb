/* global variables for the TIFFS reader code */

#include <stdint.h>
#include "globals.h"

uint32_t tiffs_base_addr;
uint32_t tiffs_sector_size;
unsigned tiffs_nsectors;

struct tiffs_inode *tiffs_active_index;
int tiffs_root_ino;
int tiffs_init_done;
