; DSP Sniffing task patch

;
; (C) 2010 by Sylvain Munaut <tnt@246tNt.com>
;
; All Rights Reserved
;
; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 2 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License along
; with this program; if not, write to the Free Software Foundation, Inc.,
; 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
;

; ----------------------------------------------------------------------------
; Known symbols
; ----------------------------------------------------------------------------

	; Variables
patch_install_fptr	.equ	0x3F6B		; Patch install function ptr
dsp_page		.equ	0x3FB0		; Current ndb.d_dsp_page
task_fn_entry		.equ	0x4387 + 23	; Task 23 index in JT_4387

	; Functions
a5_setup		.equ	0xB12C
dma_queue_setup		.equ	0xB74C

jt4387_exec		.equ	0xA9EA

fq_4320_push		.equ	0xAA9F
fq_4330_push		.equ	0xAA6C
fq_4340_push		.equ	0xAAC3


; ----------------------------------------------------------------------------
; Our double buffer API
; ----------------------------------------------------------------------------

			.section .apiram

sniff_db0		.ds	138
sniff_db1		.ds	138
sniff_db_ptr		.ds	1
sniff_burst_ptr		.ds	1


; ----------------------------------------------------------------------------
; The code itself
; ----------------------------------------------------------------------------

			.text

;
; Patch init
;
;  Called during DSP boot (around the middle)
;
patch_init:
			st	#patch_install, *(patch_install_fptr)
			ret

;
; Patch install
;
;  Called after DSP init. That's were the overriding of all the
;  jump tables should be done.
;
patch_install:
			st	#sniff_task, *(task_fn_entry)
			ret

;
; New sniff task
;
;  Called by the dispatch code when the value 23 is found in d_task_d
;
sniff_task:
			; Setup our double buffer zone ptr
			stm	#sniff_db0, AR1
			bitf	*(dsp_page), #1
			bc	1f, ntc
			stm	#sniff_db1, AR1
1:

			mvmd	AR1, @sniff_db_ptr

			; Prepare the burst_ptr and burst_counter
				; sniff_db_ptr->r_nb = 0
			st	#0, *+AR1

				; sniff_burst_ptr = sniff_db_ptr + 2;
			mar	*AR1+
			mvmd	AR1, @sniff_burst_ptr

			; Queue A5 setup in FQ4340
			; (needed to make sure the a5 bits are zeroed)
			ld	#a5_setup, 0, A
			call	fq_4340_push

			; Prepare bursts reception
			; (we queue as many many as bursts to RX)
1:
				; Decrement & Check counter
			mvdm	@sniff_db_ptr, AR1
			nop				; (pipeline conflict)
			nop				; (pipeline conflict)

			ldu	*AR1, A
			bc	2f, aeq
			sub	#1, A
			stl	A, *AR1

				; Queue the DMA
			call	dma_queue_setup

				; Queue Burst handler in FQ4320
			ld	#burst_handler, 0, A
			call	fq_4320_push

				; Loop
			b	1b
2:

			; Done
			ret

;
; Burst data handler
;
;  Called once the DMA transfer is done and the IQ bits are received.
;  Most maintenance tasks (like cleanup after DMA and inth stuff) are
;  done for us. Only real work goes here.
;
burst_handler:
			; NB demodulation
			ld	#0x34, A
			call	jt4387_exec

			; Base burst storage address
			mvdm	@sniff_burst_ptr, AR3
			nop				; (pipeline conflict)
			nop				; (pipeline conflict)

			; Copy "metadata"
			mvkd	@0x3FA4, *AR3+		; D_TOA
			mvkd	@0x3FA5, *AR3+		; D_PM
			mvkd	@0x3FA7, *AR3+		; D_ANGLE
			mvkd	@0x3FA6, *AR3+		; D_SNR
			mvkd	@0x0CCE, *AR3+		; dummy burst indicator

			; Copy the softbits
			stm	#0x0CCF, AR2		; src
			stm	#28, AR1		; size-1  (29 words = 116 bits)
			rpt	*(AR1)
			mvdd	*AR2+, *AR3+

			; Store the new pointer
			mvmd	AR3, @sniff_burst_ptr

			; Increment received bursts count
			mvdm	@sniff_db_ptr, AR1
			nop				; (pipeline conflict)
			nop				; (pipeline conflict)
			addm	#1, *AR1(1)

			; Done
			ret
