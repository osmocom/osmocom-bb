; DSP Sniffing task patch

;
; (C) 2010 by Sylvain Munaut <tnt@246tNt.com>
; (C) 2011 by Steve Markgraf <steve@steve-m.de>
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
; MCSI (Multi-Channel Serial Interface)
; ----------------------------------------------------------------------------

	; MCSI registers
MCSI_CONTROL_REG	.equ	0x800
MCSI_MAIN_PARAM_REG	.equ	0x801
MCSI_CLOCK_REG		.equ	0x805
MCSI_STATUS_REG		.equ	0x806
MCSI_TX0_REG		.equ	0x820

	; MCSI STATUS_REG bits
MCSI_TX_READY		.equ	0x10
MCSI_TX_UNFLOW		.equ	0x20

; ----------------------------------------------------------------------------
; Our double buffer API
; ----------------------------------------------------------------------------

			.section .apiram

sniff_db0		.ds	138
sniff_db1		.ds	138
sniff_db_ptr		.ds	1
sniff_burst_ptr		.ds	1

	; Variables for MCSI
mcsi_tmp			.ds	1
mcsi_dat			.ds	1

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
; MCSI clear status
;
;  Clears the MCSI status register, needs AR1 to be set to point to a temp word
;
mcsi_clear_status:
			; The MCSI_TX_READY bit is cleared with a 1
			st	#MCSI_TX_READY, *AR1
			portw	*AR1, MCSI_STATUS_REG
			ret

;
; MCSI initialisation
;
;  Initializes and enables the MCSI
;
mcsi_init:
			stm	#mcsi_tmp, AR1

			; Use a clock divider of 2 -> 13MHz/2 = 6.5MHz
			st	#2, *AR1
			portw	*AR1, MCSI_CLOCK_REG

			; Set MCSI parameters: 16 bit framesize, master mode
			st	#0x4F, *AR1
			portw	*AR1, MCSI_MAIN_PARAM_REG

			; Enable MCSI clock
			st	#1, *AR1
			portw	*AR1, MCSI_CONTROL_REG

			; Done
			ret

;
; MCSI send word
;
;  Sends a word of 16 bits over the MCSI and increases the source address
;  Parameter: the address of the word has to be stored in AR4
;
mcsi_send_word:
			stm	#mcsi_tmp, AR1
			call	mcsi_clear_status

			; perform XOR LRC
			xor	*AR4, B

			; Write TX word and increase address
			portw	*AR4+, MCSI_TX0_REG

			; Loop until TX_READY bit of MCSI_STATUS_REG
			; has been set (happens around the 4th transmitted bit)
1:
			portr	MCSI_STATUS_REG, *AR1
			bitf	*AR1, #MCSI_TX_READY
			bc	1b, ntc

			; Done
			ret

;
; MCSI send burst
;
;  Sends a burst of A words, beginning at the address of AR4
;  Parameters: the number of words to send in A, start-address in AR4
;
mcsi_send_burst:
			; Loop over all words
1:
			call	mcsi_send_word
			sub	#1, A
			bc	1b, aneq

			; Done
			ret

;
; MCSI disable
;
;  waits until the last word has been sent and disables the MCSI
;
mcsi_disable:
			stm	#mcsi_tmp, AR1

1:			; Loop until TX_UNFLOW bit of MCSI_STATUS_REG
			; has been set (can only be reset with SW_RESET)
			portr	MCSI_STATUS_REG, *AR1
			bitf	*AR1, #MCSI_TX_UNFLOW
			bc	1b, ntc

			; disable MCSI clock and set SW_RESET
			st	#2, *AR1
			portw	*AR1, MCSI_CONTROL_REG

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
			; Initialize MCSI
			call	mcsi_init

			; Initialize B used for XOR LRC
			ld	#0, B

			; Send magic string, stored in ndb param section
			stm	#0x0C39, AR4		; d_hole2_param[1]
			ld	#4, A			; 4 words
			call	mcsi_send_burst

			; Store address of I/Q data
			stm	#0x0CCE, AR4

			; Send the NB I/Q data over MCSI
			ld	#302, A			; 151 samples, 302 words
			call	mcsi_send_burst

			; Store and send XOR LRC
			stm	#mcsi_dat, AR4
			stl	B, *AR4
			call	mcsi_send_word

			; Disable MCSI
			call	mcsi_disable

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

			; Backup the old burst begin address
			mvdm	@sniff_burst_ptr, AR4

			; Store the new pointer
			mvmd	AR3, @sniff_burst_ptr

			; Initialize MCSI
			call	mcsi_init

			; Initialize B used for XOR LRC
			ld	#0, B

			; Send the demodulated burst data over MCSI
			ld	#34, A			; 5 + 29 words
			call	mcsi_send_burst

			stm	#mcsi_dat, AR4

			; Store address of a_a5fn[1] @ DSP page 0
			stm	#0x080C, AR4

			; Check DSP page
			bitf	*(dsp_page), #1
			bc	1f, ntc

			; Store address of a_a5fn[1] @ DSP page 1
			stm	#0x0820, AR4
1:
			; Send FN and d_ctrl_system via MCSI (contains the TSC)
			call	mcsi_send_word		; send a_a5fn[0]
			call	mcsi_send_word		; send a_a5fn[1]
			addm	#2, AR4			; AR4 = 0x0810/0x0824
			call	mcsi_send_word		; send d_ctrl_system

			; Store and send XOR LRC
			stm	#mcsi_dat, AR4
			stl	B, *AR4
			call	mcsi_send_word

			; Disable MCSI
			call	mcsi_disable

			; Increment received bursts count
			mvdm	@sniff_db_ptr, AR1
			nop				; (pipeline conflict)
			nop				; (pipeline conflict)
			addm	#1, *AR1(1)

			; Done
			ret
