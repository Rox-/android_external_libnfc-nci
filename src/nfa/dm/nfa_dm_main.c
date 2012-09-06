/*****************************************************************************
**
**  Name:           nfa_dm_main.c
**
**  Description:    This is the main implementation file for the NFA
**                  device manager.
**
**  Copyright (c) 2010-2012, Broadcom Corp., All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
**
*****************************************************************************/

#include <string.h>
#include "nfa_api.h"
#include "nfa_sys.h"
#include "nfa_dm_int.h"
#include "nfa_sys_int.h"


/*****************************************************************************
** Constants and types
*****************************************************************************/
static const tNFA_SYS_REG nfa_dm_sys_reg =
{
    nfa_dm_sys_enable,
    nfa_dm_evt_hdlr,
    nfa_dm_sys_disable,
    nfa_dm_proc_nfcc_power_mode
};


tNFA_DM_CB  nfa_dm_cb = {FALSE};


#define NFA_DM_NUM_ACTIONS  (NFA_DM_MAX_EVT & 0x00ff)

/* type for action functions */
typedef BOOLEAN (*tNFA_DM_ACTION) (tNFA_DM_MSG *p_data);

/* action function list */
const tNFA_DM_ACTION nfa_dm_action[] =
{
    /* device manager local device API events */
    nfa_dm_enable,                      /* NFA_DM_API_ENABLE_EVT                */
    nfa_dm_disable,                     /* NFA_DM_API_DISABLE_EVT               */
    nfa_dm_set_config,                  /* NFA_DM_API_SET_CONFIG_EVT            */
    nfa_dm_get_config,                  /* NFA_DM_API_GET_CONFIG_EVT            */
    nfa_dm_act_request_excl_rf_ctrl,    /* NFA_DM_API_REQUEST_EXCL_RF_CTRL_EVT  */
    nfa_dm_act_release_excl_rf_ctrl,    /* NFA_DM_API_RELEASE_EXCL_RF_CTRL_EVT  */
    nfa_dm_act_enable_polling,          /* NFA_DM_API_ENABLE_POLLING_EVT        */
    nfa_dm_act_disable_polling,         /* NFA_DM_API_DISABLE_POLLING_EVT       */
    nfa_dm_act_send_raw_frame,          /* NFA_DM_API_RAW_FRAME_EVT             */
    nfa_dm_set_p2p_listen_tech,         /* NFA_DM_API_SET_P2P_LISTEN_TECH_EVT   */
    nfa_dm_act_start_rf_discovery,      /* NFA_DM_API_START_RF_DISCOVERY_EVT    */
    nfa_dm_act_stop_rf_discovery,       /* NFA_DM_API_STOP_RF_DISCOVERY_EVT     */
    nfa_dm_act_set_rf_disc_duration,    /* NFA_DM_API_SET_RF_DISC_DURATION_EVT  */
    nfa_dm_act_select,                  /* NFA_DM_API_SELECT_EVT                */
    nfa_dm_act_update_rf_params,        /* NFA_DM_API_UPDATE_RF_PARAMS_EVT      */
    nfa_dm_act_deactivate,              /* NFA_DM_API_DEACTIVATE_EVT            */
    nfa_dm_act_power_off_sleep,         /* NFA_DM_API_POWER_OFF_SLEEP_EVT       */
    nfa_dm_ndef_reg_hdlr,               /* NFA_DM_API_REG_NDEF_HDLR_EVT         */
    nfa_dm_ndef_dereg_hdlr,             /* NFA_DM_API_DEREG_NDEF_HDLR_EVT       */
    nfa_dm_act_reg_vsc,                 /* NFA_DM_API_REG_VSC_EVT               */
    nfa_dm_act_send_vsc,                /* NFA_DM_API_SEND_VSC_EVT              */
    nfa_dm_act_disable_timeout,         /* NFA_DM_TIMEOUT_DISABLE_EVT           */
    nfa_dm_act_nfc_cback_data           /* NFA_DM_NFC_CBACK_DATA_EVT            */
};

/*****************************************************************************
** Local function prototypes
*****************************************************************************/
#if (BT_TRACE_VERBOSE == TRUE)
static char *nfa_dm_evt_2_str (UINT16 event);
#endif
/*******************************************************************************
**
** Function         nfa_dm_init
**
** Description      Initialises the NFC device manager
**
** Returns          void
**
*******************************************************************************/
void nfa_dm_init (void)
{
    NFA_TRACE_DEBUG0 ("nfa_dm_init ()");
    memset (&nfa_dm_cb, 0, sizeof (tNFA_DM_CB));
    nfa_dm_cb.poll_disc_handle = NFA_HANDLE_INVALID;
    nfa_dm_cb.disc_cb.disc_duration = NFA_DM_DISC_DURATION_POLL;
    nfa_dm_cb.nfcc_pwr_mode    = NFA_DM_PWR_MODE_FULL;

    /* register message handler on NFA SYS */
    nfa_sys_register (NFA_ID_DM, &nfa_dm_sys_reg);
}

/*******************************************************************************
**
** Function         nfa_dm_evt_hdlr
**
** Description      Event handling function for DM
**
**
** Returns          void
**
*******************************************************************************/
BOOLEAN nfa_dm_evt_hdlr (BT_HDR *p_msg)
{
    BOOLEAN freebuf = TRUE;
    UINT16  event = p_msg->event & 0x00ff;

#if (BT_TRACE_VERBOSE == TRUE)
    NFA_TRACE_EVENT2 ("nfa_dm_evt_hdlr event: %s (0x%02x)", nfa_dm_evt_2_str (event), event);
#else
    NFA_TRACE_EVENT1 ("nfa_dm_evt_hdlr event: 0x%x", event);
#endif

    /* execute action functions */
    if (event < NFA_DM_NUM_ACTIONS)
    {
        freebuf = (*nfa_dm_action[event]) ((tNFA_DM_MSG*) p_msg);
    }
    /* if vendor specific event handler is registered */
    if (nfa_dm_cb.p_vs_evt_hdlr)
    {
        (*nfa_dm_cb.p_vs_evt_hdlr) (p_msg);
    }

    return freebuf;
}

/*******************************************************************************
**
** Function         nfa_dm_disc_state_cback
**
** Description      Wait for discovery suspended
**
** Returns          void
**
*******************************************************************************/
void nfa_dm_disc_state_cback (UINT8 state)
{
    /* don't need to check state at this moment */
    nfa_dm_disable_complete ();
}

/*******************************************************************************
**
** Function         nfa_dm_sys_disable
**
** Description      This function is called after all subsystems have been disabled.
**
** Returns          void
**
*******************************************************************************/
void nfa_dm_sys_disable (void)
{
    /* Disable the DM sub-system */
    /* If discovery state is not IDLE or DEACTIVATED and graceful disable, */
    /* then we need to deactivate link or stop discovery                   */

    if (nfa_sys_is_graceful_disable ())
    {
        if (nfa_dm_cb.disc_cb.disc_state == NFA_DM_RFST_IDLE)
        {
            /* discovery is not started */
            nfa_dm_disable_complete ();
        }
        else
        {
            /* probably waiting to be disabled */
            NFA_TRACE_ERROR2 ("DM disc_state state = %d disc_flags:0x%x", nfa_dm_cb.disc_cb.disc_state, nfa_dm_cb.disc_cb.disc_flags);
        }

    }
    else
    {
        nfa_dm_disable_complete ();
    }
}

/*******************************************************************************
**
** Function         nfa_dm_is_protocol_supported
**
** Description      Check if protocol is supported by RW module
**
** Returns          TRUE if protocol is supported by NFA
**
*******************************************************************************/
BOOLEAN nfa_dm_is_protocol_supported (tNFC_PROTOCOL protocol, UINT8 sel_res)
{
    return (  (protocol == NFC_PROTOCOL_T1T)
            ||((protocol == NFC_PROTOCOL_T2T) && (sel_res == NFC_SEL_RES_NFC_FORUM_T2T))
            ||(protocol == NFC_PROTOCOL_T3T)
            ||(protocol == NFC_PROTOCOL_ISO_DEP)
            ||(protocol == NFC_PROTOCOL_NFC_DEP)
            ||(protocol == NFC_PROTOCOL_15693)  );
}
/*******************************************************************************
**
** Function         nfa_dm_is_active
**
** Description      check if all modules of NFA is done with enable process and
**                  NFA is not restoring NFCC.
**
** Returns          TRUE, if NFA_DM_ENABLE_EVT is reported and it is not restoring NFCC
**
*******************************************************************************/
BOOLEAN nfa_dm_is_active (void)
{
    if (  (nfa_dm_cb.flags  & NFA_DM_FLAGS_DM_IS_ACTIVE)
        &&((nfa_dm_cb.flags & (NFA_DM_FLAGS_ENABLE_EVT_PEND | NFA_DM_FLAGS_NFCC_IS_RESTORING)) == 0)  )
    {
        return TRUE;
    }
    else
        return FALSE;
}
/*******************************************************************************
**
** Function         nfa_dm_check_set_config
**
** Description      Update config parameters only if it's different from NFCC
**
**
** Returns          tNFA_STATUS
**
*******************************************************************************/
tNFA_STATUS nfa_dm_check_set_config (UINT8 tlv_list_len, UINT8 *p_tlv_list, BOOLEAN app_init)
{
    UINT8 type, len, *p_value, *p_stored, max_len;
    UINT8 xx = 0, updated_len = 0, *p_cur_len;
    BOOLEAN update;
    tNFC_STATUS nfc_status;
    UINT32 cur_bit;

    NFA_TRACE_DEBUG0 ("nfa_dm_check_set_config ()");

    /* We only allow 32 pending SET_CONFIGs */
    if (nfa_dm_cb.setcfg_pending_num >= NFA_DM_SETCONFIG_PENDING_MAX)
    {
        NFA_TRACE_ERROR0 ("nfa_dm_check_set_config () error: pending number of SET_CONFIG exceeded");
        return NFA_STATUS_FAILED;
    }

    while (tlv_list_len - xx >= 2) /* at least type and len */
    {
        update  = FALSE;
        type    = *(p_tlv_list + xx);
        len     = *(p_tlv_list + xx + 1);
        p_value = p_tlv_list + xx + 2;
        p_cur_len = NULL;

        switch (type)
        {
        case NFC_PMID_TOTAL_DURATION:
            p_stored = nfa_dm_cb.params.total_duration;
            max_len  = NCI_PARAM_LEN_TOTAL_DURATION;
            break;

        /*
        **  Listen A Configuration
        */
        case NFC_PMID_LA_BIT_FRAME_SDD:
            p_stored  = nfa_dm_cb.params.la_bit_frame_sdd;
            max_len   = NCI_PARAM_LEN_LA_BIT_FRAME_SDD;
            p_cur_len = &nfa_dm_cb.params.la_bit_frame_sdd_len;
            break;
        case NFC_PMID_LA_PLATFORM_CONFIG:
            p_stored  = nfa_dm_cb.params.la_platform_config;
            max_len   = NCI_PARAM_LEN_LA_PLATFORM_CONFIG;
            p_cur_len = &nfa_dm_cb.params.la_platform_config_len;
            break;
        case NFC_PMID_LA_SEL_INFO:
            p_stored  = nfa_dm_cb.params.la_sel_info;
            max_len   = NCI_PARAM_LEN_LA_SEL_INFO;
            p_cur_len = &nfa_dm_cb.params.la_sel_info_len;
            break;
        case NFC_PMID_LA_NFCID1:
            p_stored  = nfa_dm_cb.params.la_nfcid1;
            max_len   = NCI_NFCID1_MAX_LEN;
            p_cur_len = &nfa_dm_cb.params.la_nfcid1_len;
            break;
        case NFC_PMID_LA_HIST_BY:
            p_stored  = nfa_dm_cb.params.la_hist_by;
            max_len   = NCI_MAX_HIS_BYTES_LEN;
            p_cur_len = &nfa_dm_cb.params.la_hist_by_len;
            break;

        /*
        **  Listen B Configuration
        */
        case NFC_PMID_LB_SENSB_INFO:
            p_stored  = nfa_dm_cb.params.lb_sensb_info;
            max_len   = NCI_PARAM_LEN_LB_SENSB_INFO;
            p_cur_len = &nfa_dm_cb.params.lb_sensb_info_len;
            break;
        case NFC_PMID_LB_NFCID0:
            p_stored  = nfa_dm_cb.params.lb_nfcid0;
            max_len   = NCI_PARAM_LEN_LB_NFCID0;
            p_cur_len = &nfa_dm_cb.params.lb_nfcid0_len;
            break;
        case NFC_PMID_LB_APPDATA:
            p_stored  = nfa_dm_cb.params.lb_appdata;
            max_len   = NCI_PARAM_LEN_LB_APPDATA;
            p_cur_len = &nfa_dm_cb.params.lb_appdata_len;
            break;
        case NFC_PMID_LB_ADC_FO:
            p_stored  = nfa_dm_cb.params.lb_adc_fo;
            max_len   = NCI_PARAM_LEN_LB_ADC_FO;
            p_cur_len = &nfa_dm_cb.params.lb_adc_fo_len;
            break;
        case NFC_PMID_LB_H_INFO:
            p_stored  = nfa_dm_cb.params.lb_h_info;
            max_len   = NCI_MAX_ATTRIB_LEN;
            p_cur_len = &nfa_dm_cb.params.lb_h_info_len;
            break;

        /*
        **  Listen F Configuration
        */
        case NFC_PMID_LF_PROTOCOL:
            p_stored  = nfa_dm_cb.params.lf_protocol;
            max_len   = NCI_PARAM_LEN_LF_PROTOCOL;
            p_cur_len = &nfa_dm_cb.params.lf_protocol_len;
            break;
        case NFC_PMID_LF_T3T_FLAGS2:
            p_stored  = nfa_dm_cb.params.lf_t3t_flags2;
            max_len   = NCI_PARAM_LEN_LF_T3T_FLAGS2;
            p_cur_len = &nfa_dm_cb.params.lf_t3t_flags2_len;
            break;
        case NFC_PMID_LF_T3T_PMM:
            p_stored = nfa_dm_cb.params.lf_t3t_pmm;
            max_len  = NCI_PARAM_LEN_LF_T3T_PMM;
            break;

        /*
        **  ISO-DEP and NFC-DEP Configuration
        */
        case NFC_PMID_FWI:
            p_stored = nfa_dm_cb.params.fwi;
            max_len  = NCI_PARAM_LEN_FWI;
            break;
        case NFC_PMID_WT:
            p_stored = nfa_dm_cb.params.wt;
            max_len  = NCI_PARAM_LEN_WT;
            break;
        case NFC_PMID_ATR_REQ_GEN_BYTES:
            p_stored  = nfa_dm_cb.params.atr_req_gen_bytes;
            max_len   = NCI_MAX_GEN_BYTES_LEN;
            p_cur_len = &nfa_dm_cb.params.atr_req_gen_bytes_len;
            break;
        case NFC_PMID_ATR_RES_GEN_BYTES:
            p_stored  = nfa_dm_cb.params.atr_res_gen_bytes;
            max_len   = NCI_MAX_GEN_BYTES_LEN;
            p_cur_len = &nfa_dm_cb.params.atr_res_gen_bytes_len;
            break;
        default:
            /*
            **  Listen F Configuration
            */
            if ((type >= NFC_PMID_LF_T3T_ID1) && (type < NFC_PMID_LF_T3T_ID1 + NFA_CE_LISTEN_INFO_MAX))
            {
                p_stored = nfa_dm_cb.params.lf_t3t_id[type - NFC_PMID_LF_T3T_ID1];
                max_len  = NCI_PARAM_LEN_LF_T3T_ID;
            }
            else
            {
                /* we don't stored this config items */
                update   = TRUE;
                p_stored = NULL;
            }
            break;
        }

        if ((p_stored)&&(len <= max_len))
        {
            if (p_cur_len)
            {
                if (*p_cur_len != len)
                {
                    *p_cur_len = len;
                    update = TRUE;
                }
                else if (memcmp (p_value, p_stored, len))
                {
                    update = TRUE;
                }
            }
            else if (len == max_len)  /* fixed length */
            {
                if (memcmp (p_value, p_stored, len))
                {
                    update = TRUE;
                }
            }
        }

        if (update)
        {
            /* we don't store this type */
            if (p_stored)
            {
                memcpy (p_stored, p_value, len);
            }

            /* If need to change TLV in the original list. (Do not modify list if app_init) */
            if ((updated_len != xx) && (!app_init))
            {
                memcpy (p_tlv_list + updated_len, p_tlv_list + xx, (len + 2));
            }
            updated_len += (len + 2);
        }
        xx += len + 2;  /* move to next TLV */
    }

    /* If any TVLs to update, or if the SetConfig was initiated by the application, then send the SET_CONFIG command */
    if (updated_len || app_init)
    {
        if ((nfc_status = NFC_SetConfig (updated_len, p_tlv_list)) == NFC_STATUS_OK)
        {
            /* Keep track of whether we will need to notify NFA_DM_SET_CONFIG_EVT on NFC_SET_CONFIG_REVT */

            /* Get the next available bit offset for this setconfig (based on how many SetConfigs are outstanding) */
            cur_bit = (UINT32) (1 << nfa_dm_cb.setcfg_pending_num);

            /* If setconfig is due to NFA_SetConfig: then set the bit (NFA_DM_SET_CONFIG_EVT needed on NFC_SET_CONFIG_REVT) */
            if (app_init)
            {
                nfa_dm_cb.setcfg_pending_mask |= cur_bit;
            }
            /* Otherwise setconfig is internal: clear the bit (NFA_DM_SET_CONFIG_EVT not needed on NFC_SET_CONFIG_REVT) */
            else
            {
                nfa_dm_cb.setcfg_pending_mask &= ~cur_bit;
            }

            /* Increment setcfg_pending counter */
            nfa_dm_cb.setcfg_pending_num++;
        }
        return (nfc_status);

    }
    else
    {
        return NFA_STATUS_OK;
    }
}

#if (BT_TRACE_VERBOSE == TRUE)
/*******************************************************************************
**
** Function         nfa_dm_nfc_revt_2_str
**
** Description      convert nfc revt to string
**
*******************************************************************************/
static char *nfa_dm_evt_2_str (UINT16 event)
{
    switch (NFA_SYS_EVT_START (NFA_ID_DM) | event)
    {
    case NFA_DM_API_ENABLE_EVT:
        return "NFA_DM_API_ENABLE_EVT";

    case NFA_DM_API_DISABLE_EVT:
        return "NFA_DM_API_DISABLE_EVT";

    case NFA_DM_API_SET_CONFIG_EVT:
        return "NFA_DM_API_SET_CONFIG_EVT";

    case NFA_DM_API_GET_CONFIG_EVT:
        return "NFA_DM_API_GET_CONFIG_EVT";

    case NFA_DM_API_REQUEST_EXCL_RF_CTRL_EVT:
        return "NFA_DM_API_REQUEST_EXCL_RF_CTRL_EVT";

    case NFA_DM_API_RELEASE_EXCL_RF_CTRL_EVT:
        return "NFA_DM_API_RELEASE_EXCL_RF_CTRL_EVT";

    case NFA_DM_API_ENABLE_POLLING_EVT:
        return "NFA_DM_API_ENABLE_POLLING_EVT";

    case NFA_DM_API_DISABLE_POLLING_EVT:
        return "NFA_DM_API_DISABLE_POLLING_EVT";

    case NFA_DM_API_RAW_FRAME_EVT:
        return "NFA_DM_API_RAW_FRAME_EVT";

    case NFA_DM_API_SET_P2P_LISTEN_TECH_EVT:
        return "NFA_DM_API_SET_P2P_LISTEN_TECH_EVT";

    case NFA_DM_API_START_RF_DISCOVERY_EVT:
        return "NFA_DM_API_START_RF_DISCOVERY_EVT";

    case NFA_DM_API_STOP_RF_DISCOVERY_EVT:
        return "NFA_DM_API_STOP_RF_DISCOVERY_EVT";

    case NFA_DM_API_SET_RF_DISC_DURATION_EVT:
        return "NFA_DM_API_SET_RF_DISC_DURATION_EVT";

    case NFA_DM_API_SELECT_EVT:
        return "NFA_DM_API_SELECT_EVT";

    case NFA_DM_API_UPDATE_RF_PARAMS_EVT:
        return "NFA_DM_API_UPDATE_RF_PARAMS_EVT";

    case NFA_DM_API_DEACTIVATE_EVT:
        return "NFA_DM_API_DEACTIVATE_EVT";

    case NFA_DM_API_POWER_OFF_SLEEP_EVT:
        return "NFA_DM_API_POWER_OFF_SLEEP_EVT";

    case NFA_DM_API_REG_NDEF_HDLR_EVT:
        return "NFA_DM_API_REG_NDEF_HDLR_EVT";

    case NFA_DM_API_DEREG_NDEF_HDLR_EVT:
        return "NFA_DM_API_DEREG_NDEF_HDLR_EVT";

    case NFA_DM_TIMEOUT_DISABLE_EVT:
        return "NFA_DM_TIMEOUT_DISABLE_EVT";

    case NFA_DM_NFC_CBACK_DATA_EVT:
        return "NFA_DM_NFC_CBACK_DATA_EVT";
    }

    return "Unknown or Vendor Specific";
}
#endif /* BT_TRACE_VERBOSE */