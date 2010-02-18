
BCSR		.equ	0x29

CMD_IDLE	.equ	1		; Do nothing / DSP ready for commands
CMD_COPY_BLOCK	.equ	2		; (if size == 0, then exec)
CMD_COPY_MODE	.equ	4		; Select copy mode
					; (0=code write, 1=data write,
					;  2=code read,  3=data read,
					;  4=prom read,  5=drom read)
CMD_VERSION	.equ	0xffff		; API_RAM[0] = bootloader version

VERSION		.equ	0x0100		; 1.00


		.section .apiram

		.org	0x07fc
bl_addr_hi	.ds	1
bl_size		.ds	1
bl_addr_lo	.ds	1
bl_status	.ds	1


		.text
		.mmregs
_start:
		orm     #2, *(BCSR)	; ?

		ld	#0x1f, DP
		stm	#0x1100, SP
		stm	#0, AR4
_done:
		stm	#_api_ram, AR2
		st	#CMD_IDLE, @bl_status
_loop:
		; Version
		cmpm	@bl_status, #CMD_VERSION
		bc	1f, ntc

		bd	_done
		st	#VERSION, *AR2
1:

		; Select copy mode
		cmpm	@bl_status, #CMD_COPY_MODE
		bc	1f, ntc

		bd	_done
		mvdm	@_api_ram, AR4
1:

		; Copy
		cmpm	@bl_status, #CMD_COPY_BLOCK
		bc	_loop, ntc

		;   Capture values for copy operations
		;     A = full address
		;     AR1 size-1
		;     AR2 api_ram (set previously)
		;     AR3 data/code address
		;     AR4 mode

		ldu	@bl_addr_lo, A
		stlm	A, AR3
		add	@bl_addr_hi, 16, A

		ldu	@bl_size, B
		stlm	B, AR1
		; mar	*AR1-	; We do this in a delay slot later on ...

			; Start
		bc	1f, bneq	; B still contains size
		bacc	A

1:
			; Select
		stm	#AR4, AR5	; AR5 = &AR4
		bit	*AR5, 13	; Test mode(2)
		bcd	_read_rom, tc
		mar	*AR1-
		bit	*AR5, 15	; Test mode(0) lsb
		bcd	_copy_data, tc
		bit	*AR5, 14	; Test mode(1)
		nop

			; Copy to/from Program space
_copy_prog:
		bc	_read_prog, tc

				; Copy from API -> prog space	(mode 0)
_write_prog:
		rpt	*(AR1)
		writa	*AR2+
		b	_done

				; Copy from prog space -> API	(mode 2)
_read_prog:
		rpt	*(AR1)
		reada	*AR2+
		b	_done

			; Copy to/from Data space
_copy_data:
		bc	_read_data, tc

				; Copy from API -> data space	(mode 1)
_write_data:
		rpt	*(AR1)
		mvdd	*AR2+, *AR3+
		b	_done

				; Copy from data space -> API	(mode 3)
_read_data:
		rpt	*(AR1)
		mvdd	*AR3+, *AR2+
		b	_done

			; Read from {D,P}ROM bypassing protection
_read_rom:
		ldm	AR1, B	; Can't put those two ops in the delay slot of
		stlm	B, BRC	; 'bc' because of unprotected pipeline conflicts
		bc	_read_rom_data, tc

_read_rom_prog:
		rptb	1f - 1
		call	prom_read_xplt
1:
		b	_done

_read_rom_data:
		rptb	1f - 1
		call	drom_read_xplt
1:
		b	_done


drom_read_xplt	.equ	0xe4b8
prom_read_xplt	.equ	0x7213


		.end

