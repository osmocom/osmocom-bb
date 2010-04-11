
/* Exception Vectors like they are needed for the exception vector
   indirection of the internal boot ROM.  The following section must be liked
   to appear at 0x80001c */
.section .text.exceptions
_undef_instr:
	b	handle_abort
_sw_interr:
	b	_sw_interr
_prefetch_abort:
	b	handle_abort
_data_abort:
	b	handle_abort
_reserved:
	b	_reserved
_irq:
	b	irq_entry
_fiq:
	b	fiq_entry

