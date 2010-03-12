#ifndef _SSD1783_H
#define _SSD1783_H

/* Some basic colors */
#define RED		0x0f00
#define GREEN		0x00f0
#define BLUE		0x000f
#define YELLOW		0x0ff0
#define MAGENTA		0x0f0f
#define CYAN		0x00ff
#define BLACK		0x0000
#define WHITE		0x0fff

/* Epson S1D15G10D08B000 commandset */
#define	CMD_DISON	0xaf	// Display on
#define	CMD_DISOFF	0xae	// Display off
#define	CMD_DISNOR	0xa6	// Normal display
#define	CMD_DISINV	0xa7	// Inverse display
#define	CMD_COMSCN	0xbb	// Common scan direction
#define	CMD_DISCTL	0xca	// Display control
#define	CMD_SLPIN	0x95	// Sleep in
#define	CMD_SLPOUT	0x94	// Sleep out
#define	CMD_PASET	0x75	// Page address set
#define	CMD_CASET	0x15	// Column address set
#define	CMD_DATCTL	0xbc	// Data scan direction, etc.
#define	CMD_RGBSET8	0xce	// 256-color position set
#define	CMD_RAMWR	0x5c	// Writing to memory
#define	CMD_RAMRD	0x5d	// Reading from memory
#define	CMD_PTLIN	0xa8	// Partial display in
#define	CMD_PTLOUT	0xa9	// Partial display out
#define	CMD_RMWIN	0xe0	// Read and modify write
#define	CMD_RMWOUT	0xee	// End
#define	CMD_ASCSE	0xaa	// Area scroll set
#define	CMD_SCSTART	0xab	// Scroll start set
#define	CMD_OSCON	0xd1	// Internal oscillation on
#define	CMD_OSCOFF	0xd2	// Internal oscillation off
#define	CMD_PWRCTR	0x20	// Power control
#define	CMD_VOLCTR	0x81	// Electronic volume control
#define	CMD_VOLUP	0xd6	// Increment electronic control by 1
#define	CMD_VOLDOWN	0xd7	// Decrement electronic control by 1
#define	CMD_TMPGRD	0x82	// Temperature gradient set
#define	CMD_EPCTIN	0xcd	// Control EEPROM
#define	CMD_EPCOUT	0xcc	// Cancel EEPROM control
#define	CMD_EPMWR	0xfc	// Write into EEPROM
#define	CMD_EPMRD	0xfd	// Read from EEPROM
#define	CMD_EPSRRD1	0x7c	// Read register 1
#define	CMD_EPSRRD2	0x7d	// Read register 2
#define	CMD_NOP		0x25	// NOP instruction

/* Extended SSD1783 commandset, partly (also has HW graphic functionalities) */
#define	CMD_BIASSET	0xfb	// Set bias ratio
#define	CMD_FREQSET	0xf2	// Set frequency and n-line inversion
#define	CMD_RESCMD	0xa2	// reserved command
#define	CMD_PWMSEL	0xf7	// Select PWM/FRC, Full/8 color mode

#endif
