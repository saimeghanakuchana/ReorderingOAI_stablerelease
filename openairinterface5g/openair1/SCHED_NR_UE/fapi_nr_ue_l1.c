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

/* \file fapi_nr_ue_l1.c
 * \brief functions for NR UE FAPI-like interface
 * \author R. Knopp, K.H. HSU
 * \date 2018
 * \version 0.1
 * \company Eurecom / NTUST
 * \email: knopp@eurecom.fr, kai-hsiang.hsu@eurecom.fr
 * \note
 * \warning
 */

#include <stdio.h>
#include "fapi_nr_ue_interface.h"
#include "fapi_nr_ue_l1.h"
#include "harq_nr.h"
#include "openair2/NR_UE_PHY_INTERFACE/NR_IF_Module.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/impl_defs_nr.h"
#include "utils.h"
#include "SCHED_NR_UE/phy_sch_processing_time.h"
#include "openair1/PHY/phy_extern_nr_ue.h"

const char *const dl_pdu_type[] = {"DCI", "DLSCH", "RA_DLSCH", "SI_DLSCH", "P_DLSCH", "CSI_RS", "CSI_IM", "TA"};
const char *const ul_pdu_type[] = {"PRACH", "PUCCH", "PUSCH", "SRS"};

static void configure_dlsch(NR_UE_DLSCH_t *dlsch0,
                            NR_DL_UE_HARQ_t *harq_list,
                            fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu,
                            NR_UE_MAC_INST_t *mac,
                            int rnti)
{
  const uint8_t current_harq_pid = dlsch_config_pdu->harq_process_nbr;
  dlsch0->active = true;
  dlsch0->rnti = rnti;

  LOG_D(PHY,"current_harq_pid = %d\n", current_harq_pid);

  NR_DL_UE_HARQ_t *dlsch0_harq = &harq_list[current_harq_pid];

  //get nrOfLayers from DCI info
  uint8_t Nl = 0;
  for (int i = 0; i < 12; i++) { // max 12 ports
    if ((dlsch_config_pdu->dmrs_ports >> i) & 0x01)
      Nl += 1;
  }
  if (Nl == 0) {
    LOG_E(PHY, "Invalid number of layers %d for DLSCH\n", Nl);
    // setting NACK for this TB
    dlsch0->active = false;
    update_harq_status(mac, current_harq_pid, 0);
    return;
  }
  dlsch0->Nl = Nl;
  if (dlsch_config_pdu->new_data_indicator) {
    dlsch0_harq->first_rx = true;
    dlsch0_harq->DLround = 0;
  } else {
    dlsch0_harq->first_rx = false;
    dlsch0_harq->DLround++;
  }
  downlink_harq_process(dlsch0_harq, current_harq_pid, dlsch_config_pdu->new_data_indicator, dlsch_config_pdu->rv, dlsch0->rnti_type);
  if (dlsch0_harq->status != ACTIVE) {
    // dlsch0_harq->status not ACTIVE due to false retransmission
    // Reset the following flag to skip PDSCH procedures in that case and retrasmit harq status
    dlsch0->active = false;
    LOG_W(NR_MAC, "dlsch0_harq->status not ACTIVE due to false retransmission harq pid: %d\n", current_harq_pid);
    update_harq_status(mac, current_harq_pid, dlsch0_harq->decodeResult);
  }
}

static void configure_ntn_params(PHY_VARS_NR_UE *ue, fapi_nr_dl_ntn_config_command_pdu* ntn_params_message)
{
  if (!ue->ntn_config_message) {
    ue->ntn_config_message = CALLOC(1, sizeof(*ue->ntn_config_message));
  }

  ue->ntn_config_message->ntn_config_params.epoch_sfn = ntn_params_message->epoch_sfn;
  ue->ntn_config_message->ntn_config_params.epoch_subframe = ntn_params_message->epoch_subframe;
  ue->ntn_config_message->ntn_config_params.cell_specific_k_offset = ntn_params_message->cell_specific_k_offset;
  ue->ntn_config_message->ntn_config_params.ntn_total_time_advance_ms = ntn_params_message->ntn_total_time_advance_ms;
  ue->ntn_config_message->ntn_config_params.ntn_total_time_advance_drift = ntn_params_message->ntn_total_time_advance_drift;
  ue->ntn_config_message->ntn_config_params.ntn_total_time_advance_drift_variant = ntn_params_message->ntn_total_time_advance_drift_variant;
  ue->ntn_config_message->update = true;
}

static void configure_ta_command(PHY_VARS_NR_UE *ue, fapi_nr_ta_command_pdu *ta_command_pdu)
{
  /* Time Alignment procedure
  // - UE processing capability 1
  // - Setting the TA update to be applied after the reception of the TA command
  // - Timing adjustment computed according to TS 38.213 section 4.2
  // - Durations of N1 and N2 symbols corresponding to PDSCH and PUSCH are
  //   computed according to sections 5.3 and 6.4 of TS 38.214 */
  const int numerology = ue->frame_parms.numerology_index;
  const int ofdm_symbol_size = ue->frame_parms.ofdm_symbol_size;
  const int nb_prefix_samples = ue->frame_parms.nb_prefix_samples;
  const int samples_per_subframe = ue->frame_parms.samples_per_subframe;
  const int slots_per_frame = ue->frame_parms.slots_per_frame;
  const int slots_per_subframe = ue->frame_parms.slots_per_subframe;

  const double tc_factor = 1.0 / samples_per_subframe;
  // convert time factor "16 * 64 * T_c / (2^mu)" in N_TA calculation in TS38.213 section 4.2 to samples by multiplying with samples per second
  //   16 * 64 * T_c            / (2^mu) * samples_per_second
  // = 16 * T_s                 / (2^mu) * samples_per_second
  // = 16 * 1 / (15 kHz * 2048) / (2^mu) * (15 kHz * 2^mu * ofdm_symbol_size)
  // = 16 * 1 /           2048           *                  ofdm_symbol_size
  // = 16 * ofdm_symbol_size / 2048
  uint16_t bw_scaling = 16 * ofdm_symbol_size / 2048;

  const int Ta_max = 3846; // Max value of 12 bits TA Command
  const double N_TA_max = Ta_max * bw_scaling * tc_factor;

  // symbols corresponding to a PDSCH processing time for UE processing capability 1
  // when additional PDSCH DM-RS is configured
  int N_1 = pdsch_N_1_capability_1[numerology][3];

  /* PUSCH preapration time N_2 for processing capability 1 */
  const int N_2 = pusch_N_2_timing_capability_1[numerology][1];

  /* N_t_1 time duration in msec of N_1 symbols corresponding to a PDSCH reception time
  // N_t_2 time duration in msec of N_2 symbols corresponding to a PUSCH preparation time */
  double N_t_1 = N_1 * (ofdm_symbol_size + nb_prefix_samples) * tc_factor;
  double N_t_2 = N_2 * (ofdm_symbol_size + nb_prefix_samples) * tc_factor;

  /* Time alignment procedure */
  // N_t_1 + N_t_2 + N_TA_max must be in msec
  const double t_subframe = 1.0; // subframe duration of 1 msec
  const int ul_tx_timing_adjustment = 1 + (int)ceil(slots_per_subframe * (N_t_1 + N_t_2 + N_TA_max + 0.5) / t_subframe);

  if (ta_command_pdu->is_rar) {
    ue->ta_slot = ta_command_pdu->ta_slot;
    ue->ta_frame = ta_command_pdu->ta_frame;
    ue->ta_command = ta_command_pdu->ta_command + 31; // To use TA adjustment algo in ue_ta_procedures()
  } else {
    ue->ta_slot = (ta_command_pdu->ta_slot + ul_tx_timing_adjustment) % slots_per_frame;
    if (ta_command_pdu->ta_slot + ul_tx_timing_adjustment > slots_per_frame)
      ue->ta_frame = (ta_command_pdu->ta_frame + 1) % 1024;
    else
      ue->ta_frame = ta_command_pdu->ta_frame;
    ue->ta_command = ta_command_pdu->ta_command;
  }

  LOG_D(PHY,
        "TA command received in %d.%d Starting UL time alignment procedures. TA update will be applied at frame %d slot %d\n",
        ta_command_pdu->ta_frame, ta_command_pdu->ta_slot, ue->ta_frame, ue->ta_slot);
}

static void nr_ue_scheduled_response_dl(NR_UE_MAC_INST_t *mac,
                                        PHY_VARS_NR_UE *phy,
                                        fapi_nr_dl_config_request_t *dl_config,
                                        nr_phy_data_t *phy_data)
{
  AssertFatal(dl_config->number_pdus < FAPI_NR_DL_CONFIG_LIST_NUM,
              "dl_config->number_pdus %d out of bounds\n",
              dl_config->number_pdus);

  for (int i = 0; i < dl_config->number_pdus; ++i) {
    fapi_nr_dl_config_request_pdu_t *pdu = dl_config->dl_config_list + i;
    AssertFatal(pdu->pdu_type <= FAPI_NR_DL_CONFIG_TYPES, "pdu_type %d\n", pdu->pdu_type);
    LOG_D(PHY, "Copying DL %s PDU of %d total DL PDUs:\n", dl_pdu_type[pdu->pdu_type - 1], dl_config->number_pdus);

    switch (pdu->pdu_type) {
      case FAPI_NR_DL_CONFIG_TYPE_DCI:
        AssertFatal(phy_data->phy_pdcch_config.nb_search_space < FAPI_NR_MAX_SS, "Fix array size not large enough\n");
        const int nextFree = phy_data->phy_pdcch_config.nb_search_space;
        phy_data->phy_pdcch_config.pdcch_config[nextFree] = pdu->dci_config_pdu.dci_config_rel15;
        phy_data->phy_pdcch_config.nb_search_space++;
        LOG_D(PHY, "Number of DCI SearchSpaces %d\n", phy_data->phy_pdcch_config.nb_search_space);
        break;
      case FAPI_NR_DL_CONFIG_TYPE_CSI_IM:
        phy_data->csiim_vars.csiim_config_pdu = pdu->csiim_config_pdu.csiim_config_rel15;
        phy_data->csiim_vars.active = true;
        break;
      case FAPI_NR_DL_CONFIG_TYPE_CSI_RS:
        phy_data->csirs_vars.csirs_config_pdu = pdu->csirs_config_pdu.csirs_config_rel15;
        phy_data->csirs_vars.active = true;
        break;
      case FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH: {
        fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu = &pdu->dlsch_config_pdu.dlsch_config_rel15;
        NR_UE_DLSCH_t *dlsch0 = phy_data->dlsch + 0;
        dlsch0->rnti_type = TYPE_RA_RNTI_;
        dlsch0->dlsch_config = *dlsch_config_pdu;
        configure_dlsch(dlsch0, phy->dl_harq_processes[0], dlsch_config_pdu, mac, pdu->dlsch_config_pdu.rnti);
      } break;
      case FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH: {
        fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu = &pdu->dlsch_config_pdu.dlsch_config_rel15;
        NR_UE_DLSCH_t *dlsch0 = phy_data->dlsch + 0;
        dlsch0->rnti_type = TYPE_SI_RNTI_;
        dlsch0->dlsch_config = *dlsch_config_pdu;
        configure_dlsch(dlsch0, phy->dl_harq_processes[0], dlsch_config_pdu, mac, pdu->dlsch_config_pdu.rnti);
      } break;
      case FAPI_NR_DL_CONFIG_TYPE_DLSCH: {
        fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu = &pdu->dlsch_config_pdu.dlsch_config_rel15;
        NR_UE_DLSCH_t *dlsch0 = &phy_data->dlsch[0];
        dlsch0->rnti_type = TYPE_C_RNTI_;
        dlsch0->dlsch_config = *dlsch_config_pdu;
        configure_dlsch(dlsch0, phy->dl_harq_processes[0], dlsch_config_pdu, mac, pdu->dlsch_config_pdu.rnti);
      } break;
      case FAPI_NR_CONFIG_TA_COMMAND:
        configure_ta_command(phy, &pdu->ta_command_pdu);
        break;
      case FAPI_NR_DL_NTN_CONFIG_PARAMS:
        configure_ntn_params(phy, &pdu->ntn_config_command_pdu);
        break;
      default:
        LOG_W(PHY, "unhandled dl pdu type %d \n", pdu->pdu_type);
    }
  }
  dl_config->number_pdus = 0;
}

static void nr_ue_scheduled_response_ul(PHY_VARS_NR_UE *phy, fapi_nr_ul_config_request_t *ul_config, nr_phy_data_tx_t *phy_data)
{
  fapi_nr_ul_config_request_pdu_t *pdu = fapiLockIterator(ul_config, ul_config->frame, ul_config->slot);
  if (!pdu) {
    LOG_E(NR_MAC, "Error in locking ul scheduler dtata\n");
    return;
  }

  while (pdu->pdu_type != FAPI_NR_END) {
    switch (pdu->pdu_type) {
      case FAPI_NR_UL_CONFIG_TYPE_PUSCH: {
        // pusch config pdu
        int current_harq_pid = pdu->pusch_config_pdu.pusch_data.harq_process_id;
        NR_UL_UE_HARQ_t *harq_process_ul_ue = &phy->ul_harq_processes[current_harq_pid];
        nfapi_nr_ue_pusch_pdu_t *pusch_pdu = &phy_data->ulsch.pusch_pdu;
        LOG_D(PHY,
              "copy pusch_config_pdu nrOfLayers: %d, num_dmrs_cdm_grps_no_data: %d\n",
              pdu->pusch_config_pdu.nrOfLayers,
              pdu->pusch_config_pdu.num_dmrs_cdm_grps_no_data);

        memcpy(pusch_pdu, &pdu->pusch_config_pdu, sizeof(*pusch_pdu));
        if (pdu->pusch_config_pdu.tx_request_body.fapiTxPdu) {
          LOG_D(PHY,
                "%d.%d Copying %d bytes to harq_process_ul_ue->a (harq_pid %d)\n",
                ul_config->frame,
                ul_config->slot,
                pdu->pusch_config_pdu.tx_request_body.pdu_length,
                current_harq_pid);
          memcpy(harq_process_ul_ue->payload_AB,
                 pdu->pusch_config_pdu.tx_request_body.fapiTxPdu,
                 pdu->pusch_config_pdu.tx_request_body.pdu_length);
        }

        phy_data->ulsch.status = ACTIVE;
        pdu->pdu_type = FAPI_NR_UL_CONFIG_TYPE_DONE; // not handle it any more
      } break;

      case FAPI_NR_UL_CONFIG_TYPE_PUCCH: {
        bool found = false;
        for (int j = 0; j < 2; j++) {
          if (phy_data->pucch_vars.active[j] == false) {
            LOG_D(PHY, "Copying pucch pdu to UE PHY\n");
            phy_data->pucch_vars.pucch_pdu[j] = pdu->pucch_config_pdu;
            phy_data->pucch_vars.active[j] = true;
            found = true;
            pdu->pdu_type = FAPI_NR_UL_CONFIG_TYPE_DONE; // not handle it any more
            break;
          }
        }
        if (!found)
          LOG_E(PHY, "Couldn't find allocation for PUCCH PDU in PUCCH VARS\n");
      } break;

      case FAPI_NR_UL_CONFIG_TYPE_PRACH: {
        phy->prach_vars[0]->prach_pdu = pdu->prach_config_pdu;
        phy->prach_vars[0]->active = true;
        pdu->pdu_type = FAPI_NR_UL_CONFIG_TYPE_DONE; // not handle it any more
      } break;

      case FAPI_NR_UL_CONFIG_TYPE_DONE:
        break;

      case FAPI_NR_UL_CONFIG_TYPE_SRS:
        // srs config pdu
        phy_data->srs_vars.srs_config_pdu = pdu->srs_config_pdu;
        phy_data->srs_vars.active = true;
        pdu->pdu_type = FAPI_NR_UL_CONFIG_TYPE_DONE; // not handle it any more
        break;

      default:
        LOG_W(PHY, "unhandled ul pdu type %d \n", pdu->pdu_type);
        break;
    }
    pdu++;
  }

  LOG_D(PHY, "clear ul_config\n");
  release_ul_config(pdu, true);
}

int8_t nr_ue_scheduled_response(nr_scheduled_response_t *scheduled_response)
{
  PHY_VARS_NR_UE *phy = PHY_vars_UE_g[scheduled_response->module_id][scheduled_response->CC_id];
  AssertFatal(!scheduled_response->dl_config || !scheduled_response->ul_config || !scheduled_response->sl_rx_config
                  || !scheduled_response->sl_tx_config,
              "phy_data parameter will be cast to two different types!\n");

  if (scheduled_response->dl_config)
    nr_ue_scheduled_response_dl(scheduled_response->mac,
                                phy,
                                scheduled_response->dl_config,
                                (nr_phy_data_t *)scheduled_response->phy_data);
  if (scheduled_response->ul_config)
    nr_ue_scheduled_response_ul(phy, scheduled_response->ul_config, (nr_phy_data_tx_t *)scheduled_response->phy_data);

  if (scheduled_response->sl_rx_config || scheduled_response->sl_tx_config) {
    sl_handle_scheduled_response(scheduled_response);
  }

  return 0;
}

void nr_ue_phy_config_request(nr_phy_config_t *phy_config)
{
  PHY_VARS_NR_UE *phy = PHY_vars_UE_g[phy_config->Mod_id][phy_config->CC_id];
  fapi_nr_config_request_t *nrUE_config = &phy->nrUE_config;
  if(phy_config != NULL) {
    phy->received_config_request = true;
    memcpy(nrUE_config, &phy_config->config_req, sizeof(fapi_nr_config_request_t));
  }
}

void nr_ue_synch_request(nr_synch_request_t *synch_request)
{
  fapi_nr_synch_request_t *synch_req = &PHY_vars_UE_g[synch_request->Mod_id][synch_request->CC_id]->synch_request.synch_req;
  memcpy(synch_req, &synch_request->synch_req, sizeof(fapi_nr_synch_request_t));
  PHY_vars_UE_g[synch_request->Mod_id][synch_request->CC_id]->synch_request.received_synch_request = 1;
}

void nr_ue_sl_phy_config_request(nr_sl_phy_config_t *phy_config)
{
  PHY_VARS_NR_UE *phy = PHY_vars_UE_g[phy_config->Mod_id][phy_config->CC_id];
  sl_nr_phy_config_request_t *sl_config = &phy->SL_UE_PHY_PARAMS.sl_config;
  if (phy_config != NULL) {
    phy->received_config_request = true;
    memcpy(sl_config, &phy_config->sl_config_req, sizeof(sl_nr_phy_config_request_t));
  }
}

/*
 * MAC sends the scheduled response with either TX configrequest for Sidelink Transmission requests
 * or RX config request for Sidelink Reception requests.
 * This procedure handles these TX/RX config requests received in this slot and configures PHY
 * with a TTI action to be performed in this slot(TTI)
 */
void sl_handle_scheduled_response(nr_scheduled_response_t *scheduled_response)
{
  module_id_t module_id = scheduled_response->module_id;
  const char *sl_rx_action[] = {"NONE", "RX_PSBCH", "RX_PSCCH", "RX_SCI2_ON_PSSCH", "RX_SLSCH_ON_PSSCH"};
  const char *sl_tx_action[] = {"TX_PSBCH", "TX_PSCCH_PSSCH", "TX_PSFCH"};

  if (scheduled_response->sl_rx_config != NULL) {
    sl_nr_rx_config_request_t *sl_rx_config = scheduled_response->sl_rx_config;
    nr_phy_data_t *phy_data = (nr_phy_data_t *)scheduled_response->phy_data;

    AssertFatal(sl_rx_config->number_pdus == SL_NR_RX_CONFIG_LIST_NUM, "sl_rx_config->number_pdus incorrect\n");

    switch (sl_rx_config->sl_rx_config_list[0].pdu_type) {
      case SL_NR_CONFIG_TYPE_RX_PSBCH:
        phy_data->sl_rx_action = SL_NR_CONFIG_TYPE_RX_PSBCH;
        LOG_D(PHY, "Recvd CONFIG_TYPE_RX_PSBCH\n");
        break;
      default:
        AssertFatal(0, "Incorrect sl_rx config req pdutype \n");
        break;
    }

    LOG_D(PHY,
          "[UE%d] TTI %d:%d, SL-RX action:%s\n",
          module_id,
          sl_rx_config->sfn,
          sl_rx_config->slot,
          sl_rx_action[phy_data->sl_rx_action]);

  } else if (scheduled_response->sl_tx_config != NULL) {
    sl_nr_tx_config_request_t *sl_tx_config = scheduled_response->sl_tx_config;
    nr_phy_data_tx_t *phy_data_tx = (nr_phy_data_tx_t *)scheduled_response->phy_data;

    AssertFatal(sl_tx_config->number_pdus == SL_NR_TX_CONFIG_LIST_NUM, "sl_tx_config->number_pdus incorrect \n");

    switch (sl_tx_config->tx_config_list[0].pdu_type) {
      case SL_NR_CONFIG_TYPE_TX_PSBCH:
        phy_data_tx->sl_tx_action = SL_NR_CONFIG_TYPE_TX_PSBCH;
        LOG_D(PHY, "Recvd CONFIG_TYPE_TX_PSBCH\n");
        *((uint32_t *)phy_data_tx->psbch_vars.psbch_payload) =
            *((uint32_t *)sl_tx_config->tx_config_list[0].tx_psbch_config_pdu.psbch_payload);
        phy_data_tx->psbch_vars.psbch_tx_power = sl_tx_config->tx_config_list[0].tx_psbch_config_pdu.psbch_tx_power;
        phy_data_tx->psbch_vars.tx_slss_id = sl_tx_config->tx_config_list[0].tx_psbch_config_pdu.tx_slss_id;
        break;
      default:
        AssertFatal(0, "Incorrect sl_tx config req pdutype \n");
        break;
    }

    LOG_D(PHY,
          "[UE%d] TTI %d:%d, SL-TX action:%s slss_id:%d, sl-mib:%x, psbch pwr:%d\n",
          module_id,
          sl_tx_config->sfn,
          sl_tx_config->slot,
          sl_tx_action[phy_data_tx->sl_tx_action - 6],
          phy_data_tx->psbch_vars.tx_slss_id,
          *((uint32_t *)phy_data_tx->psbch_vars.psbch_payload),
          phy_data_tx->psbch_vars.psbch_tx_power);
  }

}
