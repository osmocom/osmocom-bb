/*
 * (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdio.h>

#include <osmocom/gsm/rsl.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/ui/telnet_interface.h>
#include <osmocom/bb/mobile/mncc_ms.h>
#include <osmocom/bb/mobile/gsm480_ss.h>

/*
 * status screen generation
 */

static int status_netname(struct osmocom_ms *ms, char *text)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_subscriber *subscr = &ms->subscr;
	int len, shift;

	/* No Service */
	if (ms->shutdown == 2 || !ms->started) {
		strncpy(text, "SHUTDOWN", UI_COLS);
	} else
	/* No Service */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && mm->substate == GSM48_MM_SST_NO_CELL_AVAIL) {
		strncpy(text, "No Service", UI_COLS);
	} else
	/* Searching */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && (mm->substate == GSM48_MM_SST_PLMN_SEARCH_NORMAL
	  || mm->substate == GSM48_MM_SST_PLMN_SEARCH)) {
		strncpy(text, "Searching...", UI_COLS);
	} else
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && (mm->substate != GSM48_MM_SST_NORMAL_SERVICE
	  && mm->substate != GSM48_MM_SST_PLMN_SEARCH_NORMAL)) {
		strncpy(text, "Emerg. only", UI_COLS);
	} else
	/* no network selected */
	if (!cs->selected) {
		strncpy(text, "", UI_COLS);
	} else
	/* HPLM is the currently selected network, and we have a SPN */
	if (cs->selected && subscr->sim_valid && subscr->sim_spn[0] 
	 && gsm_match_mnc(cs->sel_cgi.lai.plmn.mcc, cs->sel_cgi.lai.plmn.mnc,
	    cs->sel_cgi.lai.plmn.mnc_3_digits, subscr->imsi)) {
		strncpy(text, subscr->sim_spn, UI_COLS);
	} else
	/* network name set for currently selected network */
	if (cs->selected && (mm->name_short[0] || mm->name_long[0])
	 && !memcmp(&cs->sel_cgi.lai.plmn, &mm->name_plmn, sizeof(struct osmo_plmn_id))) {
		const char *name;

	 	/* only short name */ 
	 	if (mm->name_short[0] && !mm->name_long[0])
			name = mm->name_short;
	 	/* only long name */ 
	 	else if (!mm->name_short[0] && mm->name_long[0])
			name = mm->name_long;
	 	/* both names, long name fits */ 
	 	else if (strlen(mm->name_long) <= UI_COLS)
			name = mm->name_long;
	 	/* both names, use short name, even if it does not fit */ 
		else
			name = mm->name_short;

		strncpy(text, name, UI_COLS);
	} else
	/* no network name for currently selected network */
	{
		const char *mcc_name, *mnc_name;
		int mcc_len, mnc_len;

		mcc_name = gsm_get_mcc(cs->sel_cgi.lai.plmn.mcc);
		mnc_name = gsm_get_mnc(&cs->sel_cgi.lai.plmn);
		mcc_len = strlen(mcc_name);
		mnc_len = strlen(mnc_name);

	 	/* MCC / MNC fits */ 
		if (mcc_len + 3 + mnc_len <= UI_COLS)
			sprintf(text, "%s / %s", mcc_name, mnc_name);
	 	/* MCC/MNC fits */ 
		else if (mcc_len + 1 + mnc_len <= UI_COLS)
			sprintf(text, "%s/%s", mcc_name, mnc_name);
		/* use MNC, even if it does not fit */
		else
			strncpy(text, mnc_name, UI_COLS);
	}
	text[UI_COLS] = '\0';

	/* center */
	len = strlen(text);
	if (len + 1 < UI_COLS) {
		shift = (UI_COLS - len) / 2;
		memcpy(text + shift, text, len + 1);
		memset(text, ' ', shift);
	}

	return 1;
}

static int status_lai(struct osmocom_ms *ms, char *text)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	sprintf(text, "%s %s %04x", gsm_get_mcc(cs->sel_cgi.lai.plmn.mcc),
		gsm_get_mnc(&cs->sel_cgi.lai.plmn), cs->sel_cgi.lai.lac);
	text[UI_COLS] = '\0';
	return 1;
}

static int status_imsi(struct osmocom_ms *ms, char *text)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	int len;

	if (subscr->imsi[0])
		strcpy(text, subscr->imsi);
	else
		strcpy(text, "---------------");
	if (subscr->tmsi < 0xfffffffe)
		sprintf(strchr(text, '\0'), " %08x", subscr->tmsi);
	else
		strcat(text, " --------");
	len = strlen(text);
	/* wrap */
	if (len > UI_COLS) {
		memcpy(text + UI_COLS + 1, text + UI_COLS, len - UI_COLS + 1);
		text[UI_COLS] = '\0';
		text[2 * UI_COLS + 1] = '\0';

		return 2;
	}

	return 1;
}

static int status_imei(struct osmocom_ms *ms, char *text)
{
	struct gsm_settings *set = &ms->settings;
	int len;

	sprintf(text, "%s/%s", set->imei, set->imeisv + strlen(set->imei));
	len = strlen(text);
	/* wrap */
	if (len > UI_COLS) {
		memcpy(text + UI_COLS + 1, text + UI_COLS, len - UI_COLS + 1);
		text[UI_COLS] = '\0';
		text[2 * UI_COLS + 1] = '\0';

		return 2;
	}

	return 1;
}

static int status_channel(struct osmocom_ms *ms, char *text)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	uint16_t arfcn = 0xffff;
	int hopping = 0;

	/* arfcn */
	if (rr->dm_est) {
		if (rr->cd_now.h)
			hopping = 1;
		else
			arfcn = rr->cd_now.arfcn;
	} else if (cs->selected)
		arfcn = cs->sel_arfcn;
		
	if (hopping)
		strcpy(text, "a:HOPP");
	else if (arfcn < 0xffff) {
		sprintf(text, "a:%d", arfcn & 1023);
		if (arfcn & ARFCN_PCS)
			strcat(text, "P");
		else if (arfcn >= 512 && arfcn <= 885)
			strcat(text, "D");
		else if (arfcn < 10)
			strcat(text, "   ");
		else if (arfcn < 100)
			strcat(text, "  ");
		else if (arfcn < 1000)
			strcat(text, " ");
	} else
		strcpy(text, "a:----");
	
	/* channel */
	if (!rr->dm_est)
		strcat(text, " b:0");
	else {
		uint8_t ch_type, ch_subch, ch_ts;

		rsl_dec_chan_nr(rr->cd_now.chan_nr, &ch_type, &ch_subch,
			&ch_ts);
		switch (ch_type) {
		case RSL_CHAN_SDCCH8_ACCH:
		case RSL_CHAN_SDCCH4_ACCH:
			sprintf(strchr(text, '\0'), " s:%d/%d", ch_ts,
				ch_subch);
			break;
		case RSL_CHAN_Lm_ACCHs:
			sprintf(strchr(text, '\0'), " h:%d/%d", ch_ts,
				ch_subch);
			break;
		case RSL_CHAN_Bm_ACCHs:
			sprintf(strchr(text, '\0'), " f:%d", ch_ts);
			break;
		default:
			sprintf(strchr(text, '\0'), " ?:%d", ch_ts);
			break;
		}
	}
	text[UI_COLS] = '\0';

	return 1;
}

static int status_rx(struct osmocom_ms *ms, char *text)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	/* rxlev */
	if (rr->rxlev != 255) {
		sprintf(text, "rx:%d dbm", rr->rxlev - 110);
	} else
		strcpy(text, "rx:--");
	text[UI_COLS] = '\0';

	return 1;
}

static int status_tx(struct osmocom_ms *ms, char *text)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm_settings *set = &ms->settings;

	/* ta, pwr */
	if (rr->dm_est)
		sprintf(text, "ta:%d tx:%d",
			rr->cd_now.ind_ta - set->alter_delay,
			(set->alter_tx_power) ? set->alter_tx_power_value
					: rr->cd_now.ind_tx_power);
	else
		strcpy(text, "ta:-- tx:--");
	text[UI_COLS] = '\0';

	return 1;
}

static int status_nb(struct osmocom_ms *ms, char *text)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_nb_summary *nb;
	int i;

	for (i = 0; i < 6; i++) {
		nb = &cs->nb_summary[i];
		if (nb->valid) {
			sprintf(text, "%d:%d", i + 1,
				nb->arfcn & 1023);
			if (nb->arfcn & ARFCN_PCS)
				strcat(text, "P");
			else if (nb->arfcn >= 512 && nb->arfcn <= 885)
				strcat(text, "D");
			else if (nb->arfcn < 10)
				strcat(text, "   ");
			else if (nb->arfcn < 100)
				strcat(text, "  ");
			else if (nb->arfcn < 1000)
				strcat(text, " ");
			sprintf(strchr(text, '\0'), " %d", nb->rxlev_dbm);
		} else
			sprintf(text, "%d:-", i + 1);
		text[UI_COLS] = '\0';
		text += (UI_COLS + 1);
	}

	return i;
}

static int status_fkeys(struct osmocom_ms *ms, char *text)
{
	return 0;
}

/*
 * status screen structure
 */

#define SHOW_HIDE	"(show|hide)"
#define SHOW_HIDE_STR	"Show this feature on display\n" \
			"Do not show this feature on display"

struct status_screen status_screen[GUI_NUM_STATUS] = {
	/* network name */
	{
		.feature	= "network-name",
		.feature_vty	= "network-name " SHOW_HIDE,
		.feature_help	= "Show network name on display\n"
				  SHOW_HIDE_STR,
		.default_en	= 1,
		.lines		= 1,
		.display_func	= status_netname,
	},
	/* LAI */
	{
		.feature	= "location-area-info",
		.feature_vty	= "location-area-info " SHOW_HIDE,
		.feature_help	= "Show LAI on display\n" SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 1,
		.display_func	= status_lai,
	},
	/* IMSI */
	{
		.feature	= "imsi",
		.feature_vty	= "imsi " SHOW_HIDE,
		.feature_help	= "Show IMSI/TMSI on display\n" SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 2,
		.display_func	= status_imsi,
	},
	/* IMEI */
	{
		.feature	= "imei",
		.feature_vty	= "imei " SHOW_HIDE,
		.feature_help	= "Show IMEI on display\n" SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 2,
		.display_func	= status_imei,
	},
	/* channel */
	{
		.feature	= "channel",
		.feature_vty	= "channel " SHOW_HIDE,
		.feature_help	= "Show current channel on display\n"
				  SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 1,
		.display_func	= status_channel,
	},
	/* tx */
	{
		.feature	= "tx",
		.feature_vty	= "tx " SHOW_HIDE,
		.feature_help	= "Show current timing advance and rx level "
				  "on display\n" SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 1,
		.display_func	= status_tx,
	},
	/* rx */
	{
		.feature	= "rx",
		.feature_vty	= "rx " SHOW_HIDE,
		.feature_help	= "Show current rx level on display\n"
				  SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 1,
		.display_func	= status_rx,
	},
	/* nb */
	{
		.feature	= "neighbours",
		.feature_vty	= "neighbours " SHOW_HIDE,
		.feature_help	= "Show neighbour cells on display\n"
				  SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 6,
		.display_func	= status_nb,
	},
	/* function keys */
	{
		.feature	= "function-keys",
		.feature_vty	= "function-keys " SHOW_HIDE,
		.feature_help	= "Show function keys (two keys right "
				  "below the display) on the display's bottom "
				  "line\n" SHOW_HIDE_STR,
		.default_en	= 1,
		.lines		= 0,
		.display_func	= status_fkeys,
	},
};

/*
 * select menus
 */

enum config_type {
	SELECT_NODE,
	SELECT_CHOOSE,
	SELECT_NUMBER,
	SELECT_INT,
	SELECT_FIXINT,
	SELECT_FIXSTRING,
};

struct gui_choose_set {
	const char		*name;
	int			value;
};

static struct gui_choose_set enable_disable_set[] = {
	{ "Enabled", 1 },
	{ "Disabled", 0 },
	{ NULL, 0 }
};

static struct gui_choose_set activated_deactivated_set[] = {
	{ "Activated", 1 },
	{ "Deactivated", 0 },
	{ NULL, 0 }
};

static struct gui_choose_set sim_type_set[] = {
	{ "None", GSM_SIM_TYPE_NONE },
	{ "Reader", GSM_SIM_TYPE_L1PHY },
	{ "SAP", GSM_SIM_TYPE_SAP },
	{ "Test SIM", GSM_SIM_TYPE_TEST },
	{ NULL, 0 }
};

static struct gui_choose_set network_mode_set[] = {
	{ "Automatic", PLMN_MODE_AUTO },
	{ "Manual", PLMN_MODE_MANUAL },
	{ NULL, 0 }
};

static struct gui_choose_set codec_set[] = {
	{ "Full-rate", 0 },
	{ "Half-rate", 1 },
	{ NULL, 0 }
};

/* structure defines one line in a config menu */
struct gui_select_line {
	const char		*title;	/* menu title */
	struct gui_select_line	*parent; /* parent menu */

	const char		*name; /* entry name */
	enum config_type	type; /* entry type */

	/* sub node type */
	struct gui_select_line	*node; /* sub menu */

	/* select/number/int/fixint/fixstring type */
	struct gui_choose_set	*set; /* settable values */
	void *			(*query)(struct osmocom_ms *ms);
	int			(*cmd)(struct osmocom_ms *ms, void *vlaue);
	int			restart; /* if change requres reset */
	int			digits; /* max number of digits */
	int			min, max; /* range for integer */
	int			fix; /* fixed value to use */
	char			*fixstring; /* fixed string to use */
};

static void *config_knocking_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.cw;

	return &value;
}
static int config_knocking_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.cw = *(int *)value;

	return 0;
}

static void *config_autoanswer_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.auto_answer;

	return &value;
}
static int config_autoanswer_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.auto_answer = *(int *)value;

	return 0;
}

static void *config_clip_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.clip;

	return &value;
}
static int config_clip_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.clip = *(int *)value;
	if (value)
		ms->settings.clir = 0;

	return 0;
}

static void *config_clir_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.clir;

	return &value;
}
static int config_clir_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.clir = *(int *)value;
	if (value)
		ms->settings.clip = 0;

	return 0;
}

static void *config_sca_query(struct osmocom_ms *ms)
{
	return ms->settings.sms_sca;
}
static int config_sca_cmd(struct osmocom_ms *ms, void *value)
{
	strncpy(ms->settings.sms_sca, value, sizeof(ms->settings.sms_sca) - 1);

	return 0;
}

static void *config_sim_type_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.sim_type;

	return &value;
}
static int config_sim_type_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.sim_type = *(int *)value;

	return 0;
}

static void *config_network_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.plmn_mode;

	return &value;
}
static int config_network_cmd(struct osmocom_ms *ms, void *value)
{
	int mode = *(int *)value;
	struct msgb *nmsg;

	if (!ms->started)
		ms->settings.plmn_mode = mode;
	else {
		nmsg = gsm322_msgb_alloc((mode == PLMN_MODE_AUTO) ?
			GSM322_EVENT_SEL_AUTO : GSM322_EVENT_SEL_MANUAL);
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);
	}

	return 0;
}

static void *config_imei_query(struct osmocom_ms *ms)
{
	return ms->settings.imei;
}
static int config_imei_cmd(struct osmocom_ms *ms, void *value)
{
	if (gsm_check_imei(value, "0"))
		return -EINVAL;

	strncpy(ms->settings.imei, value, sizeof(ms->settings.imei) - 1);
	/* only copy the number of digits in imei */
	strncpy(ms->settings.imeisv, value, strlen(ms->settings.imei));

	return 0;
}

static void *config_imeisv_query(struct osmocom_ms *ms)
{
	return ms->settings.imeisv + strlen(ms->settings.imei);
}
static int config_imeisv_cmd(struct osmocom_ms *ms, void *value)
{
	if (gsm_check_imei(ms->settings.imei, value))
		return -EINVAL;

	/* only copy the sv */
	strncpy(ms->settings.imeisv + strlen(ms->settings.imei), value, 1);

	return 0;
}

static void *config_imei_random_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.imei_random;

	return &value;
}
static int config_imei_random_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.imei_random = *(int *)value;

	return 0;
}

static void *config_emerg_imsi_query(struct osmocom_ms *ms)
{
	return ms->settings.emergency_imsi;
}
static int config_emerg_imsi_cmd(struct osmocom_ms *ms, void *value)
{
	if (strlen(value) && osmo_imsi_str_valid(value))
		return -EINVAL;

	strcpy(ms->settings.emergency_imsi, value);

	return 0;
}

static void *config_tx_power_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.alter_tx_power_value;

	return &value;
}
static int config_tx_power_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.alter_tx_power_value = *(int *)value;
	ms->settings.alter_tx_power = 1;

	return 0;
}

static int config_tx_power_auto_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.alter_tx_power = 0;

	return 0;
}

static void *config_sim_delay_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.alter_delay;

	return &value;
}
static int config_sim_delay_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.alter_delay = *(int *)value;
	gsm48_rr_alter_delay(ms);

	return 0;
}

static void *config_stick_query(struct osmocom_ms *ms)
{
	static int value;
	
	value = ms->settings.stick_arfcn & 1023;

	return &value;
}
static int config_stick_disable_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.stick = 0;

	return 0;
}

static int config_stick_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.stick_arfcn = *(int *)value;
	ms->settings.stick = 1;

	return 0;
}

static int config_stick_pcs_cmd(struct osmocom_ms *ms, void *value)
{
	ms->settings.stick_arfcn = *(int *)value | ARFCN_PCS;
	ms->settings.stick = 1;

	return 0;
}

static void *config_support_half_query(struct osmocom_ms *ms)
{
	static int value = 1;

	if (!ms->settings.half_v1 && !ms->settings.half_v3)
		value = 0;
	else
		value = ms->settings.half;

	return &value;
}
static int config_support_half_cmd(struct osmocom_ms *ms, void *value)
{
	if (!ms->settings.half_v1 && !ms->settings.half_v3)
		return -ENOTSUP;
	ms->settings.half = *(int *)value;
	if (ms->settings.half == 0)
		ms->settings.half_prefer = 0;

	return 0;
}

static void *config_prefer_codec_query(struct osmocom_ms *ms)
{
	static int value = 0;

	if (!ms->settings.half_v1 && !ms->settings.half_v3)
		return &value;
	if (!ms->settings.half)
		return &value;
	value = ms->settings.half_prefer;

	return &value;
}
static int config_prefer_codec_cmd(struct osmocom_ms *ms, void *value)
{
	if (*(int *)value == 0) {
		if (!ms->settings.full_v1 && !ms->settings.full_v2
		 && !ms->settings.full_v3)
			return -ENOTSUP;
		ms->settings.half_prefer = 0;
	} else {
		if (!ms->settings.half_v1 && !ms->settings.half_v3)
			return -ENOTSUP;
		ms->settings.half = 1;
		ms->settings.half_prefer = 1;
	}

	return 0;
}

static void *config_status_query(struct osmocom_ms *ms)
{
	static int value;
	struct gui_select_line *menu = ms->gui.choose_menu;
	int i = 0;

	for (i = 0; i < GUI_NUM_STATUS; i++) {
		if (!strcmp(menu->name, status_screen[i].feature))
			break;
	}

	value = (ms->settings.status_enable >> i) & 1;

	return &value;
}
static int config_status_cmd(struct osmocom_ms *ms, void *value)
{
	struct gui_select_line *menu = ms->gui.choose_menu;
	int i = 0;

	for (i = 0; i < GUI_NUM_STATUS; i++) {
		if (!strcmp(menu->name, status_screen[i].feature))
			break;
	}
	if (*(int *)value)
		ms->settings.status_enable |= (1 << i);
	else
		ms->settings.status_enable &= ~(1 << i);

	return 0;
}

static struct gui_select_line config_setup[];

/* call node */
static struct gui_select_line config_call[] = {
	{
		.title = "Call SETUP",
		.parent = config_setup,
	},
	{
		.name = "Knocking",
		.type = SELECT_CHOOSE,
		.set = enable_disable_set,
		.query = config_knocking_query,
		.cmd = config_knocking_cmd,
		.restart = 0,
	},
	{
		.name = "Autoanswer",
		.type = SELECT_CHOOSE,
		.set = enable_disable_set,
		.query = config_autoanswer_query,
		.cmd = config_autoanswer_cmd,
		.restart = 0,
	},
	{
		.name = "CLIP",
		.type = SELECT_CHOOSE,
		.set = activated_deactivated_set,
		.query = config_clip_query,
		.cmd = config_clip_cmd,
		.restart = 0,
	},
	{
		.name = "CLIR",
		.type = SELECT_CHOOSE,
		.set = activated_deactivated_set,
		.query = config_clir_query,
		.cmd = config_clir_cmd,
		.restart = 0,
	},
	{
		.name = NULL,
	},
};

/* SMS node */
static struct gui_select_line config_sms[] = {
	{
		.title = "SMS Setup",
		.parent = config_setup,
	},
	{
		.name = "SCA Number",
		.type = SELECT_NUMBER,
		.query = config_sca_query,
		.cmd = config_sca_cmd,
		.digits = 20,
	},
	{
		.name = NULL,
	},
};

static struct gui_select_line config_phone[];

/* IMEI node */
static struct gui_select_line config_imei[] = {
	{
		.title = "IMEI Setup",
		.parent = config_phone,
	},
	{
		.name = "IMEI Number",
		.type = SELECT_NUMBER,
		.query = config_imei_query,
		.cmd = config_imei_cmd,
		.digits = 15,
	},
	{
		.name = "Software Ver",
		.type = SELECT_NUMBER,
		.query = config_imeisv_query,
		.cmd = config_imeisv_cmd,
		.digits = 1,
	},
	{
		.name = "Fixed IMEI",
		.type = SELECT_FIXINT,
		.cmd = config_imei_random_cmd,
		.fix = 0,
	},
	{
		.name = "Randomize",
		.type = SELECT_INT,
		.query = config_imei_random_query,
		.cmd = config_imei_random_cmd,
		.min = 0,
		.max = 15,
	},
	{
		.name = NULL,
	},
};

/* codec node */
static struct gui_select_line config_codec[] = {
	{
		.title = "Voice Codec",
		.parent = config_phone,
	},
	{
		.name = "Half-rate",
		.type = SELECT_CHOOSE,
		.set = enable_disable_set,
		.query = config_support_half_query,
		.cmd = config_support_half_cmd,
		.restart = 0,
	},
	{
		.name = "Prefer Codec",
		.type = SELECT_CHOOSE,
		.set = codec_set,
		.query = config_prefer_codec_query,
		.cmd = config_prefer_codec_cmd,
		.restart = 0,
	},
	{
		.name = NULL,
	},
};

#if 0
/* support node */
static struct gui_select_line config_support[] = {
	{
		.title = "Support",
		.parent = config_phone,
	},
	{
		.name = NULL,
	},
};
#endif

/* emergency IMSI */
static struct gui_select_line config_emerg_imsi[] = {
	{
		.title = "Emerg. IMSI",
		.parent = config_phone,
	},
	{
		.name = "IMSI",
		.type = SELECT_NUMBER,
		.query = config_emerg_imsi_query,
		.cmd = config_emerg_imsi_cmd,
		.digits = 15,
	},
	{
		.name = "No IMSI",
		.type = SELECT_FIXSTRING,
		.cmd = config_emerg_imsi_cmd,
		.fixstring = "",
	},
	{
		.name = NULL,
	},
};

/* TX power node */
static struct gui_select_line config_tx_power[] = {
	{
		.title = "TX Power",
		.name = "Level", /* name of fixed selection */
		.parent = config_phone,
	},
	{
		.name = "Auto",
		.type = SELECT_FIXINT,
		.cmd = config_tx_power_auto_cmd,
		.fix = 0,
	},
	{
		.name = "Full",
		.type = SELECT_FIXINT,
		.cmd = config_tx_power_cmd,
		.fix = 0,
	},
	{
		.name = "Level",
		.type = SELECT_INT,
		.query = config_tx_power_query,
		.cmd = config_tx_power_cmd,
		.min = 0,
		.max = 31,
	},
	{
		.name = NULL,
	},
};

/* simulated delay node */
static struct gui_select_line config_sim_delay[] = {
	{
		.title = "Sim. Delay",
		.name = "Delay", /* name of fixed selection */
		.parent = config_phone,
	},
	{
		.name = "Disabled",
		.type = SELECT_FIXINT,
		.cmd = config_sim_delay_cmd,
		.fix = 0,
	},
	{
		.name = "Level",
		.type = SELECT_INT,
		.query = config_sim_delay_query,
		.cmd = config_sim_delay_cmd,
		.min = -128,
		.max = 127,
	},
	{
		.name = NULL,
	},
};

/* stick node */
static struct gui_select_line config_stick[] = {
	{
		.title = "Stick ARFCN",
		.name = "Stick", /* name of fixed selection */
		.parent = config_phone,
	},
	{
		.name = "Disabled",
		.type = SELECT_FIXINT,
		.cmd = config_stick_disable_cmd,
		.fix = 0,
	},
	{
		.name = "ARFCN",
		.type = SELECT_INT,
		.query = config_stick_query,
		.cmd = config_stick_cmd,
		.min = 0,
		.max = 1023,
	},
	{
		.name = "ARFCN PCS",
		.type = SELECT_INT,
		.query = config_stick_query,
		.cmd = config_stick_pcs_cmd,
		.min = 512,
		.max = 810,
	},
	{
		.name = NULL,
	},
};

/* phone node */
static struct gui_select_line config_phone[] = {
	{
		.title = "Phone Config",
		.parent = config_setup,
	},
	{
		.name = "IMEI",
		.type = SELECT_NODE,
		.node = config_imei,
	},
	{
		.name = "Codec",
		.type = SELECT_NODE,
		.node = config_codec,
	},
#if 0
	{
		.name = "Support",
		.type = SELECT_NODE,
		.node = config_support,
	},
#endif
	{
		.name = "Emerg. IMSI",
		.type = SELECT_NODE,
		.node = config_emerg_imsi,
	},
	{
		.name = "TX Power",
		.type = SELECT_NODE,
		.node = config_tx_power,
	},
	{
		.name = "Sim. Delay",
		.type = SELECT_NODE,
		.node = config_sim_delay,
	},
	{
		.name = "Stick ARFCN",
		.type = SELECT_NODE,
		.node = config_stick,
	},
	{
		.name = NULL,
	},
};

/* network node */
static struct gui_select_line config_network[] = {
	{
		.title = "Net Config",
		.parent = config_setup,
	},
	{
		.name = "Network Sel",
		.type = SELECT_CHOOSE,
		.set = network_mode_set,
		.query = config_network_query,
		.cmd = config_network_cmd,
		.restart = 0,
	},
	{
		.name = NULL,
	},
};

/* status node */
static struct gui_select_line config_status[GUI_NUM_STATUS + 2];

void gui_init_status_config(void)
{
	int i;

	memset(config_status, 0, sizeof(config_status));

	config_status[0].title = "Status Set";
	config_status[0].parent = config_setup;

	for (i = 0; i < GUI_NUM_STATUS; i++) {
		config_status[i + 1].name = status_screen[i].feature;
		config_status[i + 1].type = SELECT_CHOOSE;
		config_status[i + 1].set = enable_disable_set;
		config_status[i + 1].query = config_status_query;
		config_status[i + 1].cmd = config_status_cmd;
		config_status[i + 1].restart = 0;
	}
};

static struct gui_select_line config_sim[];

/* test SIM node */
static struct gui_select_line config_test_sim[] = {
	{
		.title = "Test SIM",
		.parent = config_sim,
	},
	{
		.name = NULL,
	},
};

/* SIM node */
static struct gui_select_line config_sim[] = {
	{
		.title = "SIM Setup",
		.parent = config_setup,
	},
	{
		.name = "SIM Type",
		.type = SELECT_CHOOSE,
		.set = sim_type_set,
		.query = config_sim_type_query,
		.cmd = config_sim_type_cmd,
		.restart = 1,
	},
	{
		.name = "Test SIM",
		.type = SELECT_NODE,
		.node = config_test_sim,
	},
	{
		.name = NULL,
	},
};

/* setup node */
static struct gui_select_line config_setup[] = {
	{
		.title = "Setup",
		.parent = NULL,
	},
	{
		.name = "Call",
		.type = SELECT_NODE,
		.node = config_call,
	},
	{
		.name = "SMS",
		.type = SELECT_NODE,
		.node = config_sms,
	},
	{
		.name = "Phone",
		.type = SELECT_NODE,
		.node = config_phone,
	},
	{
		.name = "Network",
		.type = SELECT_NODE,
		.node = config_network,
	},
	{
		.name = "Status",
		.type = SELECT_NODE,
		.node = config_status,
	},
	{
		.name = "SIM",
		.type = SELECT_NODE,
		.node = config_sim,
	},
	{
		.name = NULL,
	},
};

static struct gui_select_line menu_menu[];

/* SMS node */
static struct gui_select_line menu_sms[] = {
	{
		.title = "SMS Menu",
		.parent = menu_menu,
	},
	{
		.name = NULL,
	},
};

/* SIM node */
static struct gui_select_line menu_sim[] = {
	{
		.title = "SIM Menu",
		.parent = menu_menu,
	},
	{
		.name = NULL,
	},
};

/* network node */
static struct gui_select_line menu_network[] = {
	{
		.title = "Net Menu",
		.parent = menu_menu,
	},
	{
		.name = NULL,
	},
};

static struct gui_select_line menu_forwarding[];

/* activate node */
static struct gui_select_line menu_fwd_activate[] = {
	{
		.title = "Fwd Activate",
		.parent = menu_forwarding,
	},
	{
		.name = NULL,
	},
};

/* deactivate node */
static struct gui_select_line menu_fwd_deactivate[] = {
	{
		.title = "Fwd Deactivate",
		.parent = menu_forwarding,
	},
	{
		.name = NULL,
	},
};

/* query node */
static struct gui_select_line menu_fwd_query[] = {
	{
		.title = "Fwd Query",
		.parent = menu_forwarding,
	},
	{
		.name = NULL,
	},
};

/* forwarding node */
static struct gui_select_line menu_forwarding[] = {
	{
		.title = "Fwd Menu",
		.parent = menu_menu,
	},
	{
		.name = "Activate",
		.type = SELECT_NODE,
		.node = menu_fwd_activate,
	},
	{
		.name = "Deactivate",
		.type = SELECT_NODE,
		.node = menu_fwd_deactivate,
	},
	{
		.name = "Query",
		.type = SELECT_NODE,
		.node = menu_fwd_query,
	},
	{
		.name = NULL,
	},
};

/* menu node */
static struct gui_select_line menu_menu[] = {
	{
		.title = "Menu",
		.parent = NULL,
	},
	{
		.name = "SMS",
		.type = SELECT_NODE,
		.node = menu_sms,
	},
	{
		.name = "SIM",
		.type = SELECT_NODE,
		.node = menu_sim,
	},
	{
		.name = "Network",
		.type = SELECT_NODE,
		.node = menu_network,
	},
	{
		.name = "Forwarding",
		.type = SELECT_NODE,
		.node = menu_forwarding,
	},
	{
		.name = NULL,
	},
};

/*
 * UI handling for mobile instance
 */

static void update_status(void *arg);
static int gui_select(struct osmocom_ms *ms, struct gui_select_line *menu);
static int gui_choose(struct osmocom_ms *ms, struct gui_select_line *menu);
static int gui_number(struct osmocom_ms *ms, struct gui_select_line *menu);
static int gui_int(struct osmocom_ms *ms, struct gui_select_line *menu);
static int gui_fixint(struct osmocom_ms *ms, struct gui_select_line *menu,
	struct gui_select_line *value);
static int gui_fixstring(struct osmocom_ms *ms, struct gui_select_line *menu,
	struct gui_select_line *value);
static int gui_chosen(struct osmocom_ms *ms, const char *value);
static int gui_supserv(struct gsm_ui *gui, int clear);
static int gui_input(struct gsm_ui *gui, int menu);

enum {
	MENU_STATUS,
	MENU_DIALING,
	MENU_CALL,
	MENU_SELECT,
	MENU_SUPSERV,
	MENU_SS_INPUT,
};

static int telnet_cb(struct ui_inst *ui)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);

	if (!llist_empty(&gui->ui.active_connections)) {
		/* start status screen again, if timer is not running */
		if (gui->menu == MENU_STATUS
		 && !osmo_timer_pending(&gui->timer))
			gui_start(ms);
		else {
			/* refresh, if someone connects */
			ui_inst_refresh(ui);
		}
	} else {
		/* stop if no more connection */
		gui_stop(ms);
	}
	return 0;
}

static int beep_cb(struct ui_inst *ui)
{
	ui_telnet_puts(ui, "");

	return 0;
}

static int key_dialing_cb(struct ui_inst *ui, enum ui_key kp);

static int gui_dialing(struct gsm_ui *gui)
{
	struct ui_inst *ui = &gui->ui;

	/* go to dialing screen */
	gui->menu = MENU_DIALING;
	ui_inst_init(ui, &ui_stringview, key_dialing_cb, beep_cb,
		telnet_cb);
	ui->title = "Number:";
	ui->ud.stringview.number = gui->dialing;
	ui->ud.stringview.num_len = sizeof(gui->dialing);
	ui->ud.stringview.pos = 1;
	ui_inst_refresh(ui);

	return 0;
}

static int key_dialing_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
	struct gsm_call *call;
	int num_calls = 0;

	if (kp == UI_KEY_PICKUP) {
		char *number = ui->ud.stringview.number;
		int i;

		/* check for supplementary services first */
		if (number[0] == '*' || number[0] == '#') {
			gui_supserv(gui, 1);

			return 1; /* handled */
		}
		/* check if number contains not dialable digits */
		for (i = 0; i < strlen(number); i++) {
			if ((i != 0 && number[i] == '+')
			 || !strchr("01234567890*#abc+", number[i])) {
			 	/* point to error digit */
				ui->ud.stringview.pos = i;
				ui_inst_refresh(ui);
				return 1; /* handled */
			}
		}
		mncc_call(ms, ui->ud.stringview.number, GSM_CALL_T_VOICE);

		/* go to call screen */
		gui->menu = MENU_STATUS;
		gui_notify_call(ms);

		return 1; /* handled */
	}
	if (kp == UI_KEY_HANGUP) {
		llist_for_each_entry(call, &ms->mncc_entity.call_list,
			entry) {
			num_calls++;
		}
		if (!num_calls) {
			/* go to status screen */
			gui_start(ms);
		} else {
			/* go to call screen */
			gui->menu = MENU_STATUS;
			gui_notify_call(ms);
		}

		return 1; /* handled */
	}

	return 0;
}

static int key_status_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);

	if ((kp >= UI_KEY_0 && kp <= UI_KEY_9) || kp == UI_KEY_STAR
	 || kp == UI_KEY_HASH) {
		gui->dialing[0] = kp;
		gui->dialing[1] = '\0';
		/* go to dialing screen */
		gui_dialing(gui);

		return 1; /* handled */
	}
	if (kp == UI_KEY_PICKUP) {
		gui->dialing[0] = '\0';
		/* go to dialing screen */
		gui_dialing(gui);
	}
	if (kp == UI_KEY_F1) {
		/* go to setup screen */
		gui_select(ms, menu_menu);

		return 1; /* handled */
	}
	if (kp == UI_KEY_F2) {
		/* go to setup screen */
		gui_select(ms, config_setup);

		return 1; /* handled */
	}

	return 0;
}

/* generate status and display it */
static void update_status(void *arg)
{
	struct osmocom_ms *ms = arg;
	struct gsm_settings *set = &ms->settings;
	struct gsm_ui *gui = &ms->gui;
	int i, j = 0, k, n, lines = 0, has_network_name = 0, has_bottom_line = 0;
	char *p = gui->status_text;

	/* no timer, if no telnet connection */
	if (!UI_TARGET && llist_empty(&gui->ui.active_connections))
		return;

	/* if timer fires */
	if (gui->menu != MENU_STATUS)
		return;

	gui->ui.bottom_line = NULL;

	for (i = 0; i < GUI_NUM_STATUS; i++) {
		if (i == 0)
			has_network_name = 1;
		lines += status_screen[i].lines;
		if (!(set->status_enable & (1 << i)))
			continue;
		/* finish loop if number of lines exceed the definition */
		if (lines > GUI_NUM_STATUS_LINES)
			continue;
		/* special case where function keys' help is displayed */
		if (i == GUI_NUM_STATUS - 1) {
			has_bottom_line = 1;
			gui->ui.bottom_line = "menu setup";
			continue;
		}
		n = status_screen[i].display_func(ms, p);
		while (n--) {
			gui->status_lines[j] = p;
			p += (UI_COLS + 1);
			j++;
			if (j == GUI_NUM_STATUS_LINES)
				break;
		}
	}

	/* if network name is present */
	if (has_network_name) {
		/* if not all lines are occupied */
		if (j + has_bottom_line < UI_ROWS && j > 1) {
			/* insert space below network name */
			for (k = j; k > 1; k--)
				gui->status_lines[k] = gui->status_lines[k - 1];
			gui->status_lines[1] = "";
			j++;
		}
		/* if not all lines are occupied */
		if (j + has_bottom_line < UI_ROWS && j > 1) {
			/* insert space above network name */
			for (k = j; k > 0; k--)
				gui->status_lines[k] = gui->status_lines[k - 1];
			gui->status_lines[0] = "";
			j++;
		}
	}

	gui->ui.ud.listview.lines = j;
	gui->ui.ud.listview.text = gui->status_lines;
	gui->ui.title = NULL;
	ui_inst_refresh(&gui->ui);

	/* schedule next refresh */
	gui->timer.cb = update_status;
        gui->timer.data = ms;
        osmo_timer_schedule(&gui->timer, 1,0);
}

int gui_start(struct osmocom_ms *ms)
{
	/* go to status screen */
	ms->gui.menu = MENU_STATUS;
	ui_inst_init(&ms->gui.ui, &ui_listview, key_status_cb, beep_cb,
		telnet_cb);
	update_status(ms);

	return 0;
}

int gui_stop(struct osmocom_ms *ms)
{
	struct gsm_ui *gui = &ms->gui;

        osmo_timer_del(&gui->timer);

	return 0;
}

static int key_call_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
	struct gsm_call *call, *selected_call = NULL;
	int num_calls = 0, num_hold = 0;

	if (kp == UI_KEY_UP) {
		if (gui->selected_call > 0) {
			gui->selected_call--;
			gui_notify_call(ms);
		}

		return 1; /* handled */
	}
	if (kp == UI_KEY_DOWN) {
		gui->selected_call++;
		gui_notify_call(ms);

		return 1; /* handled */
	}
	llist_for_each_entry(call, &ms->mncc_entity.call_list, entry) {
		if (num_calls == gui->selected_call)
			selected_call = call;
		num_calls++;
		if (call->call_state == CALL_ST_HOLD)
			num_hold++;
	}
	if (!selected_call)
		return 1;
	if (kp == UI_KEY_HANGUP) {
		mncc_hangup(ms, gui->selected_call + 1);

		return 1; /* handled */
	}
	if (kp == UI_KEY_PICKUP) {
		if (selected_call->call_state == CALL_ST_MT_RING
		 || selected_call->call_state == CALL_ST_MT_KNOCK)
			mncc_answer(ms, gui->selected_call + 1);
		else
		if (selected_call->call_state == CALL_ST_HOLD)
			mncc_retrieve(ms, gui->selected_call + 1);

		return 1; /* handled */
	}
	if (kp == UI_KEY_F1) {
		/* only if all calls on hold */
		if (num_calls == num_hold) {
			gui->dialing[0] = '\0';
			/* go to dialing screen */
			gui_dialing(gui);
		}

		return 1; /* handled */
	}
	if (kp == UI_KEY_F2) {
		if (selected_call->call_state == CALL_ST_ACTIVE)
			mncc_hold(ms, gui->selected_call + 1);
		else
		if (selected_call->call_state == CALL_ST_HOLD)
			mncc_retrieve(ms, gui->selected_call + 1);

		return 1; /* handled */
	}
	if ((kp >= UI_KEY_0 && kp <= UI_KEY_9) || kp == UI_KEY_STAR
	 || kp == UI_KEY_HASH) {
		if (selected_call->call_state == CALL_ST_ACTIVE) {
			/* if dtmf is not supported */
			if (!ms->settings.cc_dtmf)
				return 1; /* handled */
			char dtmf[2];

			dtmf[0] = kp;
			dtmf[1] = '\0';
			mncc_dtmf(ms, gui->selected_call + 1, dtmf);
			return 1; /* handled */
		}
		/* only if all calls on hold */
		if (num_calls == num_hold) {
			gui->dialing[0] = kp;
			gui->dialing[1] = '\0';
			/* go to dialing screen */
			gui_dialing(gui);
		}

		return 1; /* handled */
	}

	return 0;
}

/* call instances have changed */
int gui_notify_call(struct osmocom_ms *ms)
{
	struct gsm_ui *gui = &ms->gui;
	struct gsm_call *call;
	const char *state;
	char *p = gui->status_text, *n;
	int len, shift;
	int j = 0, calls = 0, calls_on_hold = 0;
	int last_call_j_first = 0, selected_call_j_first = 0;
	int last_call_j_last = 0, selected_call_j_last = 0;
	struct gsm_call *last_call = NULL, *selected_call = NULL;

	/* gui disabled */
	if (!ms->settings.ui_port)
		return 0;

	if (gui->menu != MENU_STATUS
	 && gui->menu != MENU_CALL)
	 	return 0;

	if (gui->menu == MENU_STATUS) {
		ms->gui.menu = MENU_CALL;
		ui_inst_init(&ms->gui.ui, &ui_listview, key_call_cb, beep_cb,
			telnet_cb);
		gui->selected_call = 999;
		/* continue here */
	}

	llist_for_each_entry(call, &ms->mncc_entity.call_list, entry) {
		switch (call->call_state) {
		case CALL_ST_MO_INIT:
			state = "Dialing...";
			break;
		case CALL_ST_MO_PROC:
			state = "Proceeding";
			break;
		case CALL_ST_MO_ALERT:
			state = "Ringing";
			break;
		case CALL_ST_MT_RING:
			state = "Incomming";
			break;
		case CALL_ST_MT_KNOCK:
			state = "Knocking";
			break;
		case CALL_ST_ACTIVE:
			state = "Connected";
			break;
		case CALL_ST_HOLD:
			state = "On Hold";
			calls_on_hold++;
			break;
		case CALL_ST_DISC_TX:
			state = "Releasing";
			break;
		case CALL_ST_DISC_RX:
			state = "Hung Up";
			break;
		default:
			continue;
		}

		/* store first line of selected call */
		if (calls == gui->selected_call) {
			selected_call_j_first = j;
			selected_call = call;
		}
		/* set first line of last call */
		last_call_j_first = j;
		last_call = call;

		/* state */
		strncpy(p, state, UI_COLS);
		p[UI_COLS] = '\0';
		/* center */
		len = strlen(p);
		if (len + 1 < UI_COLS) {
			shift = (UI_COLS - len) / 2;
			memcpy(p + shift, p, len + 1);
			memset(p, ' ', shift);
		}
		gui->status_lines[j] = p;
		p += (UI_COLS + 1);
		j++;
		if (j == GUI_NUM_STATUS_LINES)
			break;

		/* number */
		n = call->number;
		while (1) {
			strncpy(p, n, UI_COLS);
			p[UI_COLS] = '\0';
			gui->status_lines[j] = p;
			p += (UI_COLS + 1);
			j++;
			if (j == GUI_NUM_STATUS_LINES)
				break;
			if (strlen(n) <= UI_COLS)
				break;
			n += UI_COLS;
		}
		if (j == GUI_NUM_STATUS_LINES)
			break;

		/* store last line of selected call */
		if (calls == gui->selected_call)
			selected_call_j_last = j;
		/* set last line of last call */
		last_call_j_last = j;

		/* empty line */
		p[0] = '\0';
		gui->status_lines[j] = p;
		p += (UI_COLS + 1);
		j++;
		if (j == GUI_NUM_STATUS_LINES)
			break;

		/* count calls */
		calls++;
	}

	/* return to status menu */
	if (!calls)
		return gui_start(ms);

	/* remove last empty line */
	if (j)
		j--;

	/* in case there are less calls than the selected one */
	if (!calls) {
		gui->selected_call = 0;
	} else if (gui->selected_call >= calls) {
		gui->selected_call = calls - 1;
		selected_call_j_first = last_call_j_first;
		selected_call_j_last = last_call_j_last;
		selected_call = last_call;
	}

	/* adjust vpos, so the selected call fits on display */
	if (selected_call_j_last - gui->ui.ud.listview.vpos > UI_ROWS - 2)
		gui->ui.ud.listview.vpos = selected_call_j_last - (UI_ROWS - 1);
	if (gui->ui.ud.listview.vpos > selected_call_j_first)
		gui->ui.ud.listview.vpos = selected_call_j_first;
	

	/* mark selected call */
	if (calls > 1)
		gui->status_text[selected_call_j_first * (UI_COLS + 1)] = '*';
	
	/* if only one call */
	if (calls == 1) {
		/* insert space above call state */
		memcpy(gui->status_lines + 1, gui->status_lines,
			j * sizeof(char *));
		gui->status_lines[0] = "";
		j++;
		if (j > 2) {
			/* insert space below call state */
			memcpy(gui->status_lines + 3, gui->status_lines + 2,
				(j - 2) * sizeof(char *));
			gui->status_lines[2] = "";
			j++;
		}
	}
	/* set bottom line */
	gui->ui.bottom_line = " ";
	if (calls && selected_call) {
		switch (selected_call->call_state) {
		case CALL_ST_ACTIVE:
			gui->ui.bottom_line = " hold";
			break;
		case CALL_ST_HOLD:
			/* offer new call, if all calls are on hold */
			if (calls_on_hold == calls)
				gui->ui.bottom_line = "new resume";
			else
				gui->ui.bottom_line = " resume";
			break;
		}
	}

	gui->ui.ud.listview.lines = j;
	gui->ui.ud.listview.text = gui->status_lines;
	ui_inst_refresh(&gui->ui);

	return 0;
}

static int key_select_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
	struct gui_select_line *menu = gui->select_menu;

	if (kp == UI_KEY_F1) {
		/* go paretn menu, or status screen, if none */
		if (menu[0].parent)
			gui_select(ms, menu[0].parent);
		else
			gui_start(ms);

		return 1; /* hanlded */
	}
	if (kp == UI_KEY_F2) {
		/* if menu is empty */
		if (!menu[ui->ud.selectview.cursor + 1].name)
			return 1; /* handled */
		/* go sub node */
		switch (menu[ui->ud.selectview.cursor + 1].type) {
		case SELECT_NODE:
			gui_select(ms, menu[ui->ud.selectview.cursor + 1].node);
			break;
		case SELECT_CHOOSE:
			gui_choose(ms, &menu[ui->ud.selectview.cursor + 1]);
			break;
		case SELECT_NUMBER:
			gui_number(ms, &menu[ui->ud.selectview.cursor + 1]);
			break;
		case SELECT_INT:
			gui_int(ms, &menu[ui->ud.selectview.cursor + 1]);
			break;
		case SELECT_FIXINT:
			gui_fixint(ms, menu,
				&menu[ui->ud.selectview.cursor + 1]);
			break;
		case SELECT_FIXSTRING:
			gui_fixstring(ms, menu,
				&menu[ui->ud.selectview.cursor + 1]);
			break;
		}

		return 1; /* hanlded */
	}

	return 0;
}

/* initialize a select menu */
static int gui_select(struct osmocom_ms *ms, struct gui_select_line *menu)
{
	struct gsm_ui *gui = &ms->gui;
	char *p = gui->status_text;
	int i;

	gui->menu = MENU_SELECT;
	ui_inst_init(&gui->ui, &ui_selectview, key_select_cb, beep_cb,
		telnet_cb);

	/* set menu */
	gui->select_menu = menu;
	gui->ui.title = menu->title;
	menu++;

	/* create list */
	for (i = 0; menu[i].name; i++) {
		if (i == GUI_NUM_STATUS_LINES)
			break;
		strncpy(p, menu[i].name, UI_COLS);
		p[UI_COLS] = '\0';
		gui->status_lines[i] = p;
		p += UI_COLS + 1;
	}

	gui->ui.bottom_line = "back enter";

	gui->ui.ud.selectview.lines = i;
	gui->ui.ud.selectview.text = gui->status_lines;
	ui_inst_refresh(&gui->ui);

	return 0;
}

static int key_choose_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
	struct gui_select_line *menu = gui->choose_menu;
	struct gui_choose_set *set = menu->set;
	int rc;

	if (kp == UI_KEY_F1) {
		/* go back to selection */
		gui_select(ms, gui->select_menu);

		return 1; /* hanlded */
	}
	if (kp == UI_KEY_F2) {
		/* set selection */
		set += ui->ud.selectview.cursor;
		rc = menu->cmd(ms, &set->value);
		if (rc)
			gui_chosen(ms, NULL);
		else
			gui_chosen(ms, set->name);

		return 1; /* hanlded */
	}

	return 0;
}

static int gui_choose(struct osmocom_ms *ms, struct gui_select_line *menu)
{
	struct gsm_ui *gui = &ms->gui;
	char *p = gui->status_text;
	int i;
	struct gui_choose_set *set = menu->set;
	int cursor = 0;
	int value;

	ui_inst_init(&gui->ui, &ui_selectview, key_choose_cb, beep_cb,
		telnet_cb);

	/* set menu */
	gui->choose_menu = menu;
	gui->ui.title = menu->name;

	value = *(int *)menu->query(ms);

	/* create list of items to choose */
	for (i = 0; set[i].name; i++) {
		if (i == GUI_NUM_STATUS_LINES)
			break;
		if (value == set[i].value)
			cursor = i;
		strncpy(p, set[i].name, UI_COLS);
		p[UI_COLS] = '\0';
		gui->status_lines[i] = p;
		p += UI_COLS + 1;
	}

	gui->ui.bottom_line = "back set";

	gui->ui.ud.selectview.lines = i;
	gui->ui.ud.selectview.text = gui->status_lines;
	gui->ui.ud.selectview.cursor = cursor;
	ui_inst_refresh(&gui->ui);

	return 0;
}

static int key_number_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
	struct gui_select_line *menu = gui->choose_menu;
	int rc;

	if (kp == UI_KEY_HANGUP) {
		/* go back to selection */
		gui_select(ms, gui->select_menu);

		return 1; /* hanlded */
	}
	if (kp == UI_KEY_PICKUP) {
		char *number = ui->ud.stringview.number;
		int i;

		/* check if number is valid */
		for (i = 0; i < strlen(number); i++) {
			if ((i != 0 && number[i] == '+')
			 || !strchr("01234567890*#abc+", number[i])) {
			 	/* point to error digit */
				ui->ud.stringview.pos = i;
				ui_inst_refresh(ui);
				return 1; /* handled */
			}
		}
		/* set selection */
		rc = menu->cmd(ms, gui->dialing);
		if (rc)
			gui_chosen(ms, NULL);
		else
			gui_chosen(ms, gui->dialing);

		return 1; /* hanlded */
	}

	return 0;
}

static int gui_number(struct osmocom_ms *ms, struct gui_select_line *menu)
{
	struct gsm_ui *gui = &ms->gui;

	ui_inst_init(&gui->ui, &ui_stringview, key_number_cb, beep_cb,
		telnet_cb);

	/* set menu */
	gui->choose_menu = menu;
	gui->ui.title = menu->name;
	strncpy(gui->dialing, menu->query(ms), sizeof(gui->dialing) - 1);
	gui->ui.ud.stringview.number = gui->dialing;
	gui->ui.ud.stringview.num_len = menu->digits + 1;
	gui->ui.ud.stringview.pos = strlen(gui->dialing);
	ui_inst_refresh(&gui->ui);

	return 0;
}

static int key_int_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
	struct gui_select_line *menu = gui->choose_menu;
	int rc;

	if (kp == UI_KEY_F1) {
		/* go back to selection */
		gui_select(ms, gui->select_menu);

		return 1; /* hanlded */
	}
	if (kp == UI_KEY_F2) {
		int value;

		if (ui->ud.intview.sign)
			value = 0 - ui->ud.intview.value;
		else
			value = ui->ud.intview.value;
		/* if entered value exceeds range, let ui fix that */
		if (value < menu->min || value > menu->max)
			return 0; /* unhandled */
		/* set selection */
		rc = menu->cmd(ms, &value);
		if (rc)
			gui_chosen(ms, NULL);
		else {
			char val_str[16];

			sprintf(val_str, "%d", value);
			gui_chosen(ms, val_str);
		}

		return 1; /* hanlded */
	}

	return 0;
}

static int gui_int(struct osmocom_ms *ms, struct gui_select_line *menu)
{
	struct gsm_ui *gui = &ms->gui;
	int value;

	ui_inst_init(&gui->ui, &ui_intview, key_int_cb, beep_cb,
		telnet_cb);

	/* set menu */
	gui->choose_menu = menu;
	gui->ui.title = menu->name;
	value = *(int *)menu->query(ms);
	if (value < 0) {
		gui->ui.ud.intview.value = 0 - value;
		gui->ui.ud.intview.sign = 1;
	} else {
		gui->ui.ud.intview.value = value;
		gui->ui.ud.intview.sign = 0;
	}
	gui->ui.ud.intview.min = menu->min;
	gui->ui.ud.intview.max = menu->max;
	gui->ui.bottom_line = "back set";
	ui_inst_refresh(&gui->ui);

	return 0;
}

static int gui_fixint(struct osmocom_ms *ms, struct gui_select_line *menu,
	struct gui_select_line *value)
{
	struct gsm_ui *gui = &ms->gui;
	int rc;

	/* set fixed value */
	gui->choose_menu = menu;
	rc = value->cmd(ms, &value->fix);
	if (rc)
		gui_chosen(ms, NULL);
	else {
		gui_chosen(ms, value->name);
	}

	return 0;
}

static int gui_fixstring(struct osmocom_ms *ms, struct gui_select_line *menu,
	struct gui_select_line *value)
{
	struct gsm_ui *gui = &ms->gui;
	int rc;

	/* set fixed value */
	gui->choose_menu = menu;
	rc = value->cmd(ms, value->fixstring);
	if (rc)
		gui_chosen(ms, NULL);
	else {
		gui_chosen(ms, value->name);
	}

	return 0;
}

static int key_chosen_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);

	/* go back to selection */
	gui_select(ms, gui->select_menu);

	return 1; /* handled */
}

static int gui_chosen(struct osmocom_ms *ms, const char *value)
{
	struct gsm_ui *gui = &ms->gui;
	char *p = gui->status_text;
	struct gui_select_line *menu = gui->choose_menu;
	int j = 0;

	ui_inst_init(&gui->ui, &ui_listview, key_chosen_cb, beep_cb,
		telnet_cb);

	/* set menu */
	gui->ui.title = menu->name;

	p[0] = '\0';
	gui->status_lines[j++] = p;
	p += UI_COLS + 1;
	if (value) {
		if (menu->restart)
			strcpy(p, "after reset:");
		else
			strcpy(p, "is now:");
		p[UI_COLS] = '\0';
		gui->status_lines[j++] = p;
		p += UI_COLS + 1;
		p[0] = '\0';
		gui->status_lines[j++] = p;
		p += UI_COLS + 1;
		while (*value) {
			if (j == GUI_NUM_STATUS_LINES)
				break;
			strncpy(p, value, UI_COLS);
			p[UI_COLS] = '\0';
			value += strlen(p);
			gui->status_lines[j++] = p;
			p += UI_COLS + 1;
		}
	} else {
		strcpy(p, "failed!");
		p[UI_COLS] = '\0';
		gui->status_lines[j++] = p;
		p += UI_COLS + 1;
	}

	gui->ui.bottom_line = "back ";

	gui->ui.ud.selectview.lines = j;
	gui->ui.ud.selectview.text = gui->status_lines;
	ui_inst_refresh(&gui->ui);

	return 0;
}

static int key_supserv_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);

	if ((kp >= UI_KEY_0 && kp <= UI_KEY_9) || kp == UI_KEY_STAR
	 || kp == UI_KEY_HASH) {
		/* no key if pending */
		if (!gui->ss_active)
			return 0;
		gui->dialing[0] = kp;
		gui->dialing[1] = '\0';
		/* go to input screen */
		gui_input(gui, MENU_SS_INPUT);

		return 1; /* handled */
	}
	if (kp == UI_KEY_PICKUP) {
		if (!gui->ss_active)
			return 0;
		gui->dialing[0] = '\0';
		/* go to input screen */
		gui_input(gui, MENU_SS_INPUT);

		return 1; /* handled */
	}
	if (kp == UI_KEY_F2) {
		/* no key if pending */
		if (gui->ss_pending)
			return 0;
		/* go back to start */
		gui_start(ms);
		/* terminate SS connection */
		if (gui->ss_active)
			ss_send(ms, "hangup", 0);

		return 1; /* hanlded */
	}

	return 0;
}

static int gui_supserv(struct gsm_ui *gui, int clear) {
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
	struct ui_inst *ui = &gui->ui;

	/* if we don't want to keep our list buffer */
	if (clear)
		gui->ss_lines = 0;

	/* go to supplementary services screen */
	gui->menu = MENU_SUPSERV;
	ui_inst_init(ui, &ui_listview, key_supserv_cb, beep_cb,
		telnet_cb);
	ui->title = "SS Request";
	ui->bottom_line = " "; 
	gui->ui.ud.listview.lines = gui->ss_lines;
	gui->ui.ud.listview.text = gui->status_lines;
	ui_inst_refresh(ui);

	/* send request to supserv process */
	gui->ss_active = 0;
	gui->ss_pending = 1; /* must be set prior call, gui_notify_ss might be
			       called there
			     */
	if (gui->dialing[0])
		ss_send(ms, gui->dialing, 0);

	return 0;
}

int gui_notify_ss(struct osmocom_ms *ms, const char *fmt, ...)
{
	struct gsm_ui *gui = &ms->gui;
	struct ui_inst *ui = &gui->ui;
	char buffer[1000], *b = buffer, *start, *end, *p;
	int j = gui->ss_lines;
	va_list args;

	/* if connection is pending, clear listview buffer, otherwise append */
	if (gui->ss_pending) {
		j = 0;
		gui->ss_pending = 0;
	}
	p = gui->status_text + j * (UI_COLS + 1);

	/* not our process */
	if (gui->menu != MENU_SS_INPUT
	 && gui->menu != MENU_SUPSERV)
		return 0;

	/* change back to supserv display */
	if (gui->menu != MENU_SUPSERV) {
		gui->menu = MENU_SUPSERV;
		ui_inst_init(ui, &ui_listview, key_supserv_cb, beep_cb,
			telnet_cb);
		ui->title = "SS Request";
		gui->ui.ud.listview.lines = gui->ss_lines;
		gui->ui.ud.listview.text = gui->status_lines;
	}

	/* end of process indication */
	if (!fmt) {
		gui->ss_active = 0;
		ui->bottom_line = " back"; 
		ui_inst_refresh(&gui->ui);

		return 0;
	} else
		gui->ss_active = 1;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
	buffer[sizeof(buffer) - 1] = '\0';
	va_end(args);

	ui->title = NULL;
	/* print buffer to listview */
	while (*b) {
		if (j == GUI_NUM_STATUS_LINES)
			break;
		/* find start and end of a line. the line ends with \n or \0 */
		start = b;
		end = strchr(b, '\n');
		if (end)
			b = end + 1;
		else {
			end = b + strlen(b);
			b = end;
		}
		/* if line is longer than display width */
		if (end - start > UI_COLS) {
			/* loop until next word exceeds line */
			end = start;
			while ((b = strchr(end, ' '))) {
				if (!b)
					break;
				if (b - start > UI_COLS)
					break;
				end = b + 1;
			}
			/* if word is longer than line, we must break inside */
			if (start == end) {
				end = start + UI_COLS;
				b = end;
			} else {
				b = end;
				end--; /* remove last space */
			}
		}
		/* copy line into buffer */
		if (end - start)
			memcpy(p, start, end - start);
		p[end - start] = '\0';
		gui->status_lines[j++] = p;
		p += UI_COLS + 1;
	}
	gui->ss_lines = gui->ui.ud.listview.lines = j;
	ui->bottom_line = " end"; 
	ui_inst_refresh(&gui->ui);

	return 0;
}

static int key_input_cb(struct ui_inst *ui, enum ui_key kp);

static int gui_input(struct gsm_ui *gui, int menu)
{
	struct ui_inst *ui = &gui->ui;

	/* go to input screen */
	gui->menu = menu;
	ui_inst_init(ui, &ui_stringview, key_input_cb, beep_cb,
		telnet_cb);
	ui->title = "Select:";
	ui->ud.stringview.number = gui->dialing;
	ui->ud.stringview.num_len = sizeof(gui->dialing);
	ui->ud.stringview.pos = 1;
	ui_inst_refresh(ui);

	return 0;
}

static int key_input_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);

	if (kp == UI_KEY_PICKUP) {
		/* go to previous screen and use input */
		gui_supserv(gui, 1);

		return 1; /* handled */
	}
	if (kp == UI_KEY_HANGUP) {
		/* go to previous screen */
		gui->dialing[0] = '\0';
		gui_supserv(gui, 0);

		return 1; /* handled */
	}

	return 0;
}

