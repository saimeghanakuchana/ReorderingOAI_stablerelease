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

#include <netinet/in.h>
#include <arpa/inet.h>
#include "PduSessionEstablishmentAccept.h"
#include "common/utils/LOG/log.h"
#include "common/utils/tun_if.h"
#include "fgs_nas_utils.h"

/**
 * @brief Returns the size of the single QoS rule IE
 */
static uint8_t get_len_qos_rule(qos_rule_t *rule)
{
  return rule->length + sizeof(rule->id) + sizeof(rule->length);
}

/**
 * @brief Decode QoS Rule (9.11.4.13 of 3GPP TS 24.501)
 */
static qos_rule_t decode_qos_rule(uint8_t *buf)
{
  qos_rule_t qos_rule = {0};
  // octet 4
  qos_rule.id = *buf++;
  // octet 5 - 6
  GET_SHORT(buf, qos_rule.length);
  buf += sizeof(qos_rule.length);
  // octet 7
  qos_rule.oc = (*(buf)&0xE0) >> 5;
  qos_rule.dqr = (*(buf)&0x10) >> 4;
  qos_rule.nb_pf = *buf++ & 0x0F;
  // octet 8 - m
  for (int i = 0; i < qos_rule.nb_pf; i++) {
    packet_filter_t pf;
    if (qos_rule.oc == ROC_CREATE_NEW_QOS_RULE || qos_rule.oc == ROC_MODIFY_QOS_RULE_ADD_PF
        || qos_rule.oc == ROC_MODIFY_QOS_RULE_REPLACE_PF) {
      pf.pf_type.type_1.pf_dir = (*buf & 0x30) >> 4;
      pf.pf_type.type_1.pf_id = *buf++ & 0x0F;
      pf.pf_type.type_1.length = *buf++;
      buf += pf.pf_type.type_1.length; // skip PF content
    } else if (qos_rule.oc == ROC_MODIFY_QOS_RULE_DELETE_PF) {
      pf.pf_type.type_2.pf_id = *buf++;
    }
  }
  // octet m + 1
  qos_rule.precendence = *buf++;
  // octet m + 2
  qos_rule.qfi = *buf++ & 0x3F;
  return qos_rule;
}

/**
 * @brief PDU session establishment accept (8.3.2 of 3GPP TS 24.501)
 *        network to UE
 */
int decode_pdu_session_establishment_accept_msg(pdu_session_establishment_accept_msg_t *psea_msg, uint8_t *buffer, uint32_t msg_length)
{
  uint8_t *curPtr = buffer;

  /* Mandatory PDU Session Establishment Accept IEs */
  // PDU Session Type (half octet)
  psea_msg->pdu_type = *curPtr & 0x0f;
  // SSC Mode (half octet)
  psea_msg->ssc_mode = (*curPtr++ & 0xf0) >> 4;
  // Authorized QoS Rules
  auth_qos_rule_t *qos_rules = &psea_msg->qos_rules;
  // Length of the rule IEs (2 octets)
  GET_SHORT(curPtr, qos_rules->length);
  curPtr += sizeof(qos_rules->length);
  /* Decode rule IEs as long as the total length
    of all IEs (qos_rules->length) is not reached */
  uint16_t rules_tot_len = 0;
  while (rules_tot_len < qos_rules->length)
  {
    qos_rules->rule[0] = decode_qos_rule(curPtr);
    rules_tot_len += get_len_qos_rule(&qos_rules->rule[0]);
    curPtr += get_len_qos_rule(&qos_rules->rule[0]);
  }
  // Session-AMBR (M)
  psea_msg->sess_ambr.length = *curPtr++;
  curPtr += psea_msg->sess_ambr.length; // skip content

  uint8_t *resPtr = curPtr;

  /* Optional Presence IEs */
  while (curPtr < buffer + msg_length) {
    uint8_t psea_iei = *curPtr++;
    LOG_T(NAS, "PDU SESSION ESTABLISHMENT ACCEPT - Received %s IEI\n", iei_text_desc[psea_iei].text);
    switch (psea_iei) {
      case IEI_5GSM_CAUSE: /* Ommited */
        curPtr++;
        break;

      case IEI_PDU_ADDRESS:
        psea_msg->pdu_addr_ie.pdu_length = *curPtr++;

        /* Octet 3 */
        // PDU type (3 bits)
        psea_msg->pdu_addr_ie.pdu_type = *curPtr & 0x07;
        // SMF's IPv6 link local address (1 bit)
        uint8_t si6lla = (*curPtr >> 3) & 0x01;
        if (si6lla)
          LOG_E(NAS, "SMF's IPv6 link local address is not handled\n");
        curPtr++;

        /* Octet 4 to n */
        // PDU address information
        uint8_t *addr = psea_msg->pdu_addr_ie.pdu_addr_oct;
        if (psea_msg->pdu_addr_ie.pdu_type == PDU_SESSION_TYPE_IPV4) {
          for (int i = 0; i < IPv4_ADDRESS_LENGTH; ++i)
            addr[i] = *curPtr++;
          LOG_I(NAS, "Received PDU Session Establishment Accept, UE IPv4: %u.%u.%u.%u\n", addr[0], addr[1], addr[2], addr[3]);
        } else if (psea_msg->pdu_addr_ie.pdu_type == PDU_SESSION_TYPE_IPV6) {
          for (int i = 0; i < IPv6_INTERFACE_ID_LENGTH; ++i)
            addr[i] = *curPtr++;
          LOG_I(NAS, "Received PDU Session Establishment Accept, UE IPv6: %u.%u.%u.%u.%u.%u.%u.%u\n",
                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
        } else if (psea_msg->pdu_addr_ie.pdu_type == PDU_SESSION_TYPE_IPV4V6) {
          // 24.501 Sec 9.11.4.10: "If the PDU session type value indicates
          // IPv4v6, the PDU address information in octet 4 to octet 11
          // contains an interface identifier for the IPv6 link local address
          // and in octet 12 to octet 15 contains an IPv4 address."
          for (int i = 0; i < IPv4_ADDRESS_LENGTH + IPv6_INTERFACE_ID_LENGTH; ++i)
            addr[i] = *curPtr++;
          LOG_I(NAS, "Received PDU Session Establishment Accept, UE IPv4v6: %u.%u.%u.%u/%u.%u.%u.%u.%u.%u.%u.%u\n",
                addr[0], addr[1], addr[2], addr[3],
                addr[4], addr[5], addr[6], addr[7], addr[8], addr[9], addr[10], addr[11]);
        } else {
          LOG_E(NAS, "unknown/unhandled PDU session establishment accept PDU type %d\n", psea_msg->pdu_addr_ie.pdu_type);
          curPtr += psea_msg->pdu_addr_ie.pdu_length;
        }
        break;

      case IEI_RQ_TIMER_VALUE: /* Ommited */
        curPtr++; /* TS 24.008 10.5.7.3 */
        break;

      case IEI_SNSSAI: {
        uint8_t snssai_length = *curPtr;
        curPtr += (snssai_length + sizeof(snssai_length));
        break;
      }

      case IEI_ALWAYSON_PDU: /* Ommited */
        curPtr++;
        break;

      case IEI_MAPPED_EPS: {
        uint16_t mapped_eps_length = 0;
        GET_SHORT(curPtr, mapped_eps_length);
        curPtr += mapped_eps_length;
        break;
      }

      case IEI_EAP_MSG: {
        uint16_t eap_length = 0;
        GET_SHORT(curPtr, eap_length);
        curPtr += (eap_length + sizeof(eap_length));
        break;
      }

      case IEI_AUTH_QOS_DESC: {
        GET_SHORT(curPtr, psea_msg->qos_fd_ie.length);
        curPtr += (psea_msg->qos_fd_ie.length + sizeof(psea_msg->qos_fd_ie.length));
        break;
      }

      case IEI_EXT_CONF_OPT: {
        GET_SHORT(curPtr, psea_msg->ext_pp_ie.length);
        curPtr += (psea_msg->ext_pp_ie.length + sizeof(psea_msg->ext_pp_ie.length));
        break;
      }

      case IEI_DNN: {
        psea_msg->dnn_ie.dnn_length = *curPtr++;
        char apn[APN_MAX_LEN];

        if (psea_msg->dnn_ie.dnn_length <= APN_MAX_LEN && psea_msg->dnn_ie.dnn_length >= APN_MIN_LEN) {
          memcpy(apn, curPtr, psea_msg->dnn_ie.dnn_length);
          LOG_T(NAS, "PDU SESSION ESTABLISHMENT ACCEPT - APN: %s\n", apn);
        } else
          LOG_E(NAS, "PDU SESSION ESTABLISHMENT ACCEPT - DNN IE has invalid length\n");
        resPtr = curPtr;
        curPtr = buffer + msg_length; // we force stop processing
        break;
      }

      default:
        PRINT_NAS_ERROR("Unknown IEI %d\n", psea_iei);
        curPtr = buffer + msg_length; // we force stop processing
        break;
    }
  }
  return resPtr - buffer;
}
