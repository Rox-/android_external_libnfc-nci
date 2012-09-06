/*****************************************************************************
**
**  Name:           nfa_dm_brcm.c
**
**  Description:    This file contains the functions for BRCM specific.
**
**  Copyright (c) 2011-2012, Broadcom Corp., All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
**
*****************************************************************************/
#include <string.h>
#include "nfa_sys.h"
#include "nfa_api.h"
#include "nfa_dm_int.h"
#include "nfa_sys_int.h"
#include "nfa_brcm_api.h"

#if (NFC_BRCM_VS_INCLUDED == TRUE)
#include "hcidefs.h"
#include "nfa_brcm_int.h"

#ifndef NFA_DM_LPTD_PARAM_LEN
#define NFA_DM_LPTD_PARAM_LEN           (18)
#endif

#ifndef NFA_DM_LPTD_PARAM_EXTRA_LEN
#define NFA_DM_LPTD_PARAM_EXTRA_LEN     (2 + 14) /* 2 is for T, L. 14 is  potential extra bytes for LPTD */
#endif

#define NFA_I93_RW_CFG_LEN              (5)
#define NFA_I93_RW_CFG_PARAM_LEN        (3)
#define NFA_I93_AFI                     (0)
#define NFA_I93_ENABLE_SMART_POLL       (1)

static void nfa_brcm_pre_sys_enable (void);
static BOOLEAN nfa_brcm_pre_evt_hdlr (BT_HDR *p_msg);
static void nfa_brcm_pre_sys_disable (void);
static void nfa_brcm_pre_proc_nfcc_power_mode (UINT8 nfcc_power_mode);
static void nfa_brcm_post_sys_enable (void);
static BOOLEAN nfa_brcm_post_evt_hdlr (BT_HDR *p_msg);
static void nfa_brcm_post_sys_disable (void);
static void nfa_brcm_post_proc_nfcc_power_mode (UINT8 nfcc_power_mode);
/*****************************************************************************
** Constants and types
*****************************************************************************/
static const tNFA_SYS_REG nfa_brcm_pre_sys_reg =
{
    nfa_brcm_pre_sys_enable,
    nfa_brcm_pre_evt_hdlr,
    nfa_brcm_pre_sys_disable,
    nfa_brcm_pre_proc_nfcc_power_mode
};

static const tNFA_SYS_REG nfa_brcm_post_sys_reg =
{
    nfa_brcm_post_sys_enable,
    nfa_brcm_post_evt_hdlr,
    nfa_brcm_post_sys_disable,
    nfa_brcm_post_proc_nfcc_power_mode
};

static UINT8 nfa_i93_rw_cfg[NFA_I93_RW_CFG_LEN] =
{
    NCI_PARAM_ID_I93_DATARATE,
    NFA_I93_RW_CFG_PARAM_LEN,
    RW_I93_FLAG_DATA_RATE,     /* Bit0:Sub carrier, Bit1:Data rate, Bit4:Enable/Disable AFI */
    NFA_I93_AFI,               /* AFI if Bit 4 is set in the flag byte */
    NFA_I93_ENABLE_SMART_POLL  /* Bit0:Enable/Disable smart poll */
};

/*****************************************************************************
** Extern function prototypes
*****************************************************************************/

/*****************************************************************************
** Local function prototypes
*****************************************************************************/
#if (BT_TRACE_VERBOSE == TRUE)
static char *nfa_dm_brcm_evt_2_str (UINT16 event);
#endif

static BOOLEAN nfa_dm_brcm_evt_hdlr (BT_HDR *p_msg);
static BOOLEAN nfa_dm_brcm_act_multi_tech_rsp (BT_HDR *p_msg);
static BOOLEAN nfa_dm_brcm_act_get_build_info (BT_HDR *p_msg);

/*****************************************************************************
** Constants and types
*****************************************************************************/

/* action function list */
tNFA_DM_BRCM_ACTION nfa_dm_brcm_action[] =
{
    nfa_dm_brcm_act_multi_tech_rsp,   /* NFA_DM_BRCM_API_MULTI_TECH_RSP_EVT    */
    nfa_dm_brcm_act_get_build_info,   /* NFA_DM_BRCM_API_GET_BUILD_INFO_EVT    */
    NULL                              /* NFA_DM_BRCM_VS_1_EVT                  */
};

#if NFA_DYNAMIC_MEMORY == FALSE
tNFA_BRCM_CB  nfa_brcm_cb;
#endif

/*******************************************************************************
**
** Function         nfa_dm_brcm_vse_cback
**
** Description      Callback function for BRCM VS event
**
** Returns          None
**
*******************************************************************************/
static void nfa_dm_brcm_vse_cback (tNFC_VS_EVT event, UINT16 data_len, UINT8 *p_data)
{
    tNFA_DM_CBACK_DATA  dm_cback_data;

    NFA_TRACE_DEBUG1 ("nfa_dm_brcm_vse_cback () event=0x%x", event);

    switch (event)
    {
    case NFC_VS_HCI_NETWK_EVT:
        nfa_hci_handle_hci_netwk_info (p_data);
        break;

    case NFC_VS_LPTD_EVT:
        p_data += NCI_MSG_HDR_SIZE;
        dm_cback_data.status = *p_data;

        (*nfa_dm_cb.p_dm_cback) (NFA_DM_LPTD_EVT, &dm_cback_data);
        break;

    default:
        NFA_TRACE_DEBUG1 ("nfa_dm_brcm_vse_cback () unknown event (0x%x)", event);
        break;
    }
}

/*******************************************************************************
**
** Function         nfa_dm_brcm_set_fw_fsm
**
** Description      Enable or disable FW FSM
**
** Returns          void
**
*******************************************************************************/
void nfa_dm_brcm_set_fw_fsm (BOOLEAN enable)
{
    BT_HDR  *p_msg;
    UINT8   *p;

    p_msg = (BT_HDR *) GKI_getbuf ((UINT16) (BT_HDR_SIZE + NCI_VSC_MSG_HDR_SIZE + 1));

    if (p_msg)
    {
        p_msg->offset = NCI_VSC_MSG_HDR_SIZE;

        p = (UINT8*) (p_msg + 1) + p_msg->offset;

        if (enable)
            *p = 1; /* Enable, default is disabled */
        else
            *p = 0; /* Disable */

        p_msg->len = 1;

        NFC_SendVsCommand (NCI_MSG_SET_FWFSM, p_msg, NULL);
    }
}

/*******************************************************************************
**
** Function         nfa_dm_brcm_init
**
** Description      Initialize BRCM DM control block
**                  Initialize BRCM NFCC with VS configuration and CMD
**
** Returns          void
**
*******************************************************************************/
void nfa_dm_brcm_init (void)
{
    NFA_TRACE_DEBUG0 ("nfa_dm_brcm_init ()");

    nfa_dm_cb.p_vs_evt_hdlr = nfa_dm_brcm_evt_hdlr;

    memset (&nfa_brcm_cb, 0, sizeof (tNFA_BRCM_CB));
    nfa_brcm_cb.dm_enable_multi_resp = NFA_DM_MULTI_TECH_RESP;
}

/*******************************************************************************
**
** Function         nfa_dm_brcm_restore
**
** Description      Initialize BRCM DM control block
**                  Initialize BRCM NFCC with VS configuration and CMD
**
** Returns          void
**
*******************************************************************************/
void nfa_dm_brcm_restore (void)
{
    UINT8   *p, tlv_params[NFA_DM_LPTD_PARAM_LEN + NFA_DM_LPTD_PARAM_EXTRA_LEN];
    UINT8   len;

    NFA_TRACE_DEBUG0 ("nfa_dm_brcm_restore ()");
    if (p_nfa_dm_lptd_cfg[0])
    {
        p = tlv_params;
        *p++  = NCI_PARAM_ID_TAGSNIFF_CFG;
        *p++  = p_nfa_dm_lptd_cfg[0];
        len   = p_nfa_dm_lptd_cfg[0] + 2; /* 2 is for T and L */
        memcpy (p, &p_nfa_dm_lptd_cfg[1], p_nfa_dm_lptd_cfg[0]);
        nfa_dm_check_set_config (len, tlv_params, FALSE);
    }

    if ((p_nfa_dm_pll_325_cfg)&&(nfa_brcm_cb.xtal_index < BRCM_XTAL_INDEX_MAX))
    {
        p = p_nfa_dm_pll_325_cfg + (nfa_brcm_cb.xtal_index * NFA_DM_BRCM_PLL_325_SETCONFIG_PARAM_LEN);
        nfa_dm_check_set_config (NFA_DM_BRCM_PLL_325_SETCONFIG_PARAM_LEN, p, FALSE);
    }

    if (p_nfa_dm_start_up_cfg[0])
    {
        nfa_dm_check_set_config (p_nfa_dm_start_up_cfg[0], &p_nfa_dm_start_up_cfg[1], FALSE);
    }

#if (RW_I93_FLAG_DATA_RATE == I93_FLAG_DATA_RATE_HIGH)
    nfa_dm_check_set_config (NFA_I93_RW_CFG_LEN, nfa_i93_rw_cfg, FALSE);
#endif

    if (nfa_brcm_cb.dm_enable_multi_resp)
    {
        /* FW FSM is disabled as default */
        nfa_dm_brcm_set_fw_fsm (TRUE);
    }

    if (!(nfa_brcm_cb.dm_flags & NFA_DM_BRCM_FLAGS_VS_CBACK_REG))
    {
        nfa_brcm_cb.dm_flags |= NFA_DM_BRCM_FLAGS_VS_CBACK_REG;

        /* Register callback for VS event */
        NFC_RegVSCback (TRUE, nfa_dm_brcm_vse_cback);
    }
}

/*******************************************************************************
**
** Function         nfa_dm_brcm_act_multi_tech_rsp
**
** Description      Enable or disable FW FSM feature
**
** Returns          TRUE (message buffer to be freed by caller)
**
*******************************************************************************/
BOOLEAN nfa_dm_brcm_act_multi_tech_rsp (BT_HDR *p_msg)
{
    BOOLEAN enable = (BOOLEAN) p_msg->layer_specific;

    nfa_brcm_cb.dm_enable_multi_resp = enable;

    nfa_dm_brcm_set_fw_fsm (enable);

    return (TRUE);
}

/*******************************************************************************
**
** Function         nfa_dm_brcm_act_get_build_info
**
** Description      Get firmware build info
**
** Returns          TRUE (message buffer to be freed by caller)
**
*******************************************************************************/
BOOLEAN nfa_dm_brcm_act_get_build_info (BT_HDR *p_msg)
{
    NFC_BrcmGetFirmwareBuildInfo ();
    return (TRUE);
}


/*******************************************************************************
**
** Function         nfa_dm_brcm_evt_hdlr
**
** Description      BRCM specific event handling function for DM
**
**
** Returns          void
**
*******************************************************************************/
static BOOLEAN nfa_dm_brcm_evt_hdlr (BT_HDR *p_msg)
{
    tNFA_DM_CBACK_DATA  dm_cback_data;
    BOOLEAN             freebuf = TRUE;
    UINT16              event   = p_msg->event;
    tNFA_DM_MSG         *p_dm_msg;

    /* if this is callback data from NFC layer */
    if (event == NFA_DM_NFC_CBACK_DATA_EVT)
    {
        p_dm_msg = (tNFA_DM_MSG *) p_msg;
        if (p_dm_msg->nfc_cback_data.event == NFC_FIRMWARE_BUILD_INFO_REVT)
        {
            /* Response from calling NFA_BrcmGetFirmwareBuildInfo */
            if (nfa_dm_cb.p_dm_cback)
            {
                dm_cback_data.p_vs_evt_data = p_dm_msg->nfc_cback_data.p_data->p_vs_evt_data;
                (*nfa_dm_cb.p_dm_cback) (NFA_DM_FIRMWARE_BUILD_INFO_EVT, &dm_cback_data);
            }
        }
    }
    /* execute action functions */
    else if ((event >= NFA_DM_MAX_EVT) && (event < NFA_DM_BRCM_MAX_EVT))
    {
#if (BT_TRACE_VERBOSE == TRUE)
        NFA_TRACE_EVENT2 ("nfa_dm_brcm_evt_hdlr event: %s (0x%x)", nfa_dm_brcm_evt_2_str (event), event);
#else
        NFA_TRACE_EVENT1 ("nfa_dm_brcm_evt_hdlr event: 0x%x", event);
#endif

        if (nfa_dm_brcm_action[event - NFA_DM_MAX_EVT])
        {
            freebuf = (*nfa_dm_brcm_action[event - NFA_DM_MAX_EVT]) (p_msg);
        }
    }

    return freebuf;
}

#if (BT_TRACE_VERBOSE == TRUE)
/*******************************************************************************
**
** Function         nfa_dm_brcm_evt_2_str
**
** Description      convert nfc revt to string
**
*******************************************************************************/
static char *nfa_dm_brcm_evt_2_str (UINT16 event)
{
    switch (event)
    {
    case NFA_DM_BRCM_API_MULTI_TECH_RSP_EVT:
        return "NFA_DM_BRCM_API_MULTI_TECH_RSP_EVT";

    case NFA_DM_BRCM_VS_1_EVT:
        return "NFA_DM_BRCM_VS_1_EVT";
    }

    return "Unknown";
}
#endif /* BT_TRACE_VERBOSE */

/*******************************************************************************
**
** Function         nfa_brcm_pre_sys_enable
**
** Description      Enable BRCM specific Pre NFA
**
** Returns          None
**
*******************************************************************************/
static void nfa_brcm_pre_sys_enable (void)
{
    UINT8   *p, len, *p_end, oid, *p_vsc;
    BT_HDR  *p_buf;

    NFA_TRACE_DEBUG0 ("nfa_brcm_pre_sys_enable ()");

    nfa_dm_brcm_restore ();
    nfa_hci_brcm_enable ();

    /* send VSCs, if configured */
    if (p_nfa_dm_start_up_vsc_cfg && *p_nfa_dm_start_up_vsc_cfg)
    {
        len     = *p_nfa_dm_start_up_vsc_cfg++;
        p       = p_nfa_dm_start_up_vsc_cfg;
        p_end   = p + len;
        while (p_end > p)
        {
            oid = *p++;
            len = *p++;
            p_buf   = (BT_HDR *)GKI_getpoolbuf (NFC_NCI_POOL_ID);
            if (p_buf)
            {
                p_buf->offset   = NCI_VSC_MSG_HDR_SIZE;
                p_buf->len      = len;
                p_vsc           = (UINT8 *)(p_buf + 1) + p_buf->offset;
                memcpy (p_vsc, p, len);
                NFC_SendVsCommand(oid, p_buf, NULL);
            }
            p  += len;
        }

    }
    nfa_sys_cback_notify_enable_complete (NFA_ID_VS_PRE);
}
/*******************************************************************************
**
** Function         nfa_brcm_pre_evt_hdlr
**
** Description      Processing BRCM specific Pre NFA
**
**
** Returns          TRUE if p_msg needs to be deallocated
**
*******************************************************************************/
static BOOLEAN nfa_brcm_pre_evt_hdlr (BT_HDR *p_msg)
{
    return TRUE;
}
/*******************************************************************************
**
** Function         nfa_brcm_pre_sys_disable
**
** Description      Disable BRCM specific before NFA
**
**
** Returns          None
**
*******************************************************************************/
static void nfa_brcm_pre_sys_disable (void)
{
    NFA_TRACE_DEBUG0 ("nfa_brcm_pre_sys_disable ()");
    nfa_sys_deregister (NFA_ID_VS_PRE);
}
/*******************************************************************************
**
** Function         nfa_brcm_pre_proc_nfcc_power_mode
**
** Description      Process change of power mode for BRCM specific before NFA
**
** Returns          None
**
*******************************************************************************/
static void nfa_brcm_pre_proc_nfcc_power_mode (UINT8 nfcc_power_mode)
{
    NFA_TRACE_DEBUG0 ("nfa_brcm_pre_proc_nfcc_power_mode ()");

    /* if NFCC power mode is change to full power */
    if (nfcc_power_mode == NFA_DM_PWR_MODE_FULL)
    {
        nfa_dm_brcm_restore ();
    }
    nfa_hci_brcm_proc_nfcc_power_mode (nfcc_power_mode);

    nfa_sys_cback_notify_nfcc_power_mode_proc_complete (NFA_ID_VS_PRE);
}

/*******************************************************************************
**
** Function         nfa_brcm_post_sys_enable
**
** Description      Enable BRCM specific after NFA
**
** Returns          None
**
*******************************************************************************/
static void nfa_brcm_post_sys_enable (void)
{
    NFA_TRACE_DEBUG0 ("nfa_brcm_post_sys_enable ()");
    nfa_ee_brcm_enable ();
    nfa_sys_cback_notify_enable_complete (NFA_ID_VS_POST);
}
/*******************************************************************************
**
** Function         nfa_brcm_post_evt_hdlr
**
** Description      Processing BRCM specific Post NFA
**
**
** Returns          TRUE if p_msg needs to be deallocated
**
*******************************************************************************/
static BOOLEAN nfa_brcm_post_evt_hdlr (BT_HDR *p_msg)
{
    return TRUE;
}
/*******************************************************************************
**
** Function         nfa_brcm_post_sys_disable
**
** Description      Disable BRCM specific Post NFA
**
**
** Returns          None
**
*******************************************************************************/
static void nfa_brcm_post_sys_disable (void)
{
    NFA_TRACE_DEBUG0 ("nfa_brcm_post_sys_disable ()");
    nfa_ee_brcm_disable ();
    nfa_sys_deregister (NFA_ID_VS_POST);
}

/*******************************************************************************
**
** Function         nfa_brcm_post_proc_nfcc_power_mode
**
** Description      Process change of power mode for BRCM specific after NFA
**
** Returns          None
**
*******************************************************************************/
static void nfa_brcm_post_proc_nfcc_power_mode (UINT8 nfcc_power_state)
{
    NFA_TRACE_DEBUG0 ("nfa_brcm_post_proc_nfcc_power_mode ()");
    nfa_sys_cback_notify_nfcc_power_mode_proc_complete (NFA_ID_VS_POST);
}



/*******************************************************************************
** APIs
*******************************************************************************/
/*******************************************************************************
**
** Function         NFA_BrcmInit
**
** Description      This function initializes Broadcom specific control blocks for NFA
**                  and registers a callback to be called after the NFCC is reset,
**                  to perform any platform-specific initialization (e.g. patch download).
**
**                  tBRCM_DEV_INIT_CONFIG
**                      BRCM_DEV_INIT_FLAGS_AUTO_BAUD     : UART auto baud detection
**                      BRCM_DEV_INIT_FLAGS_SET_XTAL_FREQ : set crystal frequency
**                      xtal_freq : crystal frequency in KHz
**
** Returns          none
**
*******************************************************************************/
void NFA_BrcmInit (tBRCM_DEV_INIT_CONFIG *p_dev_init_config,
                   tBRCM_DEV_INIT_CBACK  *p_dev_init_cback)
{
    NFA_TRACE_API0 ("NFA_BrcmInit ()");

    nfa_dm_brcm_init ();
    nfa_dm_vs_brcm_init ();
    nfa_ee_brcm_init ();
    nfa_hci_brcm_init ();
    NFC_BrcmInit (p_dev_init_config, p_dev_init_cback);

    /* register message handler on NFA SYS */
    nfa_sys_register ( NFA_ID_VS_PRE,  &nfa_brcm_pre_sys_reg);
    nfa_sys_register ( NFA_ID_VS_POST, &nfa_brcm_post_sys_reg);

    if (p_dev_init_config)
    {
        switch (p_dev_init_config->xtal_freq)
        {
        case  9600: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_9600;  break;
        case 13000: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_13000; break;
        case 16200: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_16200; break;
        case 19200: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_19200; break;
        case 24000: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_24000; break;
        case 26000: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_26000; break;
        case 38400: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_38400; break;
        case 52000: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_52000; break;
        case 37400: nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_37400; break;
        default :   nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_MAX;
                    NFA_TRACE_DEBUG1 ("No matchec xtal index for %d", p_dev_init_config->xtal_freq);
                    break;
        }
    }
    else
    {
        nfa_brcm_cb.xtal_index = BRCM_XTAL_INDEX_MAX;
    }
}



/*******************************************************************************
**
** Function         NFA_BrcmGetFirmwareBuildInfo
**
** Description      Get firmware build info from the NFCC.
**                  NFA_DM_FIRMWARE_BUILD_INFO_EVT will indicate the result.
**
** Returns          NFA_STATUS_OK if successfully started
**                  NFA_STATUS_FAILED otherwise
**
*******************************************************************************/
tNFA_STATUS NFA_BrcmGetFirmwareBuildInfo (void)
{
    BT_HDR *p_msg;

    NFA_TRACE_API0 ("NFA_BrcmGetFirmwareBuildInfo ()");

    if ((p_msg = (BT_HDR *) GKI_getbuf (sizeof (BT_HDR))) != NULL)
    {
        p_msg->event = NFA_DM_BRCM_API_GET_BUILD_INFO_EVT;

        nfa_sys_sendmsg (p_msg);

        return (NFA_STATUS_OK);
    }

    return (NFA_STATUS_FAILED);
}

/*******************************************************************************
**
** Function         NFA_SetMultiTechRsp
**
** Description      Enable or disable NFCC responding more than one technology
**                  during listen discovry.
**
** Returns          NFA_STATUS_OK if successfully initiated
**                  NFA_STATUS_FAILED otherwise
**
*******************************************************************************/
tNFA_STATUS NFA_SetMultiTechRsp (BOOLEAN enable)
{
    BT_HDR *p_msg;

    NFA_TRACE_API1 ("NFA_SetMultiTechRsp () enable=%d", enable);

    if ((p_msg = (BT_HDR *) GKI_getbuf (sizeof(BT_HDR))) != NULL)
    {
        p_msg->event          = NFA_DM_BRCM_API_MULTI_TECH_RSP_EVT;
        p_msg->layer_specific = enable;

        nfa_sys_sendmsg (p_msg);

        return (NFA_STATUS_OK);
    }

    return (NFA_STATUS_FAILED);
}

#endif