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

#ifndef E1AP_BEARER_CONTEXT_MANAGEMENT_H_
#define E1AP_BEARER_CONTEXT_MANAGEMENT_H_

#include <stdbool.h>
#include "openair2/COMMON/e1ap_messages_types.h"
#include "common/platform_types.h"

struct E1AP_E1AP_PDU;

struct E1AP_E1AP_PDU *encode_E1_bearer_context_setup_request(const e1ap_bearer_setup_req_t *msg);
bool decode_E1_bearer_context_setup_request(const struct E1AP_E1AP_PDU *pdu, e1ap_bearer_setup_req_t *out);
e1ap_bearer_setup_req_t cp_bearer_context_setup_request(const e1ap_bearer_setup_req_t *msg);
void free_e1ap_context_setup_request(e1ap_bearer_setup_req_t *msg);
bool eq_bearer_context_setup_request(const e1ap_bearer_setup_req_t *a, const e1ap_bearer_setup_req_t *b);

struct E1AP_E1AP_PDU *encode_E1_bearer_context_setup_response(const e1ap_bearer_setup_resp_t *msg);
bool decode_E1_bearer_context_setup_response(const struct E1AP_E1AP_PDU *pdu, e1ap_bearer_setup_resp_t *out);
void free_e1ap_context_setup_response(const e1ap_bearer_setup_resp_t *msg);
e1ap_bearer_setup_resp_t cp_bearer_context_setup_response(const e1ap_bearer_setup_resp_t *msg);
bool eq_bearer_context_setup_response(const e1ap_bearer_setup_resp_t *a, const e1ap_bearer_setup_resp_t *b);

struct E1AP_E1AP_PDU *encode_E1_bearer_context_setup_failure(const e1ap_bearer_context_setup_failure_t *msg);
bool decode_E1_bearer_context_setup_failure(e1ap_bearer_context_setup_failure_t *out, const struct E1AP_E1AP_PDU *pdu);
e1ap_bearer_context_setup_failure_t cp_bearer_context_setup_failure(const e1ap_bearer_context_setup_failure_t *msg);
bool eq_bearer_context_setup_failure(const e1ap_bearer_context_setup_failure_t *a, const e1ap_bearer_context_setup_failure_t *b);
void free_e1_bearer_context_setup_failure(const e1ap_bearer_context_setup_failure_t *msg);

struct E1AP_E1AP_PDU *encode_e1_bearer_context_release_command(const e1ap_bearer_release_cmd_t *msg);
bool decode_e1_bearer_context_release_command(e1ap_bearer_release_cmd_t *out, const struct E1AP_E1AP_PDU *pdu);
bool eq_bearer_context_release_command(const e1ap_bearer_release_cmd_t *a, const e1ap_bearer_release_cmd_t *b);
e1ap_bearer_release_cmd_t cp_bearer_context_release_command(const e1ap_bearer_release_cmd_t *msg);
void free_e1_bearer_context_release_command(const e1ap_bearer_release_cmd_t *msg);

struct E1AP_E1AP_PDU *encode_e1_bearer_context_release_complete(const e1ap_bearer_release_cplt_t *cplt);
bool decode_e1_bearer_context_release_complete(e1ap_bearer_release_cplt_t *out, const struct E1AP_E1AP_PDU *pdu);
bool eq_bearer_context_release_complete(const e1ap_bearer_release_cplt_t *a, const e1ap_bearer_release_cplt_t *b);
e1ap_bearer_release_cplt_t cp_bearer_context_release_complete(const e1ap_bearer_release_cplt_t *src);
void free_e1_bearer_context_release_complete(const e1ap_bearer_release_cplt_t *msg);

struct E1AP_E1AP_PDU *encode_E1_bearer_context_mod_request(const e1ap_bearer_mod_req_t *msg);
bool decode_E1_bearer_context_mod_request(const struct E1AP_E1AP_PDU *pdu, e1ap_bearer_mod_req_t *out);
void free_e1ap_context_mod_request(const e1ap_bearer_mod_req_t *msg);
e1ap_bearer_mod_req_t cp_bearer_context_mod_request(const e1ap_bearer_mod_req_t *msg);
bool eq_bearer_context_mod_request(const e1ap_bearer_mod_req_t *a, const e1ap_bearer_mod_req_t *b);

struct E1AP_E1AP_PDU *encode_E1_bearer_context_mod_response(const e1ap_bearer_modif_resp_t *msg);
bool decode_E1_bearer_context_mod_response(e1ap_bearer_modif_resp_t *out, const struct E1AP_E1AP_PDU *pdu);
void free_e1ap_context_mod_response(const e1ap_bearer_modif_resp_t *msg);
e1ap_bearer_modif_resp_t cp_bearer_context_mod_response(const e1ap_bearer_modif_resp_t *msg);
bool eq_bearer_context_mod_response(const e1ap_bearer_modif_resp_t *a, const e1ap_bearer_modif_resp_t *b);

struct E1AP_E1AP_PDU *encode_E1_bearer_context_mod_failure(const e1ap_bearer_context_mod_failure_t *msg);
bool decode_E1_bearer_context_mod_failure(e1ap_bearer_context_mod_failure_t *out, const struct E1AP_E1AP_PDU *pdu);
void free_E1_bearer_context_mod_failure(const e1ap_bearer_context_mod_failure_t *msg);
e1ap_bearer_context_mod_failure_t cp_E1_bearer_context_mod_failure(const e1ap_bearer_context_mod_failure_t *msg);
bool eq_E1_bearer_context_mod_failure(const e1ap_bearer_context_mod_failure_t *a, const e1ap_bearer_context_mod_failure_t *b);

#endif /* E1AP_BEARER_CONTEXT_MANAGEMENT_H_ */
