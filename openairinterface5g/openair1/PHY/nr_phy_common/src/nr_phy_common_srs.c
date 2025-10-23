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

#include "PHY/nr_phy_common/inc/nr_phy_common.h"
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/NR_REFSIG/ul_ref_seq_nr.h"

#define SRS_PERIODICITY                 (17)
static const uint16_t srs_periodicity[SRS_PERIODICITY] = {1, 2, 4, 5, 8, 10, 16, 20, 32, 40, 64, 80, 160, 320, 640, 1280, 2560};
// TS 38.211 - Table 6.4.1.4.2-1
static const uint16_t srs_max_number_cs[3] = {8, 12, 6};

//#define SRS_DEBUG

static int group_number_hopping(int slot_number, uint8_t n_ID_SRS, uint8_t l0, uint8_t l_line)
{
  // Pseudo-random sequence c(i) defined by TS 38.211 - Section 5.2.1
  uint32_t cinit = n_ID_SRS;
  uint8_t c_last_index = 8 * (slot_number * NR_SYMBOLS_PER_SLOT + l0 + l_line) + 7;
  uint32_t *c_sequence =  calloc(c_last_index + 1, sizeof(uint32_t));
  pseudo_random_sequence(c_last_index + 1, c_sequence, cinit);

  // TS 38.211 - 6.4.1.4.2 Sequence generation
  uint32_t f_gh = 0;
  for (int m = 0; m <= 7; m++) {
    f_gh += c_sequence[8 * (slot_number * NR_SYMBOLS_PER_SLOT + l0 + l_line) + m] << m;
  }
  f_gh = f_gh % 30;
  int u = (f_gh + n_ID_SRS) % U_GROUP_NUMBER;
  free(c_sequence);
  return u;
}

static int sequence_number_hopping(int slot_number, uint8_t n_ID_SRS, uint16_t M_sc_b_SRS, uint8_t l0, uint8_t l_line)
{
  int v = 0;
  if (M_sc_b_SRS > 6 * NR_NB_SC_PER_RB) {
    // Pseudo-random sequence c(i) defined by TS 38.211 - Section 5.2.1
    uint32_t cinit = n_ID_SRS;
    uint8_t c_last_index = (slot_number * NR_SYMBOLS_PER_SLOT + l0 + l_line);
    uint32_t *c_sequence =  calloc(c_last_index + 1, sizeof(uint32_t));
    pseudo_random_sequence(c_last_index + 1,  c_sequence, cinit);
    // TS 38.211 - 6.4.1.4.2 Sequence generation
    v = c_sequence[c_last_index];
    free(c_sequence);
  }
  return v;
}

static int compute_F_b(frame_t frame_number,
                       slot_t slot_number,
                       int slots_per_frame,
                       nr_srs_info_t *nr_srs_info,
                       uint8_t l_line,
                       uint8_t b)
{
  // Compute the number of SRS transmissions
  uint16_t n_SRS = 0;
  if (nr_srs_info->resource_type == aperiodic) {
    n_SRS = l_line / nr_srs_info->R;
  } else {
    int t = (slots_per_frame * frame_number + slot_number - nr_srs_info->T_offset) / nr_srs_info->T_SRS;
    n_SRS = t * (nr_srs_info->N_symb_SRS / nr_srs_info->R) + (l_line / nr_srs_info->R);
  }

  uint16_t product_N_b = 1;
  for (unsigned int b_prime = nr_srs_info->b_hop; b_prime < nr_srs_info->B_SRS; b_prime++) {
    if (b_prime != nr_srs_info->b_hop) {
      product_N_b *= get_N_b_srs(nr_srs_info->C_SRS, b_prime);
    }
  }

  int F_b = 0;
  uint8_t N_b = get_N_b_srs(nr_srs_info->C_SRS, b);
  if (N_b & 1) { // Nb odd
    F_b = (N_b / 2) * (n_SRS / product_N_b);
  } else { // Nb even
    uint16_t product_N_b_B_SRS = product_N_b;
    product_N_b_B_SRS *= get_N_b_srs(nr_srs_info->C_SRS, nr_srs_info->B_SRS); /* product for b_hop to b */
    F_b = (N_b / 2) * ((n_SRS % product_N_b_B_SRS) / product_N_b) + ((n_SRS % product_N_b_B_SRS) / 2 * product_N_b);
  }
  return F_b;
}

static int compute_n_b(frame_t frame_number,
                       slot_t slot_number,
                       uint16_t slots_per_frame,
                       nr_srs_info_t *nr_srs_info,
                       uint8_t l_line,
                       uint8_t b,
                       int m_SRS_b)
{
  uint8_t N_b = get_N_b_srs(nr_srs_info->C_SRS, b);
  int n_b = 0;
  if (nr_srs_info->b_hop >= nr_srs_info->B_SRS) {
    n_b = (4 * nr_srs_info->n_RRC / m_SRS_b) % N_b;
  } else {
    if (b <= nr_srs_info->b_hop) {
      n_b = (4 * nr_srs_info->n_RRC / m_SRS_b) % N_b;
    } else {
      // Compute the hopping offset Fb
      int F_b = compute_F_b(frame_number, slot_number, slots_per_frame, nr_srs_info, l_line, b);
      n_b = (F_b + (4 * nr_srs_info->n_RRC / m_SRS_b)) % N_b;
    }
  }
  return n_b;
}

/*************************************************************************
*
* NAME :         generate_srs_nr
*
* PARAMETERS :   pointer to srs config pdu
*                pointer to transmit buffer
*                amplitude scaling for this physical signal
*                slot number of transmission
* RETURN :       0  if srs sequence has been successfully generated
*                -1 if sequence can not be properly generated
*
* DESCRIPTION :  generate/map srs symbol into transmit buffer
*                See TS 38.211 - Section 6.4.1.4 Sounding reference signal
*
***************************************************************************/
bool generate_srs_nr(NR_DL_FRAME_PARMS *frame_parms,
                     c16_t **txdataF,
                     uint16_t symbol_offset,
                     int bwp_start,
                     nr_srs_info_t *nr_srs_info,
                     int16_t amp,
                     frame_t frame_number,
                     slot_t slot_number)
{
  uint8_t K_TC = 2 << nr_srs_info->comb_size;
  /* Number of antenna ports (M) can't be higher than number of physical antennas (N): M <= N */
  int N_ap = nr_srs_info->n_srs_ports > frame_parms->nb_antennas_tx ? frame_parms->nb_antennas_tx : nr_srs_info->n_srs_ports;
  uint8_t l0 = frame_parms->symbols_per_slot - 1 - nr_srs_info->l_offset;  // Starting symbol position in the time domain
  uint8_t n_SRS_cs_max = srs_max_number_cs[nr_srs_info->comb_size];
  int m_SRS_b = get_m_srs(nr_srs_info->C_SRS, nr_srs_info->B_SRS);   // Number of resource blocks
  uint16_t M_sc_b_SRS = m_SRS_b * NR_NB_SC_PER_RB/K_TC;       // Length of the SRS sequence

#ifdef SRS_DEBUG
  LOG_I(NR_PHY,"Frame = %i, slot = %i\n", frame_number, slot_number);
  LOG_I(NR_PHY,"B_SRS = %i\n", nr_srs_info->B_SRS);
  LOG_I(NR_PHY,"C_SRS = %i\n", nr_srs_info->C_SRS);
  LOG_I(NR_PHY,"b_hop = %i\n", nr_srs_info->b_hop);
  LOG_I(NR_PHY,"K_TC = %i\n", K_TC);
  LOG_I(NR_PHY,"K_TC_overbar = %i\n", nr_srs_info->K_TC_overbar);
  LOG_I(NR_PHY,"n_SRS_cs = %i\n", nr_srs_info->n_SRS_cs);
  LOG_I(NR_PHY,"n_ID_SRS = %i\n", nr_srs_info->n_ID_SRS);
  LOG_I(NR_PHY,"n_shift = %i\n", nr_srs_info->n_shift);
  LOG_I(NR_PHY,"n_RRC = %i\n", nr_srs_info->n_RRC);
  LOG_I(NR_PHY,"groupOrSequenceHopping = %i\n", nr_srs_info->groupOrSequenceHopping);
  LOG_I(NR_PHY,"l_offset = %i\n", nr_srs_info->l_offset);
  LOG_I(NR_PHY,"T_SRS = %i\n", nr_srs_info->T_SRS);
  LOG_I(NR_PHY,"T_offset = %i\n", nr_srs_info->T_offset);
  LOG_I(NR_PHY,"R = %i\n", nr_srs_info->R);
  LOG_I(NR_PHY,"N_ap = %i\n", N_ap);
  LOG_I(NR_PHY,"N_symb_SRS = %i\n", nr_srs_info->N_symb_SRS);
  LOG_I(NR_PHY,"l0 = %i\n", l0);
  LOG_I(NR_PHY,"n_SRS_cs_max = %i\n", n_SRS_cs_max);
  LOG_I(NR_PHY,"m_SRS_b = %i\n", m_SRS_b);
  LOG_I(NR_PHY,"M_sc_b_SRS = %i\n", M_sc_b_SRS);
#endif

  // Validation of SRS config parameters

  if (nr_srs_info->R == 0) {
    LOG_E(NR_PHY, "generate_srs: this parameter repetition factor %d is not consistent !\n", nr_srs_info->R);
    return false;
  } else if (nr_srs_info->R > nr_srs_info->N_symb_SRS) {
    LOG_E(NR_PHY, "generate_srs: R %d can not be greater than N_symb_SRS %d !\n", nr_srs_info->R, nr_srs_info->N_symb_SRS);
    return false;
  }

  if (nr_srs_info->n_SRS_cs >= n_SRS_cs_max) {
    LOG_E(NR_PHY, "generate_srs: inconsistent parameter n_SRS_cs %d >=  n_SRS_cs_max %d !\n", nr_srs_info->n_SRS_cs, n_SRS_cs_max);
    return false;
  }

  if (nr_srs_info->T_SRS == 0) {
    LOG_E(NR_PHY, "generate_srs: inconsistent parameter T_SRS %d can not be equal to zero !\n", nr_srs_info->T_SRS);
    return false;
  } else {
    int index = 0;
    while (srs_periodicity[index] != nr_srs_info->T_SRS) {
      index++;
      if (index == SRS_PERIODICITY) {
        LOG_E(NR_PHY, "generate_srs: inconsistent parameter T_SRS %d not specified !\n", nr_srs_info->T_SRS);
        return false;
      }
    }
  }

  // Variable initialization
  if(nr_srs_info) {
    nr_srs_info->srs_generated_signal_bits = log2_approx(amp);
  }
  uint64_t subcarrier_offset = frame_parms->first_carrier_offset + bwp_start * NR_NB_SC_PER_RB;
  double sqrt_N_ap = sqrt(N_ap);
  int n_b[nr_srs_info->B_SRS + 1];

  // Find index of table which is for this SRS length
  uint16_t M_sc_b_SRS_index = 0;
  while((ul_allocated_re[M_sc_b_SRS_index] != M_sc_b_SRS) && (M_sc_b_SRS_index < SRS_SB_CONF))
    M_sc_b_SRS_index++;

  // SRS sequence generation and mapping, TS 38.211 - Section 6.4.1.4
  for (int p_index = 0; p_index < N_ap; p_index++) {
#ifdef SRS_DEBUG
    LOG_I(NR_PHY,"============ port %d ============\n", p_index);
#endif

    uint16_t n_SRS_cs_i = (nr_srs_info->n_SRS_cs + (n_SRS_cs_max * (SRS_antenna_port[p_index] - 1000) / N_ap)) % n_SRS_cs_max;
    double alpha_i = 2 * M_PI * ((double)n_SRS_cs_i / (double)n_SRS_cs_max);

#ifdef SRS_DEBUG
    LOG_I(NR_PHY,"n_SRS_cs_i = %i\n", n_SRS_cs_i);
    LOG_I(NR_PHY,"alpha_i = %f\n", alpha_i);
#endif

    for (int l_line = 0; l_line < nr_srs_info->N_symb_SRS; l_line++) {
#ifdef SRS_DEBUG
      LOG_I(NR_PHY,":::::::: OFDM symbol %d ::::::::\n", l0+l_line);
#endif

      // Set group and sequence numbers (u,v) per OFDM symbol
      int u = 0, v = 0;
      switch(nr_srs_info->groupOrSequenceHopping) {
        case neitherHopping:
          u = nr_srs_info->n_ID_SRS % U_GROUP_NUMBER;
          v = 0;
          break;
        case groupHopping:
          u = group_number_hopping(slot_number, nr_srs_info->n_ID_SRS, l0, l_line);
          v = 0;
          break;
        case sequenceHopping:
          u = nr_srs_info->n_ID_SRS % U_GROUP_NUMBER;
          v = sequence_number_hopping(slot_number, nr_srs_info->n_ID_SRS, M_sc_b_SRS, l0, l_line);
          break;
        default:
          LOG_E(NR_PHY, "generate_srs: unknown hopping setting %d !\n", nr_srs_info->groupOrSequenceHopping);
          return false;
      }

#ifdef SRS_DEBUG
      LOG_I(NR_PHY,"u = %i\n", u);
      LOG_I(NR_PHY,"v = %i\n", v);
#endif

      // Compute the frequency position index n_b
      uint16_t sum_n_b = 0;
      for (int b = 0; b <= nr_srs_info->B_SRS; b++) {
        n_b[b] = compute_n_b(frame_number, slot_number, frame_parms->slots_per_frame, nr_srs_info, l_line, b, m_SRS_b);
        sum_n_b += n_b[b];

#ifdef SRS_DEBUG
        LOG_I(NR_PHY,"n_b[%i] = %i\n", b, n_b[b]);
#endif
      }

#ifdef SRS_DEBUG
      LOG_I(NR_PHY,"sum_n_b = %i\n", sum_n_b);
#endif

      // Compute the frequency-domain starting position
      uint8_t K_TC_p = 0;
      if((nr_srs_info->n_SRS_cs >= n_SRS_cs_max / 2)
         && (nr_srs_info->n_SRS_cs < n_SRS_cs_max)
         && (N_ap == 4)
         && ((SRS_antenna_port[p_index] == 1001) || (SRS_antenna_port[p_index] == 1003))) {
        K_TC_p = (nr_srs_info->K_TC_overbar + K_TC / 2) % K_TC;
      } else {
        K_TC_p = nr_srs_info->K_TC_overbar;
      }
      uint8_t k_l_offset = 0; // If the SRS is configured by the IE SRS-PosResource-r16, the quantity k_l_offset is
                              // given by TS 38.211 - Table 6.4.1.4.3-2, otherwise k_l_offset = 0.
      uint8_t k_0_overbar_p = (nr_srs_info->n_shift * NR_NB_SC_PER_RB + (K_TC_p + k_l_offset)) % K_TC;
      uint8_t k_0_p = k_0_overbar_p + K_TC * M_sc_b_SRS * sum_n_b;
      nr_srs_info->k_0_p[p_index][l_line] = k_0_p;

#ifdef SRS_DEBUG
      LOG_I(NR_PHY,"K_TC_p = %i\n", K_TC_p);
      LOG_I(NR_PHY,"k_0_overbar_p = %i\n", k_0_overbar_p);
      LOG_I(NR_PHY,"k_0_p = %i\n", k_0_p);
#endif

      uint16_t subcarrier = subcarrier_offset + k_0_p;
      if (subcarrier >= frame_parms->ofdm_symbol_size)
        subcarrier -= frame_parms->ofdm_symbol_size;
      uint16_t l_line_offset = l_line * frame_parms->ofdm_symbol_size;
      // For each port, and for each OFDM symbol, here it is computed and mapped an SRS sequence with M_sc_b_SRS symbols
      for (int k = 0; k < M_sc_b_SRS; k++) {
        cd_t shift = {cos(alpha_i * k), sin(alpha_i * k)};
        const c16_t tmp = rv_ul_ref_sig[u][v][M_sc_b_SRS_index][k];
        cd_t r_overbar = {tmp.r, tmp.i};

        // cos(x+y) = cos(x)cos(y) - sin(x)sin(y)
        cd_t r = cdMul(shift, r_overbar);
        c16_t r_amp = {(((int32_t)round((double)amp * r.r / sqrt_N_ap)) >> 15),
                       (((int32_t)round((double)amp * r.i / sqrt_N_ap)) >> 15)};

#ifdef SRS_DEBUG
        int subcarrier_log = subcarrier-subcarrier_offset;
        if(subcarrier_log < 0) {
          subcarrier_log = subcarrier_log + frame_parms->ofdm_symbol_size;
        }
        if(subcarrier_log%12 == 0) {
          LOG_I(NR_PHY,"------------ %d ------------\n", subcarrier_log/12);
        }
        LOG_I(NR_PHY, "(%d)  \t%i\t%i\n", subcarrier_log, r_amp.r, r_amp.i);
#endif

        txdataF[p_index][symbol_offset + l_line_offset + subcarrier] = r_amp;

        // Subcarrier increment
        subcarrier += K_TC;
        if (subcarrier >= frame_parms->ofdm_symbol_size)
          subcarrier -= frame_parms->ofdm_symbol_size;

      } // for (int k = 0; k < M_sc_b_SRS; k++)
    } // for (int l_line = 0; l_line < N_symb_SRS; l_line++)
  } // for (int p_index = 0; p_index < N_ap; p_index++)
  return true;
}
