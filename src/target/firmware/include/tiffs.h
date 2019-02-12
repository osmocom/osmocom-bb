/*
 * The external API for the TIFFS reader library (libtiffs).
 *
 * For the file reading functions, the return code is 0 if the file wasn't
 * found or there is no valid FFS (no error msg printed), 1 if the file was
 * found and read successfully, or negative if some other error(s) occurred
 * (error msg printed internally).
 */

#define INODE_TO_DATAPTR(i) \
	((uint8_t *)tiffs_base_addr + ((i)->dataptr << 4))

#define	TIFFS_OBJTYPE_FILE	0xF1
#define	TIFFS_OBJTYPE_DIR	0xF2
#define	TIFFS_OBJTYPE_SEGMENT	0xF4

struct tiffs_inode {
	uint16_t	len;
	uint8_t		reserved1;
	uint8_t		type;
	uint16_t	descend;
	uint16_t	sibling;
	uint32_t	dataptr;
	uint16_t	sequence;
	uint16_t	updates;
};

int tiffs_init(uint32_t base_addr, uint32_t sector_size, unsigned nsectors);

int tiffs_read_file_maxlen(const char *pathname, uint8_t *buf,
			   size_t maxlen, size_t *lenrtn);
int tiffs_read_file_fixedlen(const char *pathname, uint8_t *buf,
			     size_t expect_len);
