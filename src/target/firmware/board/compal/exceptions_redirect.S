
.section .text.exceptions
_undef_instr:
	ldr pc, _vec_undef_instr
_sw_interr:
	ldr pc, _vec_sw_interr
_prefetch_abort:
	ldr pc, _vec_prefetch_abort
_data_abort:
	ldr pc, _vec_data_abort
_reserved:
	ldr pc, _vec_reserved
_irq:
	ldr pc, _vec_irq
_fiq:
	ldr pc, _vec_fiq

_vec_undef_instr:		.word(0x80001c)
_vec_sw_interr:			.word(0x800020)
_vec_prefetch_abort:	.word(0x800024)
_vec_data_abort:		.word(0x800028)
_vec_reserved:			.word(0x80002c)
_vec_irq:				.word(0x800030)
_vec_fiq:				.word(0x800034)
