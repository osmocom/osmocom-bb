#ifndef _CALYPSO_TPU_H
#define _CALYPSO_TPU_H

#define BITS_PER_TDMA		1250
#define QBITS_PER_TDMA		(BITS_PER_TDMA * 4)	/* 5000 */
#define TPU_RANGE		QBITS_PER_TDMA
#define	SWITCH_TIME		(TPU_RANGE-10)

/* Assert or de-assert TPU reset */
void tpu_reset(int active);
/* Enable or Disable a new scenario loaded into the TPU */
void tpu_enable(int active);
/* Enable or Disable the clock of the TPU Module */
void tpu_clk_enable(int active);
/* Enable Frame Interrupt generation on next frame.  DSP will reset it */
void tpu_dsp_frameirq_enable(void);
/* Is a Frame interrupt still pending for the DSP ? */
int tpu_dsp_fameirq_pending(void);
/* Rewind the TPU, i.e. restart enqueueing instructions at the base addr */
void tpu_rewind(void);
/* Enqueue a raw TPU instruction */
void tpu_enqueue(uint16_t instr);
/* Initialize TPU and TPU driver */
void tpu_init(void);
/* (Busy)Wait until TPU is idle */
void tpu_wait_idle(void);
/* Enable FRAME interrupt generation */
void tpu_frame_irq_en(int mcu, int dsp);
/* Force the generation of a DSP interrupt */
void tpu_force_dsp_frame_irq(void);

/* Get the current TPU SYNCHRO register */
uint16_t tpu_get_synchro(void);
/* Get the current TPU OFFSET register */
uint16_t tpu_get_offset(void);

enum tpu_instr {
	TPU_INSTR_AT		= (1 << 13),
	TPU_INSTR_OFFSET	= (2 << 13),
	TPU_INSTR_SYNCHRO	= (3 << 13),	/* Loading delta synchro value in TPU synchro register */
	TPU_INSTR_WAIT		= (5 << 13),	/* Wait a certain period (in GSM qbits) */
	TPU_INSTR_SLEEP		= (0 << 13),	/* Stop the sequencer by disabling TPU ENABLE bit in ctrl reg */
	/* data processing */
	TPU_INSTR_MOVE		= (4 << 13),
};

/* Addresses internal to the TPU, only accessible via MOVE */
enum tpu_reg_int {
	TPUI_TSP_CTRL1	= 0x00,
	TPUI_TSP_CTRL2	= 0x01,
	TPUI_TX_1	= 0x04,
	TPUI_TX_2	= 0x03,
	TPUI_TX_3	= 0x02,
	TPUI_TX_4	= 0x05,
	TPUI_TSP_ACT_L	= 0x06,
	TPUI_TSP_ACT_U	= 0x07,
	TPUI_TSP_SET1	= 0x09,
	TPUI_TSP_SET2	= 0x0a,
	TPUI_TSP_SET3	= 0x0b,
	TPUI_DSP_INT_PG	= 0x10,
	TPUI_GAUGING_EN = 0x11,
};

enum tpui_ctrl2_bits {
	TPUI_CTRL2_RD		= (1 << 0),
	TPUI_CTRL2_WR		= (1 << 1),
};

static inline uint16_t tpu_mod5000(int16_t time)
{
	if (time < 0)
		return time + 5000;
	if (time >= 5000)
		return time - 5000;
	return time;
}

/* Enqueue a SLEEP operation (stop sequencer by disabling TPU ENABLE bit) */
static inline void tpu_enq_sleep(void)
{
	tpu_enqueue(TPU_INSTR_SLEEP);
}

/* Enqueue a MOVE operation */
static inline void tpu_enq_move(uint8_t addr, uint8_t data)
{
	tpu_enqueue(TPU_INSTR_MOVE | (data << 5) | (addr & 0x1f));
}

/* Enqueue an AT operation */
static inline void tpu_enq_at(int16_t time)
{
	tpu_enqueue(TPU_INSTR_AT | tpu_mod5000(time));
}

/* Enqueue a SYNC operation */
static inline void tpu_enq_sync(int16_t time)
{
	tpu_enqueue(TPU_INSTR_SYNCHRO | time);
}

/* Enqueue a WAIT operation */
static inline void tpu_enq_wait(int16_t time)
{
	tpu_enqueue(TPU_INSTR_WAIT | time);
}

/* Enqueue an OFFSET operation */
static inline void tpu_enq_offset(int16_t time)
{
	tpu_enqueue(TPU_INSTR_OFFSET | time);
}

static inline void tpu_enq_dsp_irq(void)
{
	tpu_enq_move(TPUI_DSP_INT_PG, 0x0001);
}

/* add two numbers, modulo 5000, and ensure the result is positive */
uint16_t add_mod5000(int16_t a, int16_t b);

#endif /* _CALYPSO_TPU_H */
