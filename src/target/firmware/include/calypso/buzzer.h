#ifndef _CAL_BUZZER_H
#define _CAL_BUZZER_H

#define NOTE(n,oct) (n<<2 | (oct & 0x03))

#define NOTE_E 0x00
#define NOTE_DIS 0x01
#define NOTE_D 0x02
#define NOTE_CIS 0x03
#define NOTE_C 0x04
#define NOTE_H 0x05
#define NOTE_AIS 0x06
#define NOTE_A 0x07
#define NOTE_GIS 0x08
#define NOTE_G 0x09
#define NOTE_FIS 0x0A
#define NOTE_F 0x0B

#define OCTAVE_5 OCTAVE(0x00)
#define OCTAVE_4 OCTAVE(0x01)
#define OCTAVE_3 OCTAVE(0x02)
#define OCTAVE_2 OCTAVE(0x03)
#define OCTAVE_1 OCTAVE(0x04)

#define OCTAVE(m) (m>NOTE_C?m+1:m)

/* Switch buzzer to PWT mode (or back) */
void buzzer_mode_pwt(int on);
/* Set the buzzer level */
void buzzer_volume(uint8_t level);
/* Set the buzzer note */
void buzzer_note(uint8_t note);

#endif /* _CAL_BUZZER_H */
