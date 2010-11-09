#ifndef _MTK_BPI_H
#define _MTK_BPI_H

/* MTK Baseband Parallel Interface */

/* Chapter 9.2 of MT6235 Data Sheet */

#define BPI_BUF(n)	(BPI_BUF0 + ((n) * 4))

#define MTK_BPI(n)	(n)

enum mtk_bpi_reg {
	BPI_CON		= 0x0000,
	BPI_BUF0	= 0x0004,
	BPI_ENA0	= 0x00b0,
	BPI_ENA1	= 0x00b4,
	BPI_ENA2	= 0x00b8,
};

#endif /* _MTK_BPI_H */
