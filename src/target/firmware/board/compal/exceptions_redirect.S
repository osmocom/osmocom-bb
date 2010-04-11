
/* XXX this should dispatch via 0x80001c */
		
.section .text.exceptions
_undef_instr:
	b _undef_instr
_sw_interr:
	b _sw_interr
_prefetch_abort:
	b _prefetch_abort
_data_abort:
	b _data_abort
_reserved:
	b _reserved
_irq:
	b _irq
_fiq:
	b _fiq
