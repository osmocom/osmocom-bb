/* Calypso integrated DSP boot code */

#define _SA_DECL (const uint16_t *)&(const uint16_t [])

/* We don't really need any DSP boot code, it happily works with its own ROM */
static const struct dsp_section *dsp_bootcode = NULL;

#undef _SA_DECL

