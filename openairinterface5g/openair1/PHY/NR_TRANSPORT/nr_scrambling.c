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

#include "nr_transport_common_proto.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#define DEBUG_SCRAMBLING(a)
//#define DEBUG_SCRAMBLING(a) a
void nr_codeword_scrambling(uint8_t *in,
                            uint32_t size,
                            uint8_t q,
                            uint32_t Nid,
                            uint32_t n_RNTI,
                            uint32_t* out)
{
  const int roundedSz = (size + 31) / 32;
  uint32_t *seq = gold_cache((n_RNTI << 15) + (q << 14) + Nid, roundedSz);
  unsigned int i_32 = 0;
#if defined(__AVX512F__) && defined(__AVX512VL__)
  for (; i_32 < ((roundedSz >> 3) << 3); i_32 += 8) {
    __m256i in_256 = _mm256_load_epi32(&((uint32_t *)in)[i_32]);
    __m256i seq_256 = _mm256_load_epi32(&seq[i_32]);
    _mm256_storeu_epi32(&out[i_32], _mm256_xor_si256(in_256, seq_256));
  }
#endif
#if defined(__aarch64__)
  for (; i_32 < ((roundedSz >> 2) << 2); i_32 += 4) {
    unsigned int i = i_32 >> 2;
    uint32x4_t in_32x4 = vld1q_u32(&((uint32_t *)in)[i_32]);
    uint32x4_t seq_32x4 = vld1q_u32(&seq[i_32]);
    ((uint32x4_t *)out)[i] = veorq_u32(in_32x4, seq_32x4);
  }
#endif
  for (; i_32 < roundedSz; i_32++) { 
    unsigned int i = i_32;
    out[i] = ((uint32_t *)in)[i] ^ seq[i];
    DEBUG_SCRAMBLING(LOG_D(PHY, "in[%d] %x => %x\n", i, ((uint32_t*)in)[i], out[i]));
  }
}

void nr_codeword_unscrambling(int16_t* llr, uint32_t size, uint8_t q, uint32_t Nid, uint32_t n_RNTI)
{
  const int roundedSz = (size + 31) / 32;
  uint32_t *seq = gold_cache((n_RNTI << 15) + (q << 14) + Nid, roundedSz);
#if defined(__x86_64__) || defined(__i386__) || defined(__arm__) || defined(__aarch64__)
  simde__m128i *llr128 = (simde__m128i*)llr;
  for (int i = 0, j = 0; i < roundedSz; i++, j += 4) {
    uint8_t *s8 = (uint8_t *)(seq + i);
    llr128[j]   = simde_mm_mullo_epi16(llr128[j],byte2m128i[s8[0]]);
    llr128[j+1] = simde_mm_mullo_epi16(llr128[j+1],byte2m128i[s8[1]]);
    llr128[j+2] = simde_mm_mullo_epi16(llr128[j+2],byte2m128i[s8[2]]);
    llr128[j + 3] = simde_mm_mullo_epi16(llr128[j + 3], byte2m128i[s8[3]]);
  }
#else
  for (uint32_t i=0; i<size; i++) {
    if (seq[i / 32] & (1U << (i % 32)))
      llr[i] = -llr[i];
  }
#endif
}

void nr_codeword_unscrambling_init(int16_t *s2, uint32_t size, uint8_t q, uint32_t Nid, uint32_t n_RNTI)
{
  const int roundedSz = (size + 31) / 32;
  uint32_t *seq = gold_cache((n_RNTI << 15) + (q << 14) + Nid, roundedSz);
  simde__m128i *s128=(simde__m128i *)s2;
  for (int i = 0; i < roundedSz; i++) {
    uint8_t *s8 = (uint8_t *)(seq + i);
    *s128++ = byte2m128i[s8[0]];
    *s128++ = byte2m128i[s8[1]];
    *s128++ = byte2m128i[s8[2]];
    *s128++ = byte2m128i[s8[3]];
  }
}
