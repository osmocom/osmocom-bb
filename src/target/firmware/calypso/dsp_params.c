/* Values from an actual phone firmware that uses the 3306 DSP ROM code version */
static T_PARAM_MCU_DSP dsp_params = {
	.d_transfer_rate		= 0x6666,
	/* Latencies */
	.d_lat_mcu_bridge		= 15,
	.d_lat_mcu_hom2sam		= 12,
	.d_lat_mcu_bef_fast_access	= 5,
	.d_lat_dsp_after_sam		= 4,
	/* DSP Start Address */
	.d_gprs_install_address		= 0x7002,	/* needs to be set by patch or manually */
	.d_misc_config			= 1,
	.d_cn_sw_workaround		= 0xE,
	.d_hole2_param			= { 0, 0, 0, 0 },
	/* Frequency Burst */
	.d_fb_margin_beg		= 24,
	.d_fb_margin_end		= 22,
	.d_nsubb_idle			= 296,
	.d_nsubb_dedic			= 30,
	.d_fb_thr_det_iacq		= 0x3333,
	.d_fb_thr_det_track		= 0x28f6,
	/* Demodulation */
	.d_dc_off_thres			= 0x7fff,
	.d_dummy_thres			= 17408,
	.d_dem_pond_gewl		= 26624,
	.d_dem_pond_red			= 20152,
	/* TCH Full Speech */
	.d_maccthresh1			= 7872,
	.d_mldt				= -4,
	.d_maccthresh			= 7872,
	.d_gu				= 5772,
	.d_go				= 7872,
	.d_attmax			= 53,
	.d_sm				= -892,
	.d_b				= 208,
	/* V.42 bis */
	.d_v42b_switch_hyst		= 16,
	.d_v42b_switch_min		= 64,
	.d_v42b_switch_max		= 250,
	.d_v42b_reset_delay		= 10,
	/* TCH Half Speech */
	.d_ldT_hr			= -5,
	.d_maccthresh_hr		= 6500,
	.d_maccthresh1_hr		= 6500,
	.d_gu_hr			= 2620,
	.d_go_hr			= 3700,
	.d_b_hr				= 182,
	.d_sm_hr			= -1608,
	.d_attmax_hr			= 53,
	/* TCH Enhanced FR Speech */
	.c_mldt_efr			= -4,
	.c_maccthresh_efr		= 8000,
	.c_maccthresh1_efr		= 8000,
	.c_gu_efr			= 4522,
	.c_go_efr			= 6500,
	.c_b_efr			= 174,
	.c_sm_efr			= -878,
	.c_attmax_efr			= 53,
	/* CHED TCH Full Speech */
	.d_sd_min_thr_tchfs		= 15,
	.d_ma_min_thr_tchfs		= 738,
	.d_md_max_thr_tchfs		= 1700,
	.d_md1_max_thr_tchfs		= 99,
	/* CHED TCH Half Speech */
	.d_sd_min_thr_tchhs		= 37,
	.d_ma_min_thr_tchhs		= 344,
	.d_sd_av_thr_tchhs		= 1845,
	.d_md_max_thr_tchhs		= 2175,
	.d_md1_max_thr_tchhs		= 138,
	/* CHED TCH/F EFR Speech */
	.d_sd_min_thr_tchefs		= 15,
	.d_ma_min_thr_tchefs		= 738,
	.d_md_max_thr_tchefs		= 0x4ce,
	.d_md1_max_thr_tchefs		= 0x63,
	/* */
	.d_wed_fil_ini			= 0x122a,
	.d_wed_fil_tc			= 0x7c00,
	.d_x_min			= 0xf,
	.d_x_max			= 0x17,
	.d_slope			= 0x87,
	.d_y_min			= 0x2bf,
	.d_y_max			= 0x99c,
	.d_wed_diff_threshold		= 0x196,
	.d_mabfi_min_thr_tchhs		= 0x14c8,
	/* FACCH module */
	.d_facch_thr			= 0,
	/* IDS module */
	.d_max_ovsp_ul			= 8,
	.d_sync_thres			= 0x3f50,
	.d_idle_thres			= 0x4000,
	.d_m1_thres			= 5,
	.d_max_ovsp_dl			= 8,
	.d_gsm_bgd_mgt			= 0,
	/* we don't set the FIR coefficients !?! */
};
