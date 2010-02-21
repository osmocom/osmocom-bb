#ifndef _CAL_DSP_API_H
#define _CAL_DSP_API_H

/* This is a header file with structures imported from the TSM30 source code (l1_defty.h)
 *
 * As this header file only is a list of definitions and data structures, it is
 * not ocnsidered to be a copyrightable work itself.
 *
 * Nonetheless, it might be good to rewrite it (without ugly typedefs!) */

#if(L1_DYN_DSP_DWNLD == 1)
  #include "l1_dyn_dwl_defty.h"
#endif

/* Include a header file that defines everything this l1_defty.h needs */
#include "l1_environment.h"

#define BASE_API_NDB		0xFFD001A8L	/* 268 words */
#define BASE_API_PARAM		0xFFD00862L	/* 57 words */
#define BASE_API_R_PAGE_0	0xFFD00050L	/* 20 words */
#define BASE_API_R_PAGE_1	0xFFD00078L	/* 20 words */
#define BASE_API_W_PAGE_0	0xFFD00000L	/* 20 words */
#define BASE_API_W_PAGE_1	0xFFD00028L	/* 20 words */


/***********************************************************/
/*                                                         */
/*  Data structure for global info components.             */
/*                                                         */
/***********************************************************/

typedef struct
{
  API d_task_d;           // (0) Downlink task command.
  API d_burst_d;          // (1) Downlink burst identifier.
  API d_task_u;           // (2) Uplink task command.
  API d_burst_u;          // (3) Uplink burst identifier.
  API d_task_md;          // (4) Downlink Monitoring (FB/SB) command.
#if (DSP == 33) || (DSP == 34) || (DSP == 35) || (DSP == 36)
  API d_background;       // (5) Background tasks
#else
  API d_reserved;         // (5) Reserved
#endif
  API d_debug;            // (6) Debug/Acknowledge/general purpose word.
  API d_task_ra;          // (7) RA task command.
  API d_fn;               // (8)  FN, in Rep. period and FN%104, used for TRAFFIC/TCH only.
                             //        bit [0..7]  -> b_fn_report, FN in the normalized reporting period.
                             //        bit [8..15] -> b_fn_sid,    FN % 104, used for SID positionning.
  API d_ctrl_tch;         // (9)  Tch channel description.
                             //        bit [0..3]  -> b_chan_mode,    channel  mode.
                             //        bit [4..5]  -> b_chan_type,    channel type.
                             //        bit [6]     -> reset SACCH
                             //        bit [7]     -> vocoder ON
                             //        bit [8]     -> b_sync_tch_ul,  synchro. TCH/UL.
                             //        bit [9]     -> b_sync_tch_dl,  synchro. TCH/DL.
                             //        bit [10]    -> b_stop_tch_ul,  stop TCH/UL.
                             //        bit [11]    -> b_stop_tch_dl,  stop TCH/DL.
                             //        bit [12.13] -> b_tch_loop,     tch loops A/B/C.
  API hole;               // (10) unused hole.

#if ((ANLG_FAM == 1) || (ANLG_FAM == 2) || (ANLG_FAM == 3))
  API d_ctrl_abb;         // (11) Bit field indicating the analog baseband register to send.
                             //        bit [0]     -> b_ramp: the ramp information(a_ramp[]) is located in NDB
                             //        bit [1.2]   -> unused
                             //        bit [3]     -> b_apcdel: delays-register in NDB
                             //        bit [4]     -> b_afc: freq control register in DB
                             //        bit [5..15] -> unused
#endif
  API a_a5fn[2];          // (12..13) Encryption Frame number.
                             //        word 0, bit [0..4]  -> T2.
                             //        word 0, bit [5..10] -> T3.
                             //        word 1, bit [0..11] -> T1.
  API d_power_ctl;        // (14) Power level control.
  API d_afc;              // (15) AFC value (enabled by "b_afc" in "d_ctrl_TCM4400 or in d_ctrl_abb").
  API d_ctrl_system;      // (16) Controle Register for RESET/RESUME.
                             //        bit [0..2] -> b_tsq,           training sequence.
                             //        bit [3]    -> b_bcch_freq_ind, BCCH frequency indication.
                             //        bit [15]   -> b_task_abort,    DSP task abort command.
}
T_DB_MCU_TO_DSP;

typedef struct
{
  API d_task_d;           // (0) Downlink task command.
  API d_burst_d;          // (1) Downlink burst identifier.
  API d_task_u;           // (2) Uplink task command.
  API d_burst_u;          // (3) Uplink burst identifier.
  API d_task_md;          // (4) Downlink Monitoring (FB/SB) task command.
#if (DSP == 33) || (DSP == 34) || (DSP == 35) || (DSP == 36)
  API d_background;       // (5) Background tasks
#else
  API d_reserved;         // (5) Reserved
#endif
  API d_debug;            // (6) Debug/Acknowledge/general purpose word.
  API d_task_ra;          // (7) RA task command.

#if (DSP == 33) || (DSP == 34) || (DSP == 35) || (DSP == 36)
  API a_serv_demod[4];    // ( 8..11) Serv. cell demod. result, array of 4 words (D_TOA,D_PM,D_ANGLE,D_SNR).
  API a_pm[3];            // (12..14) Power measurement results, array of 3 words.
  API a_sch[5];           // (15..19) Header + SB information, array of  5 words.
#else
  API a_pm[3];            // ( 8..10) Power measurement results, array of 3 words.
  API a_serv_demod[4];    // (11..14) Serv. cell demod. result, array of 4 words (D_TOA,D_PM,D_ANGLE,D_SNR).
  API a_sch[5];           // (15..19) Header + SB information, array of  5 words.
#endif
}
T_DB_DSP_TO_MCU;

#if (DSP == 34) || (DSP == 35) || (DSP == 36) // NDB GSM
  typedef struct
  {
    // MISC Tasks
    API d_dsp_page;

    // DSP status returned (DSP --> MCU).
    API d_error_status;

    // RIF control (MCU -> DSP).
    API d_spcx_rif;

    API d_tch_mode;  // TCH mode register.
                     // bit [0..1]  -> b_dai_mode.
                     // bit [2]     -> b_dtx.

    API d_debug1;                // bit 0 at 1 enable dsp f_tx delay for Omega

    API d_dsp_test;

    // Words dedicated to Software version (DSP code + Patch)
    API d_version_number1;
    API d_version_number2;

    API d_debug_ptr;
    API d_debug_bk;

    API d_pll_config;

    // GSM/GPRS DSP Debug trace support
    API p_debug_buffer;
    API d_debug_buffer_size;
    API d_debug_trace_type;

    #if (W_A_DSP_IDLE3 == 1)
      // DSP report its state: 0 run, 1 Idle1, 2 Idle2, 3 Idle3.
      API d_dsp_state;
      // 5 words are reserved for any possible mapping modification
      API d_hole1_ndb[2];
    #else
      // 6 words are reserved for any possible mapping modification
      API d_hole1_ndb[3];
    #endif

    #if (AMR == 1)
      API p_debug_amr;
    #else
      API d_hole_debug_amr;
    #endif

    #if (CHIPSET == 12)
      #if (DSP == 35) || (DSP == 36)
        API d_hole2_ndb[1];
        API d_mcsi_select;
      #else
        API d_hole2_ndb[2];
      #endif
    #else
      API d_hole2_ndb[2];
    #endif

    // New words APCDEL1 and APCDEL2 for 2TX: TX/PRACH combinations
    API d_apcdel1_bis;
    API d_apcdel2_bis;


    // New registers due to IOTA analog base band
    API d_apcdel2;
    API d_vbctrl2;
    API d_bulgcal;

    // Analog Based Band
    API d_afcctladd;

    API d_vbuctrl;
    API d_vbdctrl;
    API d_apcdel1;
    API d_apcoff;
    API d_bulioff;
    API d_bulqoff;
    API d_dai_onoff;
    API d_auxdac;

  #if (ANLG_FAM == 1)
    API d_vbctrl;
  #elif ((ANLG_FAM == 2) || (ANLG_FAM == 3))
    API d_vbctrl1;
  #endif
  
    API d_bbctrl;

    // Monitoring tasks control (MCU <- DSP)
    // FB task
    API d_fb_det;           // FB detection result. (1 for FOUND).
    API d_fb_mode;          // Mode for FB detection algorithm.
    API a_sync_demod[4];    // FB/SB demod. result, (D_TOA,D_PM,D_ANGLE,D_SNR).

    // SB Task
    API a_sch26[5];         // Header + SB information, array of  5 words.

    API d_audio_gain_ul;
    API d_audio_gain_dl;

    // Controller of the melody E2 audio compressor
    API d_audio_compressor_ctrl;

    // AUDIO module
    API d_audio_init;
    API d_audio_status;

    // Audio tasks
    // TONES (MCU -> DSP)
    API d_toneskb_init;
    API d_toneskb_status;
    API d_k_x1_t0;
    API d_k_x1_t1;
    API d_k_x1_t2;
    API d_pe_rep;
    API d_pe_off;
    API d_se_off;
    API d_bu_off;
    API d_t0_on;
    API d_t0_off;
    API d_t1_on;
    API d_t1_off;
    API d_t2_on;
    API d_t2_off;
    API d_k_x1_kt0;
    API d_k_x1_kt1;
    API d_dur_kb;
    API d_shiftdl;
    API d_shiftul;

    API d_aec_ctrl;

    API d_es_level_api;
    API d_mu_api;

    // Melody Ringer module
    API d_melo_osc_used;
    API d_melo_osc_active;
    API a_melo_note0[4];
    API a_melo_note1[4];
    API a_melo_note2[4];
    API a_melo_note3[4];
    API a_melo_note4[4];
    API a_melo_note5[4];
    API a_melo_note6[4];
    API a_melo_note7[4];

    // selection of the melody format
    API d_melody_selection;

    // Holes due to the format melody E1
    API a_melo_holes[3];

    // Speech Recognition module
    API d_sr_status;          // status of the DSP speech reco task
    API d_sr_param;           // paramters for the DSP speech reco task: OOV threshold.
    API d_sr_bit_exact_test;  // bit exact test
    API d_sr_nb_words;        // number of words used in the speech recognition task
    API d_sr_db_level;        // estimate voice level in dB
    API d_sr_db_noise;        // estimate noise in dB
    API d_sr_mod_size;        // size of the model
    API a_n_best_words[4];  // array of the 4 best words
    API a_n_best_score[8];  // array of the 4 best scores (each score is 32 bits length)

    // Audio buffer
    API a_dd_1[22];         // Header + DATA traffic downlink information, sub. chan. 1.
    API a_du_1[22];         // Header + DATA traffic uplink information, sub. chan. 1.

    // V42bis module
    API d_v42b_nego0;
    API d_v42b_nego1;
    API d_v42b_control;
    API d_v42b_ratio_ind;
    API d_mcu_control;
    API d_mcu_control_sema;

    // Background tasks
    API d_background_enable;
    API d_background_abort;
    API d_background_state;
    API d_max_background;
    API a_background_tasks[16];
    API a_back_task_io[16];

    // GEA module defined in l1p_deft.h (the following section is overlaid with GPRS NDB memory)
    API d_gea_mode_ovly;
    API a_gea_kc_ovly[4];

#if (ANLG_FAM == 3)
    // SYREN specific registers
    API d_vbpop;
    API d_vau_delay_init;
    API d_vaud_cfg;
    API d_vauo_onoff;
    API d_vaus_vol;
    API d_vaud_pll;
    API d_hole3_ndb[1];
#elif ((ANLG_FAM == 1) || (ANLG_FAM == 2))

    API d_hole3_ndb[7];

#endif

    // word used for the init of USF threshold
    API d_thr_usf_detect;

    // Encryption module
    API d_a5mode;           // Encryption Mode.

    API d_sched_mode_gprs_ovly;

    // 7 words are reserved for any possible mapping modification
    API d_hole4_ndb[5];

    // Ramp definition for Omega device
    API a_ramp[16];

    // CCCH/SACCH downlink information...(!!)
    API a_cd[15];           // Header + CCCH/SACCH downlink information.

    // FACCH downlink information........(!!)
    API a_fd[15];           // Header + FACCH downlink information.

    // Traffic downlink data frames......(!!)
    API a_dd_0[22];         // Header + DATA traffic downlink information, sub. chan. 0.

    // CCCH/SACCH uplink information.....(!!)
    API a_cu[15];           // Header + CCCH/SACCH uplink information.

    // FACCH downlink information........(!!)
    API a_fu[15];           // Header + FACCH uplink information

    // Traffic downlink data frames......(!!)
    API a_du_0[22];         // Header + DATA traffic uplink information, sub. chan. 0.

    // Random access.....................(MCU -> DSP).
    API d_rach;             // RACH information.

    //...................................(MCU -> DSP).
    API a_kc[4];            // Encryption Key Code.

    // Integrated Data Services module
    API d_ra_conf;
    API d_ra_act;
    API d_ra_test;
    API d_ra_statu;
    API d_ra_statd;
    API d_fax;
    API a_data_buf_ul[21];
    API a_data_buf_dl[37];

  // GTT API mapping for DSP code 34 (for test only)
  #if (L1_GTT == 1)
    API d_tty_status;
    API d_tty_detect_thres;
    API d_ctm_detect_shift;
    API d_tty_fa_thres;
    API d_tty_mod_norm;
    API d_tty_reset_buffer_ul;
    API d_tty_loop_ctrl;
    API p_tty_loop_buffer;
  #else
    API a_tty_holes[8];
  #endif

    API a_sr_holes0[414];

  #if (L1_NEW_AEC)
    // new AEC
    API d_cont_filter;
    API d_granularity_att;
    API d_coef_smooth;
    API d_es_level_max;
    API d_fact_vad;
    API d_thrs_abs;
    API d_fact_asd_fil;
    API d_fact_asd_mut;
    API d_far_end_pow_h;
    API d_far_end_pow_l;
    API d_far_end_noise_h;
    API d_far_end_noise_l;
  #else
    API a_new_aec_holes[12];
  #endif // L1_NEW_AEC

    // Speech recognition model
    API a_sr_holes1[145];
    API d_cport_init;
    API d_cport_ctrl;
    API a_cport_cfr[2];
    API d_cport_tcl_tadt;
    API d_cport_tdat;
    API d_cport_tvs;
    API d_cport_status;
    API d_cport_reg_value;

    API a_cport_holes[1011];

    API a_model[1041];

    // EOTD buffer
#if (L1_EOTD==1)
    API d_eotd_first;
    API d_eotd_max;
    API d_eotd_nrj_high;
    API d_eotd_nrj_low;
    API a_eotd_crosscor[18];
#else
    API a_eotd_holes[22];
#endif
    // AMR ver 1.0 buffers
    API a_amr_config[4];
    API a_ratscch_ul[6];
    API a_ratscch_dl[6];
    API d_amr_snr_est; // estimation of the SNR of the AMR speech block
  #if (L1_VOICE_MEMO_AMR)
    API d_amms_ul_voc;
  #else
    API a_voice_memo_amr_holes[1];
  #endif
    API d_thr_onset_afs;     // thresh detection ONSET AFS
    API d_thr_sid_first_afs; // thresh detection SID_FIRST AFS
    API d_thr_ratscch_afs;   // thresh detection RATSCCH AFS
    API d_thr_update_afs;    // thresh detection SID_UPDATE AFS
    API d_thr_onset_ahs;     // thresh detection ONSET AHS
    API d_thr_sid_ahs;       // thresh detection SID frames AHS
    API d_thr_ratscch_marker;// thresh detection RATSCCH MARKER
    API d_thr_sp_dgr;   // thresh detection SPEECH DEGRADED/NO_DATA
    API d_thr_soft_bits;   
    #if (MELODY_E2)
      API d_melody_e2_osc_stop;
      API d_melody_e2_osc_active;
      API d_melody_e2_semaphore;
      API a_melody_e2_osc[16][3];
      API d_melody_e2_globaltimefactor;
      API a_melody_e2_instrument_ptr[8];
      API d_melody_e2_deltatime;

      #if (AMR_THRESHOLDS_WORKAROUND)
        API a_d_macc_thr_afs[8];
        API a_d_macc_thr_ahs[6];
      #else
        API a_melody_e2_holes0[14];
      #endif

      API a_melody_e2_holes1[693];
      API a_dsp_trace[SC_AUDIO_MELODY_E2_MAX_SIZE_OF_DSP_TRACE];
      API a_melody_e2_instrument_wave[SC_AUDIO_MELODY_E2_MAX_SIZE_OF_INSTRUMENT];
    #else
      API d_holes[61];
      #if (AMR_THRESHOLDS_WORKAROUND)
        API a_d_macc_thr_afs[8];
        API a_d_macc_thr_ahs[6];
      #endif
    #endif

  }
  T_NDB_MCU_DSP;
#elif (DSP == 33) // NDB GSM
  typedef struct
  {
    // MISC Tasks
    API d_dsp_page;

    // DSP status returned (DSP --> MCU).
    API d_error_status;

    // RIF control (MCU -> DSP).
    API d_spcx_rif;

    API d_tch_mode;  // TCH mode register.
                     // bit [0..1]  -> b_dai_mode.
                     // bit [2]     -> b_dtx.

    API d_debug1;                // bit 0 at 1 enable dsp f_tx delay for Omega

    API d_dsp_test;

    // Words dedicated to Software version (DSP code + Patch)
    API d_version_number1;
    API d_version_number2;

    API d_debug_ptr;
    API d_debug_bk;

    API d_pll_config;

    // GSM/GPRS DSP Debug trace support
    API p_debug_buffer;
    API d_debug_buffer_size;
    API d_debug_trace_type;

    #if (W_A_DSP_IDLE3 == 1)
      // DSP report its state: 0 run, 1 Idle1, 2 Idle2, 3 Idle3.
      API d_dsp_state;
      // 10 words are reserved for any possible mapping modification
      API d_hole1_ndb[5];
    #else
      // 11 words are reserved for any possible mapping modification
      API d_hole1_ndb[6];
    #endif

    // New words APCDEL1 and APCDEL2 for 2TX: TX/PRACH combinations
    API d_apcdel1_bis;
    API d_apcdel2_bis;


    // New registers due to IOTA analog base band
    API d_apcdel2;
    API d_vbctrl2;
    API d_bulgcal;

    // Analog Based Band
    API d_afcctladd;

    API d_vbuctrl;
    API d_vbdctrl;
    API d_apcdel1;
    API d_apcoff;
    API d_bulioff;
    API d_bulqoff;
    API d_dai_onoff;
    API d_auxdac;

  #if (ANLG_FAM == 1)
    API d_vbctrl;
  #elif ((ANLG_FAM == 2) || (ANLG_FAM == 3))
    API d_vbctrl1;
  #endif

    API d_bbctrl;

    // Monitoring tasks control (MCU <- DSP)
    // FB task
    API d_fb_det;           // FB detection result. (1 for FOUND).
    API d_fb_mode;          // Mode for FB detection algorithm.
    API a_sync_demod[4];    // FB/SB demod. result, (D_TOA,D_PM,D_ANGLE,D_SNR).

    // SB Task
    API a_sch26[5];         // Header + SB information, array of  5 words.

    API d_audio_gain_ul;
    API d_audio_gain_dl;

    // Controller of the melody E2 audio compressor
    API d_audio_compressor_ctrl;

    // AUDIO module
    API d_audio_init;
    API d_audio_status;

    // Audio tasks
    // TONES (MCU -> DSP)
    API d_toneskb_init;
    API d_toneskb_status;
    API d_k_x1_t0;
    API d_k_x1_t1;
    API d_k_x1_t2;
    API d_pe_rep;
    API d_pe_off;
    API d_se_off;
    API d_bu_off;
    API d_t0_on;
    API d_t0_off;
    API d_t1_on;
    API d_t1_off;
    API d_t2_on;
    API d_t2_off;
    API d_k_x1_kt0;
    API d_k_x1_kt1;
    API d_dur_kb;
    API d_shiftdl;
    API d_shiftul;

    API d_aec_ctrl;

    API d_es_level_api;
    API d_mu_api;

    // Melody Ringer module
    API d_melo_osc_used;
    API d_melo_osc_active;
    API a_melo_note0[4];
    API a_melo_note1[4];
    API a_melo_note2[4];
    API a_melo_note3[4];
    API a_melo_note4[4];
    API a_melo_note5[4];
    API a_melo_note6[4];
    API a_melo_note7[4];

    // selection of the melody format
    API d_melody_selection;

    // Holes due to the format melody E1
    API a_melo_holes[3];

    // Speech Recognition module
    API d_sr_status;          // status of the DSP speech reco task
    API d_sr_param;           // paramters for the DSP speech reco task: OOV threshold.
    API d_sr_bit_exact_test;  // bit exact test
    API d_sr_nb_words;        // number of words used in the speech recognition task
    API d_sr_db_level;        // estimate voice level in dB
    API d_sr_db_noise;        // estimate noise in dB
    API d_sr_mod_size;        // size of the model
    API a_n_best_words[4];  // array of the 4 best words
    API a_n_best_score[8];  // array of the 4 best scores (each score is 32 bits length)

    // Audio buffer
    API a_dd_1[22];         // Header + DATA traffic downlink information, sub. chan. 1.
    API a_du_1[22];         // Header + DATA traffic uplink information, sub. chan. 1.

    // V42bis module
    API d_v42b_nego0;
    API d_v42b_nego1;
    API d_v42b_control;
    API d_v42b_ratio_ind;
    API d_mcu_control;
    API d_mcu_control_sema;

    // Background tasks
    API d_background_enable;
    API d_background_abort;
    API d_background_state;
    API d_max_background;
    API a_background_tasks[16];
    API a_back_task_io[16];

    // GEA module defined in l1p_deft.h (the following section is overlaid with GPRS NDB memory)
    API d_gea_mode_ovly;
    API a_gea_kc_ovly[4];

    API d_hole3_ndb[8];

    // Encryption module
    API d_a5mode;           // Encryption Mode.

    API d_sched_mode_gprs_ovly;

    // 7 words are reserved for any possible mapping modification
    API d_hole4_ndb[5];

    // Ramp definition for Omega device
    API a_ramp[16];

    // CCCH/SACCH downlink information...(!!)
    API a_cd[15];           // Header + CCCH/SACCH downlink information.

    // FACCH downlink information........(!!)
    API a_fd[15];           // Header + FACCH downlink information.

    // Traffic downlink data frames......(!!)
    API a_dd_0[22];         // Header + DATA traffic downlink information, sub. chan. 0.

    // CCCH/SACCH uplink information.....(!!)
    API a_cu[15];           // Header + CCCH/SACCH uplink information.

    // FACCH downlink information........(!!)
    API a_fu[15];           // Header + FACCH uplink information

    // Traffic downlink data frames......(!!)
    API a_du_0[22];         // Header + DATA traffic uplink information, sub. chan. 0.

    // Random access.....................(MCU -> DSP).
    API d_rach;             // RACH information.

    //...................................(MCU -> DSP).
    API a_kc[4];            // Encryption Key Code.

    // Integrated Data Services module
    API d_ra_conf;
    API d_ra_act;
    API d_ra_test;
    API d_ra_statu;
    API d_ra_statd;
    API d_fax;
    API a_data_buf_ul[21];
    API a_data_buf_dl[37];

  #if (L1_NEW_AEC)
    // new AEC
    API a_new_aec_holes[422];
    API d_cont_filter;
    API d_granularity_att;
    API d_coef_smooth;
    API d_es_level_max;
    API d_fact_vad;
    API d_thrs_abs;
    API d_fact_asd_fil;
    API d_fact_asd_mut;
    API d_far_end_pow_h;
    API d_far_end_pow_l;
    API d_far_end_noise_h;
    API d_far_end_noise_l;
  #endif

  // Speech recognition model
  #if (L1_NEW_AEC)
    API a_sr_holes[1165];
  #else
    API a_sr_holes[1599];
  #endif // L1_NEW_AEC
    API a_model[1041];

    // EOTD buffer
    #if (L1_EOTD==1)
      API d_eotd_first;
      API d_eotd_max;
      API d_eotd_nrj_high;
      API d_eotd_nrj_low;
      API a_eotd_crosscor[18];
    #else
      API a_eotd_holes[22];
    #endif

    #if (MELODY_E2)
      API a_melody_e2_holes0[27];
      API d_melody_e2_osc_used;
      API d_melody_e2_osc_active;
      API d_melody_e2_semaphore;
      API a_melody_e2_osc[16][3];
      API d_melody_e2_globaltimefactor;
      API a_melody_e2_instrument_ptr[8];
      API a_melody_e2_holes1[708];
      API a_dsp_trace[SC_AUDIO_MELODY_E2_MAX_SIZE_OF_DSP_TRACE];
      API a_melody_e2_instrument_wave[SC_AUDIO_MELODY_E2_MAX_SIZE_OF_INSTRUMENT];
    #endif
  }
  T_NDB_MCU_DSP;

#elif ((DSP == 32) || (DSP == 31))
  typedef struct
  {
    // Monitoring tasks control..........(MCU <- DSP)
    API d_fb_det;           // FB detection result. (1 for FOUND).
    API d_fb_mode;          // Mode for FB detection algorithm.
    API a_sync_demod[4];    // FB/SB demod. result, (D_TOA,D_PM,D_ANGLE,D_SNR).

    // CCCH/SACCH downlink information...(!!)
    API a_cd[15];           // Header + CCCH/SACCH downlink information.

    // FACCH downlink information........(!!)
    API a_fd[15];           // Header + FACCH downlink information.

    // Traffic downlink data frames......(!!)
    API a_dd_0[22];         // Header + DATA traffic downlink information, sub. chan. 0.
    API a_dd_1[22];         // Header + DATA traffic downlink information, sub. chan. 1.

    // CCCH/SACCH uplink information.....(!!)
    API a_cu[15];           // Header + CCCH/SACCH uplink information.

    #if (SPEECH_RECO)
      // FACCH downlink information........(!!)
      API a_fu[3];              // Header + FACCH uplink information
                                // The size of this buffer is 15 word but some speech reco words
                                // are overlayer with this buffer. This is the reason why the size is 3 instead of 15.
      API d_sr_status;          // status of the DSP speech reco task
      API d_sr_param;           // paramters for the DSP speech reco task: OOV threshold.
      API sr_hole1;             // hole
      API d_sr_bit_exact_test;  // bit exact test
      API d_sr_nb_words;        // number of words used in the speech recognition task
      API d_sr_db_level;        // estimate voice level in dB
      API d_sr_db_noise;        // estimate noise in dB
      API d_sr_mod_size;        // size of the model
      API sr_holes_1[4];        // hole
    #else
      // FACCH downlink information........(!!)
      API a_fu[15];           // Header + FACCH uplink information
    #endif

    // Traffic uplink data frames........(!!)
    API a_du_0[22];         // Header + DATA traffic uplink information, sub. chan. 0.
    API a_du_1[22];         // Header + DATA traffic uplink information, sub. chan. 1.

    // Random access.....................(MCU -> DSP).
    API d_rach;             // RACH information.

    //...................................(MCU -> DSP).
    API d_a5mode;           // Encryption Mode.
    API a_kc[4];            // Encryption Key Code.
    API d_tch_mode;         // TCH mode register.
                            //   bit [0..1]  -> b_dai_mode.
                            //   bit [2]     -> b_dtx.

  // OMEGA...........................(MCU -> DSP).
  #if ((ANLG_FAM == 1) || (ANLG_FAM == 2))
    API a_ramp[16];
    #if (MELODY_E1)
      API d_melo_osc_used;
      API d_melo_osc_active;
      API a_melo_note0[4];
      API a_melo_note1[4];
      API a_melo_note2[4];
      API a_melo_note3[4];
      API a_melo_note4[4];
      API a_melo_note5[4];
      API a_melo_note6[4];
      API a_melo_note7[4];
      #if (DSP==31)
        // selection of the melody format
        API d_melody_selection;
        API holes[9];
      #else // DSP==32
        API d_dco_type;               // Tide
        API p_start_IQ;
        API d_level_off;
        API d_dco_dbg;
        API d_tide_resa;
        API d_asynch_margin;          // Perseus Asynch Audio Workaround
        API hole[4];
      #endif // DSP 32

    #else // NO MELODY E1
      #if (DSP==31)
      // selection of the melody format
      API d_melody_selection;
      API holes[43];               // 43 unused holes.
      #else // DSP==32
        API holes[34];               // 34 unused holes.
        API d_dco_type;               // Tide
        API p_start_IQ;
        API d_level_off;
        API d_dco_dbg;
        API d_tide_resa;
        API d_asynch_margin;          // Perseus Asynch Audio Workaround
        API hole[4];
      #endif //DSP == 32
    #endif // NO MELODY E1

    API d_debug3;
    API d_debug2;
    API d_debug1;                // bit 0 at 1 enable dsp f_tx delay for Omega
    API d_afcctladd;
    API d_vbuctrl;
    API d_vbdctrl;
    API d_apcdel1;
    API d_aec_ctrl;
    API d_apcoff;
    API d_bulioff;
    API d_bulqoff;
    API d_dai_onoff;
    API d_auxdac;

    #if (ANLG_FAM == 1)
      API d_vbctrl;
    #elif (ANLG_FAM == 2)
      API d_vbctrl1;
    #endif

    API d_bbctrl;
  #else 
    #error DSPCODE not supported with given ANALOG
  #endif //(ANALOG)1, 2
    //...................................(MCU -> DSP).
    API a_sch26[5];         // Header + SB information, array of  5 words.

    // TONES.............................(MCU -> DSP)
    API d_toneskb_init;
    API d_toneskb_status;
    API d_k_x1_t0;
    API d_k_x1_t1;
    API d_k_x1_t2;
    API d_pe_rep;
    API d_pe_off;
    API d_se_off;
    API d_bu_off;
    API d_t0_on;
    API d_t0_off;
    API d_t1_on;
    API d_t1_off;
    API d_t2_on;
    API d_t2_off;
    API d_k_x1_kt0;
    API d_k_x1_kt1;
    API d_dur_kb;

    // PLL...............................(MCU -> DSP).
    API d_pll_clkmod1;
    API d_pll_clkmod2;

    // DSP status returned..........(DSP --> MCU).
    API d_error_status;

    // RIF control.......................(MCU -> DSP).
    API d_spcx_rif;

    API d_shiftdl;
    API d_shiftul;

    API p_saec_prog;
    API p_aec_prog;
    API p_spenh_prog;

    API a_ovly[75];
    API d_ra_conf;
    API d_ra_act;
    API d_ra_test;
    API d_ra_statu;
    API d_ra_statd;
    API d_fax;
    #if (SPEECH_RECO)
      API a_data_buf_ul[3];
      API a_n_best_words[4];  // array of the 4 best words
      API a_n_best_score[8];  // array of the 4 best scores (each score is 32 bits length)
      API sr_holes_2[6];
      API a_data_buf_dl[37];

      API a_hole[24];

      API d_sched_mode_gprs_ovly;

      API fir_holes1[384];
      API a_fir31_uplink[31];
      API a_fir31_downlink[31];
      API d_audio_init;
      API d_audio_status;

      API a_model[1041];  // array of the speech reco model
    #else
      API a_data_buf_ul[21];
      API a_data_buf_dl[37];

      API a_hole[24];

      API d_sched_mode_gprs_ovly;

      API fir_holes1[384];
      API a_fir31_uplink[31];
      API a_fir31_downlink[31];
      API d_audio_init;
      API d_audio_status;

#if (L1_EOTD ==1)
      API a_eotd_hole[369];

      API d_eotd_first;
      API d_eotd_max;
      API d_eotd_nrj_high;
      API d_eotd_nrj_low;
      API a_eotd_crosscor[18];
#endif
    #endif
  }
  T_NDB_MCU_DSP;


#else // OTHER DSP CODE like 17

typedef struct
{
  // Monitoring tasks control..........(MCU <- DSP)
  API d_fb_det;           // FB detection result. (1 for FOUND).
  API d_fb_mode;          // Mode for FB detection algorithm.
  API a_sync_demod[4];    // FB/SB demod. result, (D_TOA,D_PM,D_ANGLE,D_SNR).

  // CCCH/SACCH downlink information...(!!)
  API a_cd[15];           // Header + CCCH/SACCH downlink information.

  // FACCH downlink information........(!!)
  API a_fd[15];           // Header + FACCH downlink information.

  // Traffic downlink data frames......(!!)
  #if (DATA14_4 == 0)
    API a_dd_0[20];         // Header + DATA traffic downlink information, sub. chan. 0.
    API a_dd_1[20];         // Header + DATA traffic downlink information, sub. chan. 1.
  #endif
  #if (DATA14_4 == 1)
    API a_dd_0[22];         // Header + DATA traffic downlink information, sub. chan. 0.
    API a_dd_1[22];         // Header + DATA traffic downlink information, sub. chan. 1.
  #endif

  // CCCH/SACCH uplink information.....(!!)
  API a_cu[15];           // Header + CCCH/SACCH uplink information.

  #if (SPEECH_RECO)
    // FACCH downlink information........(!!)
    API a_fu[3];              // Header + FACCH uplink information
                              // The size of this buffer is 15 word but some speech reco words
                              // are overlayer with this buffer. This is the reason why the size is 3 instead of 15.
    API d_sr_status;          // status of the DSP speech reco task
    API d_sr_param;           // paramters for the DSP speech reco task: OOV threshold.
    API sr_hole1;             // hole
    API d_sr_bit_exact_test;  // bit exact test
    API d_sr_nb_words;        // number of words used in the speech recognition task
    API d_sr_db_level;        // estimate voice level in dB
    API d_sr_db_noise;        // estimate noise in dB
    API d_sr_mod_size;        // size of the model
    API sr_holes_1[4];        // hole
  #else
    // FACCH downlink information........(!!)
    API a_fu[15];           // Header + FACCH uplink information
  #endif

  // Traffic uplink data frames........(!!)
  #if (DATA14_4 == 0)
    API a_du_0[20];         // Header + DATA traffic uplink information, sub. chan. 0.
    API a_du_1[20];         // Header + DATA traffic uplink information, sub. chan. 1.
  #endif
  #if (DATA14_4 == 1)
    API a_du_0[22];         // Header + DATA traffic uplink information, sub. chan. 0.
    API a_du_1[22];         // Header + DATA traffic uplink information, sub. chan. 1.
  #endif

  // Random access.....................(MCU -> DSP).
  API d_rach;             // RACH information.

  //...................................(MCU -> DSP).
  API d_a5mode;           // Encryption Mode.
  API a_kc[4];            // Encryption Key Code.
  API d_tch_mode;         // TCH mode register.
                               //        bit [0..1]  -> b_dai_mode.
                               //        bit [2]     -> b_dtx.

  // OMEGA...........................(MCU -> DSP).

#if ((ANLG_FAM == 1) || (ANLG_FAM == 2))
  API a_ramp[16];
  #if (MELODY_E1)
    API d_melo_osc_used;
    API d_melo_osc_active;
    API a_melo_note0[4];
    API a_melo_note1[4];
    API a_melo_note2[4];
    API a_melo_note3[4];
    API a_melo_note4[4];
    API a_melo_note5[4];
    API a_melo_note6[4];
    API a_melo_note7[4];
    #if (DSP == 17)
    // selection of the melody format
      API d_dco_type;               // Tide
      API p_start_IQ;
      API d_level_off;
      API d_dco_dbg;
      API d_tide_resa;
      API d_asynch_margin;          // Perseus Asynch Audio Workaround
      API hole[4];
    #else
      API d_melody_selection;
      API holes[9];
    #endif
  #else // NO MELODY E1
    // selection of the melody format
    #if (DSP == 17)
      API holes[34];               // 34 unused holes.
      API d_dco_type;               // Tide
      API p_start_IQ;
      API d_level_off;
      API d_dco_dbg;
      API d_tide_resa;
      API d_asynch_margin;          // Perseus Asynch Audio Workaround
      API hole[4]
    #else
      // selection of the melody format
      API d_melody_selection;
      API holes[43];               // 43 unused holes.
    #endif
  #endif
  API d_debug3;
  API d_debug2;
  API d_debug1;                // bit 0 at 1 enable dsp f_tx delay for Omega
  API d_afcctladd;
  API d_vbuctrl;
  API d_vbdctrl;
  API d_apcdel1;
  API d_aec_ctrl;
  API d_apcoff;
  API d_bulioff;
  API d_bulqoff;
  API d_dai_onoff;
  API d_auxdac;
  #if (ANLG_FAM == 1)
    API d_vbctrl;
  #elif (ANLG_FAM == 2)
    API d_vbctrl1;
  #endif
  API d_bbctrl;

  #else 
    #error DSPCODE not supported with given ANALOG
  #endif //(ANALOG)1, 2
  //...................................(MCU -> DSP).
  API a_sch26[5];         // Header + SB information, array of  5 words.

  // TONES.............................(MCU -> DSP)
  API d_toneskb_init;
  API d_toneskb_status;
  API d_k_x1_t0;
  API d_k_x1_t1;
  API d_k_x1_t2;
  API d_pe_rep;
  API d_pe_off;
  API d_se_off;
  API d_bu_off;
  API d_t0_on;
  API d_t0_off;
  API d_t1_on;
  API d_t1_off;
  API d_t2_on;
  API d_t2_off;
  API d_k_x1_kt0;
  API d_k_x1_kt1;
  API d_dur_kb;

  // PLL...............................(MCU -> DSP).
  API d_pll_clkmod1;
  API d_pll_clkmod2;

  // DSP status returned..........(DSP --> MCU).
  API d_error_status;

  // RIF control.......................(MCU -> DSP).
  API d_spcx_rif;

  API d_shiftdl;
  API d_shiftul;

  #if (AEC == 1)
    // AEC control.......................(MCU -> DSP).
    #if (VOC == FR_EFR)
      API p_aec_init;
      API p_aec_prog;
      API p_spenh_init;
      API p_spenh_prog;
    #endif

    #if (VOC == FR_HR_EFR)
      API p_saec_prog;
      API p_aec_prog;
      API p_spenh_prog;
    #endif
  #endif

    API a_ovly[75];
    API d_ra_conf;
    API d_ra_act;
    API d_ra_test;
    API d_ra_statu;
    API d_ra_statd;
    API d_fax;
    #if (SPEECH_RECO)
      API a_data_buf_ul[3];
      API a_n_best_words[4];  // array of the 4 best words
      API a_n_best_score[8];  // array of the 4 best scores (each score is 32 bits length)
      API sr_holes_2[6];
      API a_data_buf_dl[37];

      API fir_holes1[409];
      API a_fir31_uplink[31];
      API a_fir31_downlink[31];
      API d_audio_init;
      API d_audio_status;
      API a_model[1041];  // array of the speech reco model
    #else
      API a_data_buf_ul[21];
      API a_data_buf_dl[37];

      API fir_holes1[409];
      API a_fir31_uplink[31];
      API a_fir31_downlink[31];
      API d_audio_init;
      API d_audio_status;
    #endif
}
T_NDB_MCU_DSP;
#endif

#if (DSP == 34) || (DSP == 35) || (DSP == 36)
typedef struct
{
  API_SIGNED d_transfer_rate;

  // Common GSM/GPRS
  // These words specified the latencies to applies on some peripherics
  API_SIGNED d_lat_mcu_bridge;
  API_SIGNED d_lat_mcu_hom2sam;
  API_SIGNED d_lat_mcu_bef_fast_access;
  API_SIGNED d_lat_dsp_after_sam;

  // DSP Start address
  API_SIGNED d_gprs_install_address;

  API_SIGNED d_misc_config;

  API_SIGNED d_cn_sw_workaround;

  API_SIGNED d_hole2_param[4];

    //...................................Frequency Burst.
  API_SIGNED d_fb_margin_beg;
  API_SIGNED d_fb_margin_end;
  API_SIGNED d_nsubb_idle;
  API_SIGNED d_nsubb_dedic;
  API_SIGNED d_fb_thr_det_iacq;
  API_SIGNED d_fb_thr_det_track;
    //...................................Demodulation.
  API_SIGNED d_dc_off_thres;
  API_SIGNED d_dummy_thres;
  API_SIGNED d_dem_pond_gewl;
  API_SIGNED d_dem_pond_red;

    //...................................TCH Full Speech.
  API_SIGNED d_maccthresh1;
  API_SIGNED d_mldt;
  API_SIGNED d_maccthresh;
  API_SIGNED d_gu;
  API_SIGNED d_go;
  API_SIGNED d_attmax;
  API_SIGNED d_sm;
  API_SIGNED d_b;

  // V42Bis module
  API_SIGNED d_v42b_switch_hyst;
  API_SIGNED d_v42b_switch_min;
  API_SIGNED d_v42b_switch_max;
  API_SIGNED d_v42b_reset_delay;

  //...................................TCH Half Speech.
  API_SIGNED d_ldT_hr;
  API_SIGNED d_maccthresh_hr;
  API_SIGNED d_maccthresh1_hr;
  API_SIGNED d_gu_hr;
  API_SIGNED d_go_hr;
  API_SIGNED d_b_hr;
  API_SIGNED d_sm_hr;
  API_SIGNED d_attmax_hr;

  //...................................TCH Enhanced FR Speech.
  API_SIGNED c_mldt_efr;
  API_SIGNED c_maccthresh_efr;
  API_SIGNED c_maccthresh1_efr;
  API_SIGNED c_gu_efr;
  API_SIGNED c_go_efr;
  API_SIGNED c_b_efr;
  API_SIGNED c_sm_efr;
  API_SIGNED c_attmax_efr;

  //...................................CHED
  API_SIGNED d_sd_min_thr_tchfs;
  API_SIGNED d_ma_min_thr_tchfs;
  API_SIGNED d_md_max_thr_tchfs;
  API_SIGNED d_md1_max_thr_tchfs;

  API_SIGNED d_sd_min_thr_tchhs;
  API_SIGNED d_ma_min_thr_tchhs;
  API_SIGNED d_sd_av_thr_tchhs;
  API_SIGNED d_md_max_thr_tchhs;
  API_SIGNED d_md1_max_thr_tchhs;

  API_SIGNED d_sd_min_thr_tchefs;
  API_SIGNED d_ma_min_thr_tchefs;
  API_SIGNED d_md_max_thr_tchefs;
  API_SIGNED d_md1_max_thr_tchefs;

  API_SIGNED d_wed_fil_ini;
  API_SIGNED d_wed_fil_tc;
  API_SIGNED d_x_min;
  API_SIGNED d_x_max;
  API_SIGNED d_slope;
  API_SIGNED d_y_min;
  API_SIGNED d_y_max;
  API_SIGNED d_wed_diff_threshold;
  API_SIGNED d_mabfi_min_thr_tchhs;

  // FACCH module
  API_SIGNED d_facch_thr;

  // IDS module
  API_SIGNED d_max_ovsp_ul;
  API_SIGNED d_sync_thres;
  API_SIGNED d_idle_thres;
  API_SIGNED d_m1_thres;
  API_SIGNED d_max_ovsp_dl;
  API_SIGNED d_gsm_bgd_mgt;

  // FIR coefficients
  API a_fir_holes[4];
  API a_fir31_uplink[31];
  API a_fir31_downlink[31];
}
T_PARAM_MCU_DSP;
#elif (DSP == 33)
typedef struct
{
  API_SIGNED d_transfer_rate;

  // Common GSM/GPRS
  // These words specified the latencies to applies on some peripherics
  API_SIGNED d_lat_mcu_bridge;
  API_SIGNED d_lat_mcu_hom2sam;
  API_SIGNED d_lat_mcu_bef_fast_access;
  API_SIGNED d_lat_dsp_after_sam;

  // DSP Start address
  API_SIGNED d_gprs_install_address;

  API_SIGNED d_misc_config;

  API_SIGNED d_cn_sw_workaround;

  #if DCO_ALGO
    API_SIGNED d_cn_dco_param;

    API_SIGNED d_hole2_param[3];
  #else
    API_SIGNED d_hole2_param[4];
  #endif

    //...................................Frequency Burst.
  API_SIGNED d_fb_margin_beg;
  API_SIGNED d_fb_margin_end;
  API_SIGNED d_nsubb_idle;
  API_SIGNED d_nsubb_dedic;
  API_SIGNED d_fb_thr_det_iacq;
  API_SIGNED d_fb_thr_det_track;
    //...................................Demodulation.
  API_SIGNED d_dc_off_thres;
  API_SIGNED d_dummy_thres;
  API_SIGNED d_dem_pond_gewl;
  API_SIGNED d_dem_pond_red;

    //...................................TCH Full Speech.
  API_SIGNED d_maccthresh1;
  API_SIGNED d_mldt;
  API_SIGNED d_maccthresh;
  API_SIGNED d_gu;
  API_SIGNED d_go;
  API_SIGNED d_attmax;
  API_SIGNED d_sm;
  API_SIGNED d_b;

  // V42Bis module
  API_SIGNED d_v42b_switch_hyst;
  API_SIGNED d_v42b_switch_min;
  API_SIGNED d_v42b_switch_max;
  API_SIGNED d_v42b_reset_delay;

  //...................................TCH Half Speech.
  API_SIGNED d_ldT_hr;
  API_SIGNED d_maccthresh_hr;
  API_SIGNED d_maccthresh1_hr;
  API_SIGNED d_gu_hr;
  API_SIGNED d_go_hr;
  API_SIGNED d_b_hr;
  API_SIGNED d_sm_hr;
  API_SIGNED d_attmax_hr;

  //...................................TCH Enhanced FR Speech.
  API_SIGNED c_mldt_efr;
  API_SIGNED c_maccthresh_efr;
  API_SIGNED c_maccthresh1_efr;
  API_SIGNED c_gu_efr;
  API_SIGNED c_go_efr;
  API_SIGNED c_b_efr;
  API_SIGNED c_sm_efr;
  API_SIGNED c_attmax_efr;

  //...................................CHED
  API_SIGNED d_sd_min_thr_tchfs;
  API_SIGNED d_ma_min_thr_tchfs;
  API_SIGNED d_md_max_thr_tchfs;
  API_SIGNED d_md1_max_thr_tchfs;

  API_SIGNED d_sd_min_thr_tchhs;
  API_SIGNED d_ma_min_thr_tchhs;
  API_SIGNED d_sd_av_thr_tchhs;
  API_SIGNED d_md_max_thr_tchhs;
  API_SIGNED d_md1_max_thr_tchhs;

  API_SIGNED d_sd_min_thr_tchefs;
  API_SIGNED d_ma_min_thr_tchefs;
  API_SIGNED d_md_max_thr_tchefs;
  API_SIGNED d_md1_max_thr_tchefs;

  API_SIGNED d_wed_fil_ini;
  API_SIGNED d_wed_fil_tc;
  API_SIGNED d_x_min;
  API_SIGNED d_x_max;
  API_SIGNED d_slope;
  API_SIGNED d_y_min;
  API_SIGNED d_y_max;
  API_SIGNED d_wed_diff_threshold;
  API_SIGNED d_mabfi_min_thr_tchhs;

  // FACCH module
  API_SIGNED d_facch_thr;

  // IDS module
  API_SIGNED d_max_ovsp_ul;
  API_SIGNED d_sync_thres;
  API_SIGNED d_idle_thres;
  API_SIGNED d_m1_thres;
  API_SIGNED d_max_ovsp_dl;
  API_SIGNED d_gsm_bgd_mgt;

  // FIR coefficients
  API a_fir_holes[4];
  API a_fir31_uplink[31];
  API a_fir31_downlink[31];
}
T_PARAM_MCU_DSP;

#else

typedef struct
{
    //...................................Frequency Burst.
  API_SIGNED d_nsubb_idle;
  API_SIGNED d_nsubb_dedic;
  API_SIGNED d_fb_thr_det_iacq;
  API_SIGNED d_fb_thr_det_track;
    //...................................Demodulation.
  API_SIGNED d_dc_off_thres;
  API_SIGNED d_dummy_thres;
  API_SIGNED d_dem_pond_gewl;
  API_SIGNED d_dem_pond_red;
  API_SIGNED hole[1];
  API_SIGNED d_transfer_rate;
    //...................................TCH Full Speech.
  API_SIGNED d_maccthresh1;
  API_SIGNED d_mldt;
  API_SIGNED d_maccthresh;
  API_SIGNED d_gu;
  API_SIGNED d_go;
  API_SIGNED d_attmax;
  API_SIGNED d_sm;
  API_SIGNED d_b;

  #if (VOC == FR_HR) || (VOC == FR_HR_EFR)
      //...................................TCH Half Speech.
    API_SIGNED d_ldT_hr;
    API_SIGNED d_maccthresh_hr;
    API_SIGNED d_maccthresh1_hr;
    API_SIGNED d_gu_hr;
    API_SIGNED d_go_hr;
    API_SIGNED d_b_hr;
    API_SIGNED d_sm_hr;
    API_SIGNED d_attmax_hr;
  #endif

  #if (VOC == FR_EFR) || (VOC == FR_HR_EFR)
      //...................................TCH Enhanced FR Speech.
    API_SIGNED c_mldt_efr;
    API_SIGNED c_maccthresh_efr;
    API_SIGNED c_maccthresh1_efr;
    API_SIGNED c_gu_efr;
    API_SIGNED c_go_efr;
    API_SIGNED c_b_efr;
    API_SIGNED c_sm_efr;
    API_SIGNED c_attmax_efr;
  #endif

    //...................................TCH Full Speech.
  API_SIGNED d_sd_min_thr_tchfs;
  API_SIGNED d_ma_min_thr_tchfs;
  API_SIGNED d_md_max_thr_tchfs;
  API_SIGNED d_md1_max_thr_tchfs;

  #if (VOC == FR) || (VOC == FR_HR) || (VOC == FR_HR_EFR)
      //...................................TCH Half Speech.
    API_SIGNED d_sd_min_thr_tchhs;
    API_SIGNED d_ma_min_thr_tchhs;
    API_SIGNED d_sd_av_thr_tchhs;
    API_SIGNED d_md_max_thr_tchhs;
    API_SIGNED d_md1_max_thr_tchhs;
  #endif

  #if (VOC == FR_EFR) || (VOC == FR_HR_EFR)
      //...................................TCH Enhanced FR Speech.
    API_SIGNED d_sd_min_thr_tchefs;                       //(24L   *C_POND_RED)
    API_SIGNED d_ma_min_thr_tchefs;                       //(1200L *C_POND_RED)
    API_SIGNED d_md_max_thr_tchefs;                       //(2000L *C_POND_RED)
    API_SIGNED d_md1_max_thr_tchefs;                      //(160L  *C_POND_RED)
    API_SIGNED d_hole1;
  #endif

  API_SIGNED d_wed_fil_ini;
  API_SIGNED d_wed_fil_tc;
  API_SIGNED d_x_min;
  API_SIGNED d_x_max;
  API_SIGNED d_slope;
  API_SIGNED d_y_min;
  API_SIGNED d_y_max;
  API_SIGNED d_wed_diff_threshold;
  API_SIGNED d_mabfi_min_thr_tchhs;
  API_SIGNED d_facch_thr;
  API_SIGNED d_dsp_test;


  #if (DATA14_4 == 0 ) || (VOC == FR_HR_EFR)
    API_SIGNED d_patch_addr1;
    API_SIGNED d_patch_data1;
    API_SIGNED d_patch_addr2;
    API_SIGNED d_patch_data2;
    API_SIGNED d_patch_addr3;
    API_SIGNED d_patch_data3;
    API_SIGNED d_patch_addr4;
    API_SIGNED d_patch_data4;
  #endif

    //...................................
  API_SIGNED d_version_number;    // DSP patch version
  API_SIGNED d_ti_version;        // customer number. No more used since 1.5

  API_SIGNED d_dsp_page;

  #if IDS
  API_SIGNED d_max_ovsp_ul;
  API_SIGNED d_sync_thres;
  API_SIGNED d_idle_thres;
  API_SIGNED d_m1_thres;
  API_SIGNED d_max_ovsp_dl;
  #endif


}
T_PARAM_MCU_DSP;
#endif

#if (DSP_DEBUG_TRACE_ENABLE == 1)
typedef struct
{
  API d_debug_ptr_begin;
  API d_debug_ptr_end;
}
T_DB2_DSP_TO_MCU;
#endif

/* DSP error as per ndb->d_error_status */
enum dsp_error {
	DSP_ERR_RHEA		= 0x0001,
	DSP_ERR_IQ_SAMPLES	= 0x0004,
	DSP_ERR_DMA_PROG	= 0x0008,
	DSP_ERR_DMA_TASK	= 0x0010,
	DSP_ERR_DMA_PEND	= 0x0020,
	DSP_ERR_VM		= 0x0080,
	DSP_ERR_DMA_UL_TASK	= 0x0100,
	DSP_ERR_DMA_UL_PROG	= 0x0200,
	DSP_ERR_DMA_UL_PEND	= 0x0400,
	DSP_ERR_STACK_OV	= 0x0800,
};

/* How an ABB register + value is expressed in the API RAM */
#define ABB_VAL(reg, val) ( (((reg) & 0x1F) << 1) | (((val) & 0x3FF) << 6) )

/* How an ABB register + value | TRUE is expressed in the API RAM */
#define ABB_VAL_T(reg, val)	(ABB_VAL(reg, val) | 1)

#endif /* _CAL_DSP_API_H */
