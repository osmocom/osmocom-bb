/* global variables for the TIFFS reader code - extern declarations */

extern uint32_t tiffs_base_addr;
extern uint32_t tiffs_sector_size;
extern unsigned tiffs_nsectors;

extern struct tiffs_inode *tiffs_active_index;
extern int tiffs_root_ino;
extern int tiffs_init_done;
