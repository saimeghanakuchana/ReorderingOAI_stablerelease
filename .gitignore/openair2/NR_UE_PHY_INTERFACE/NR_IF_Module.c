/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/* \file NR_IF_Module.c
 * \brief functions for NR UE FAPI-like interface
 * \author R. Knopp, K.H. HSU
 * \date 2018
 * \version 0.1
 * \company Eurecom / NTUST
 * \email: knopp@eurecom.fr, kai-hsiang.hsu@eurecom.fr
 * \note
 * \warning
 */

#include "PHY/defs_nr_UE.h"
#include "NR_IF_Module.h"
#include "NR_MAC_UE/mac_proto.h"
#include "assertions.h"
#include "SCHED_NR_UE/fapi_nr_ue_l1.h"
#include "openair2/RRC/NR_UE/L2_interface_ue.h"

#define MAX_IF_MODULES 100

static nr_ue_if_module_t *nr_ue_if_module_inst[MAX_IF_MODULES];

void print_ue_mac_stats(const module_id_t mod, const int frame_rx, const int slot_rx)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(mod);
  if (mac->state != UE_CONNECTED)
    return;
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);

  char txt[1024];
  char *end = txt + sizeof(txt);
  char *cur = txt;

  float nbul = 0;
  for (uint64_t *p = mac->stats.ul.rounds; p < mac->stats.ul.rounds + NR_MAX_HARQ_ROUNDS_FOR_STATS; p++)
    nbul += *p;
  if (nbul < 1)
    nbul = 1;

  float nbdl = 0;
  for (uint64_t *p = mac->stats.dl.rounds; p < mac->stats.dl.rounds + NR_MAX_HARQ_ROUNDS_FOR_STATS; p++)
    nbdl += *p;
  if (nbdl < 1)
    nbdl = 1;

  cur += snprintf(cur,
                  end - cur,
                  "UE %d RNTI %04x stats sfn: %d.%d, cumulated bad DCI %d\n",
                  mod,
                  mac->crnti,
                  frame_rx,
                  slot_rx,
                  mac->stats.bad_dci);

  cur += snprintf(cur, end - cur, "    DL harq: %lu", mac->stats.dl.rounds[0]);
  int nb;
  for (nb = NR_MAX_HARQ_ROUNDS_FOR_STATS - 1; nb > 1; nb--)
    if (mac->stats.ul.rounds[nb])
      break;
  for (int i = 1; i < nb + 1; i++)
    cur += snprintf(cur, end - cur, "/%lu", mac->stats.dl.rounds[i]);

  cur += snprintf(cur, end - cur, "\n    Ul harq: %lu", mac->stats.ul.rounds[0]);
  for (nb = NR_MAX_HARQ_ROUNDS_FOR_STATS - 1; nb > 1; nb--)
    if (mac->stats.ul.rounds[nb])
      break;
  for (int i = 1; i < nb + 1; i++)
    cur += snprintf(cur, end - cur, "/%lu", mac->stats.ul.rounds[i]);
  snprintf(cur,
           end - cur,
           " avg code rate %.01f, avg bit/symbol %.01f, avg per TB: "
           "(nb RBs %.01f, nb symbols %.01f)\n",
           (double)mac->stats.ul.target_code_rate / (mac->stats.ul.total_bits * 1024 * 10), // See const Table_51311 definition
           (double)mac->stats.ul.total_bits / mac->stats.ul.total_symbols,
           mac->stats.ul.rb_size / nbul,
           mac->stats.ul.nr_of_symbols / nbul);
  LOG_I(NR_MAC, "%s", txt);
  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
}

//  L2 Abstraction Layer
static int handle_bcch_bch(NR_UE_MAC_INST_t *mac,
                           int cc_id,
                           unsigned int gNB_index,
                           void *phy_data,
                           uint8_t *pduP,
                           unsigned int additional_bits,
                           uint32_t ssb_index,
                           uint32_t ssb_length,
                           uint16_t ssb_start_subcarrier,
                           long ssb_arfcn,
                           uint16_t cell_id)
{
  mac->mib_ssb = ssb_index;
  mac->physCellId = cell_id;
  mac->mib_additional_bits = additional_bits;
  mac->ssb_start_subcarrier = ssb_start_subcarrier;
  if(ssb_length == 64)
    mac->frequency_range = FR2;
  else
    mac->frequency_range = FR1;
  //  fixed 3 bytes MIB PDU
  nr_mac_rrc_data_ind_ue(mac->ue_id, cc_id, gNB_index, 0, 0, 0, cell_id, ssb_arfcn, NR_BCCH_BCH, (uint8_t *) pduP, 3);
  return 0;
}

//  L2 Abstraction Layer
static int handle_bcch_dlsch(NR_UE_MAC_INST_t *mac,
                             int cc_id,
                             unsigned int gNB_index,
                             uint8_t ack_nack,
                             uint8_t *pduP,
                             uint32_t pdu_len,
                             int frame,
                             int slot)
{
  nr_ue_decode_BCCH_DL_SCH(mac, cc_id, gNB_index, ack_nack, pduP, pdu_len, frame, slot);
  return 0;
}

//  L2 Abstraction Layer
static nr_dci_format_t handle_dci(NR_UE_MAC_INST_t *mac,
                                  unsigned int gNB_index,
                                  frame_t frame,
                                  int slot,
                                  fapi_nr_dci_indication_pdu_t *dci)
{
  // if notification of a reception of a PDCCH transmission of the SpCell is received from lower layers
  // if the C-RNTI MAC CE was included in Msg3
  // consider this Contention Resolution successful
  if (mac->msg3_C_RNTI && mac->ra.ra_state == nrRA_WAIT_CONTENTION_RESOLUTION)
    nr_ra_succeeded(mac, gNB_index, frame, slot);

  // suspend RAR response window timer
  // (in RFsim running multiple slot in parallel it might expire while decoding MSG2)
  if (mac->ra.ra_state == nrRA_WAIT_RAR)
    nr_timer_suspension(&mac->ra.response_window_timer);

  return nr_ue_process_dci_indication_pdu(mac, frame, slot, dci);
}

// L2 Abstraction Layer
// Note: sdu should always be processed because data and timing advance updates are transmitted by the UE
static int8_t handle_dlsch(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id)
{
  if (mac->ra.ra_state != nrRA_WAIT_RAR) // no HARQ for MSG2
    update_harq_status(mac,
                       dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu.harq_pid,
                       dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu.ack_nack);
  if(dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu.ack_nack)
    nr_ue_send_sdu(mac, dl_info, pdu_id);

  return 0;
}

static void handle_rlm(rlm_t rlm_result, int frame, NR_UE_MAC_INST_t *mac)
{
  if (rlm_result == RLM_no_monitoring)
    return;
  bool is_sync = rlm_result == RLM_in_sync ? true : false;
  nr_mac_rrc_sync_ind(mac->ue_id, frame, is_sync);
}

static int8_t handle_l1_measurements(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, fapi_nr_l1_measurements_t *l1_measurements)
{
  handle_rlm(l1_measurements->radiolink_monitoring, frame, mac);
  nr_ue_process_l1_measurements(mac, frame, slot, l1_measurements);
  return 0;
}

void update_harq_status(NR_UE_MAC_INST_t *mac, uint8_t harq_pid, uint8_t ack_nack)
{
  NR_UE_DL_HARQ_STATUS_t *current_harq = &mac->dl_harq_info[harq_pid];

  if (current_harq->active) {
    LOG_D(PHY,"Updating harq_status for harq_id %d, ack/nak %d\n", harq_pid, current_harq->ack);
    // we can prepare feedback for MSG4 in advance
    if (mac->ra.ra_state == nrRA_WAIT_CONTENTION_RESOLUTION || mac->ra.ra_state == nrRA_WAIT_MSGB)
      prepare_msg4_msgb_feedback(mac, harq_pid, ack_nack);
    else {
      current_harq->ack = ack_nack;
      current_harq->ack_received = true;
    }
  }
  else if (!get_FeedbackDisabled(mac->sc_info.downlinkHARQ_FeedbackDisabled_r17, harq_pid)) {
    //shouldn't get here
    LOG_E(NR_MAC, "Trying to process acknack for an inactive harq process (%d)\n", harq_pid);
  }
}

int nr_ue_ul_indication(nr_uplink_indication_t *ul_info)
{
  module_id_t module_id = ul_info->module_id;
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  LOG_D(PHY, "Locked in ul, slot %d\n", ul_info->slot);

  LOG_T(NR_MAC, "Not calling scheduler mac->ra.ra_state = %d\n", mac->ra.ra_state);

  if (is_ul_slot(ul_info->slot, &mac->frame_structure))
    nr_ue_ul_scheduler(mac, ul_info);
  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  return 0;
}

static uint32_t nr_ue_dl_processing(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info)
{
  uint32_t ret_mask = 0x0;
  DevAssert(mac != NULL && dl_info != NULL);

  // DL indication after reception of DCI or DL PDU
  if (dl_info->dci_ind && dl_info->dci_ind->number_of_dcis) {
    LOG_T(MAC, "[L2][IF MODULE][DL INDICATION][DCI_IND]\n");
    for (int i = 0; i < dl_info->dci_ind->number_of_dcis; i++) {
      LOG_T(MAC, ">>>NR_IF_Module i=%d, dl_info->dci_ind->number_of_dcis=%d\n", i, dl_info->dci_ind->number_of_dcis);
      nr_dci_format_t dci_format = handle_dci(mac,
                                              dl_info->gNB_index,
                                              dl_info->frame,
                                              dl_info->slot,
                                              dl_info->dci_ind->dci_list + i);

      /* The check below filters out UL_DCIs which are being processed as DL_DCIs. */
      if (dci_format != NR_DL_DCI_FORMAT_1_0 && dci_format != NR_DL_DCI_FORMAT_1_1) {
        LOG_D(NR_MAC, "We are filtering a UL_DCI to prevent it from being treated like a DL_DCI\n");
        continue;
      }
      dci_pdu_rel15_t *def_dci_pdu_rel15 = &mac->def_dci_pdu_rel15[dl_info->slot][dci_format];
      ret_mask |= (1 << FAPI_NR_DCI_IND);
      AssertFatal(nr_ue_if_module_inst[dl_info->module_id] != NULL, "IF module is NULL!\n");
      fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, dl_info->slot);
      nr_scheduled_response_t scheduled_response = {.dl_config = dl_config,
                                                    .mac = mac,
                                                    .module_id = dl_info->module_id,
                                                    .CC_id = dl_info->cc_id,
                                                    .phy_data = dl_info->phy_data};
      nr_ue_if_module_inst[dl_info->module_id]->scheduled_response(&scheduled_response);
      memset(def_dci_pdu_rel15, 0, sizeof(*def_dci_pdu_rel15));
    }
    dl_info->dci_ind = NULL;
  }

  if (dl_info->rx_ind != NULL) {

    for (int i = 0; i < dl_info->rx_ind->number_pdus; ++i) {

      fapi_nr_rx_indication_body_t rx_indication_body = dl_info->rx_ind->rx_indication_body[i];
      LOG_D(NR_MAC,
            "slot %d Sending DL indication to MAC. 1 PDU type %d of %d total number of PDUs \n",
            dl_info->slot,
            rx_indication_body.pdu_type,
            dl_info->rx_ind->number_pdus);

      switch(rx_indication_body.pdu_type) {
        case FAPI_NR_RX_PDU_TYPE_SSB:
          handle_rlm(rx_indication_body.ssb_pdu.radiolink_monitoring,
                     dl_info->frame,
                     mac);
          if(rx_indication_body.ssb_pdu.decoded_pdu) {
            ret_mask |= (handle_bcch_bch(mac,
                                         dl_info->cc_id,
                                         dl_info->gNB_index,
                                         dl_info->phy_data,
                                         rx_indication_body.ssb_pdu.pdu,
                                         rx_indication_body.ssb_pdu.additional_bits,
                                         rx_indication_body.ssb_pdu.ssb_index,
                                         rx_indication_body.ssb_pdu.ssb_length,
                                         rx_indication_body.ssb_pdu.ssb_start_subcarrier,
                                         rx_indication_body.ssb_pdu.arfcn,
                                         rx_indication_body.ssb_pdu.cell_id)) << FAPI_NR_RX_PDU_TYPE_SSB;
          }
          break;
        case FAPI_NR_RX_PDU_TYPE_SIB:
          ret_mask |= (handle_bcch_dlsch(mac,
                                         dl_info->cc_id,
                                         dl_info->gNB_index,
                                         rx_indication_body.pdsch_pdu.ack_nack,
                                         rx_indication_body.pdsch_pdu.pdu,
                                         rx_indication_body.pdsch_pdu.pdu_length,
                                         dl_info->frame,
                                         dl_info->slot)) << FAPI_NR_RX_PDU_TYPE_SIB;
          break;
        case FAPI_NR_RX_PDU_TYPE_DLSCH:
          ret_mask |= (handle_dlsch(mac, dl_info, i)) << FAPI_NR_RX_PDU_TYPE_DLSCH;
          break;
        case FAPI_NR_RX_PDU_TYPE_RAR:
          if (!dl_info->rx_ind->rx_indication_body[i].pdsch_pdu.ack_nack) {
            LOG_W(PHY, "Received a RAR-Msg2 but LDPC decode failed\n");
            // resume RAR response window timer if MSG2 decoding failed
            nr_timer_suspension(&mac->ra.response_window_timer);
          } else {
            LOG_I(PHY, "[UE %d] RAR-Msg2 decoded\n", mac->ue_id);
          }
          ret_mask |= (handle_dlsch(mac, dl_info, i)) << FAPI_NR_RX_PDU_TYPE_RAR;
          break;
        case FAPI_NR_MEAS_IND:
          ret_mask |= (handle_l1_measurements(mac,
                                              dl_info->frame,
                                              dl_info->slot,
                                              &rx_indication_body.l1_measurements)) << FAPI_NR_MEAS_IND;
          break;
        default:
          break;
      }
    }
    dl_info->rx_ind = NULL;
  }
  return ret_mask;
}

int nr_ue_dl_indication(nr_downlink_indication_t *dl_info)
{
  uint32_t ret2 = 0;
  NR_UE_MAC_INST_t *mac = get_mac_inst(dl_info->module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  if (!dl_info->dci_ind && !dl_info->rx_ind)
    // DL indication to process DCI reception
    nr_ue_dl_scheduler(mac, dl_info);
  else
    // DL indication to process data channels
    ret2 = nr_ue_dl_processing(mac, dl_info);
  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  return ret2;
}

void nr_ue_slot_indication(uint8_t mod_id, bool is_tx)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(mod_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  if (is_tx)
    update_mac_ul_timers(mac);
  else
    update_mac_dl_timers(mac);
  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
}

nr_ue_if_module_t *nr_ue_if_module_init(uint32_t module_id)
{
  if (nr_ue_if_module_inst[module_id] == NULL) {
    nr_ue_if_module_inst[module_id] = (nr_ue_if_module_t *)malloc(sizeof(nr_ue_if_module_t));
    memset((void*)nr_ue_if_module_inst[module_id],0,sizeof(nr_ue_if_module_t));

    nr_ue_if_module_inst[module_id]->cc_mask=0;
    nr_ue_if_module_inst[module_id]->current_frame = 0;
    nr_ue_if_module_inst[module_id]->current_slot = 0;
    nr_ue_if_module_inst[module_id]->phy_config_request = nr_ue_phy_config_request;
    nr_ue_if_module_inst[module_id]->synch_request = nr_ue_synch_request;
    if (get_softmodem_params()->sl_mode) {
      nr_ue_if_module_inst[module_id]->sl_phy_config_request = nr_ue_sl_phy_config_request;
      nr_ue_if_module_inst[module_id]->sl_indication = nr_ue_sl_indication;
    }
    nr_ue_if_module_inst[module_id]->scheduled_response = nr_ue_scheduled_response;
    nr_ue_if_module_inst[module_id]->dl_indication = nr_ue_dl_indication;
    nr_ue_if_module_inst[module_id]->ul_indication = nr_ue_ul_indication;
    nr_ue_if_module_inst[module_id]->slot_indication = nr_ue_slot_indication;
  }
  return nr_ue_if_module_inst[module_id];
}

static void handle_sl_bch(int ue_id,
                          sl_nr_ue_mac_params_t *sl_mac,
                          uint8_t *const sl_mib,
                          const uint8_t len,
                          uint16_t frame_rx,
                          uint16_t slot_rx,
                          uint16_t rx_slss_id)
{
  LOG_D(NR_MAC, " decode SL-MIB %d\n", rx_slss_id);

  uint8_t sl_tdd_config[2] = {0, 0};

  sl_tdd_config[0] = sl_mib[0];
  sl_tdd_config[1] = sl_mib[1] & 0xF0;
  uint8_t incov = sl_mib[1] & 0x08;
  uint16_t frame_0 = (sl_mib[2] & 0xFE) >> 1;
  uint16_t frame_1 = sl_mib[1] & 0x07;
  frame_0 |= (frame_1 & 0x01) << 7;
  frame_1 = ((frame_1 & 0x06) >> 1) << 8;
  uint16_t frame = frame_1 | frame_0;
  uint8_t slot = ((sl_mib[2] & 0x01) << 6) | ((sl_mib[3] & 0xFC) >> 2);

  LOG_D(NR_MAC,
        "[UE%d]In %d:%d Received SL-MIB:%x .Contents- SL-TDD config:%x, Incov:%d, FN:%d, Slot:%d\n",
        ue_id,
        frame_rx,
        slot_rx,
        *((uint32_t *)sl_mib),
        *((uint16_t *)sl_tdd_config),
        incov,
        frame,
        slot);

  sl_mac->decoded_DFN = frame;
  sl_mac->decoded_slot = slot;

  nr_mac_rrc_data_ind_ue(ue_id, 0, 0, frame_rx, slot_rx, 0, rx_slss_id, 0, NR_SBCCH_SL_BCH, (uint8_t *)sl_mib, len);

  return;
}
/*
if PSBCH rx - handle_psbch()
  - Extract FN, Slot
  - Extract TDD configuration from the 12 bits
  - SEND THE SL-MIB to RRC
if PSSCH DATa rx - handle slsch()
*/
void sl_nr_process_rx_ind(int ue_id,
                          uint32_t frame,
                          uint32_t slot,
                          sl_nr_ue_mac_params_t *sl_mac,
                          sl_nr_rx_indication_t *rx_ind)
{
  uint8_t num_pdus = rx_ind->number_pdus;
  uint8_t pdu_type = rx_ind->rx_indication_body[num_pdus - 1].pdu_type;

  switch (pdu_type) {
    case SL_NR_RX_PDU_TYPE_SSB:

      if (rx_ind->rx_indication_body[num_pdus - 1].ssb_pdu.decode_status) {
        LOG_D(NR_MAC,
              "[UE%d]SL-MAC Received SL-SSB: RSRP:%d dBm/RE, rx_psbch_payload:%x, rx_slss_id:%d\n",
              ue_id,
              rx_ind->rx_indication_body[num_pdus - 1].ssb_pdu.rsrp_dbm,
              *((uint32_t *)rx_ind->rx_indication_body[num_pdus - 1].ssb_pdu.psbch_payload),
              rx_ind->rx_indication_body[num_pdus - 1].ssb_pdu.rx_slss_id);

        handle_sl_bch(ue_id,
                      sl_mac,
                      rx_ind->rx_indication_body[num_pdus - 1].ssb_pdu.psbch_payload,
                      4,
                      frame,
                      slot,
                      rx_ind->rx_indication_body[num_pdus - 1].ssb_pdu.rx_slss_id);
        sl_mac->ssb_rsrp_dBm = rx_ind->rx_indication_body[num_pdus - 1].ssb_pdu.rsrp_dbm;
      } else {
        LOG_I(NR_MAC, "[UE%d]SL-MAC - NO SL-SSB Received\n", ue_id);
      }

      break;
    case SL_NR_RX_PDU_TYPE_SLSCH:
      break;

    default:
      AssertFatal(1 == 0, "Incorrect type received. %s\n", __FUNCTION__);
      break;
  }
}

/*
 * Sidelink indication is sent from PHY->MAC.
 * This interface function handles these
 *  - rx_ind (SSB on PSBCH/SLSCH on PSSCH).
 *  - sci_ind (received scis during rxpool reception/txpool sensing)
 */
void nr_ue_sl_indication(nr_sidelink_indication_t *sl_indication)
{
  // NR_UE_L2_STATE_t ret;
  int ue_id = sl_indication->module_id;
  NR_UE_MAC_INST_t *mac = get_mac_inst(ue_id);

  uint16_t slot = sl_indication->slot_rx;
  uint16_t frame = sl_indication->frame_rx;

  sl_nr_ue_mac_params_t *sl_mac = mac->SL_MAC_PARAMS;

  if (sl_indication->rx_ind) {
    sl_nr_process_rx_ind(ue_id, frame, slot, sl_mac, sl_indication->rx_ind);
  } else {
    nr_ue_sidelink_scheduler(sl_indication, mac);
  }

}
