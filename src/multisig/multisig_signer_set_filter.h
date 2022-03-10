// Copyright (c) 2021, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "cryptonote_config.h"
#include "ringct/rctTypes.h"

#include <cstdint>
#include <vector>


namespace multisig
{
  /**
  * multisig signer set filter
  * 
  * - a set of multisig signers, represented as bit flags that correspond 1:1 with a list of sorted signer ids
  * - note: must rework implementation if max signers increases
  */
  using signer_set_filter = std::uint16_t;
  static_assert(8*sizeof(signer_set_filter) == config::MULTISIG_MAX_SIGNERS, "");

  /**
  * brief: validate_multisig_signer_set_filter - Check that a signer set is valid.
  *   - Only possible signers are flagged.
  *   - Only 'threshold' number of signers are flagged.
  * param: num_signers - number of participants in multisig (N)
  * param: threshold - threshold of multisig (M)
  * param: filter - a set of multisig signers to test validity of
  * return: true/false on validation result
  */
  bool validate_multisig_signer_set_filter(const std::uint32_t num_signers,
    const std::uint32_t threshold,
    const signer_set_filter filter);
  bool validate_multisig_signer_set_filters(const std::uint32_t num_signers,
    const std::uint32_t threshold,
    const std::vector<signer_set_filter> &filters);
  /**
  * brief: aggregate_multisig_signer_set_filter_to_permutations - Extract filters from an aggregate filter.
  *   - An aggregate filter is bitwise-or between all contained filters.
  * param: num_signers - total number of signers the filter acts on
  * param: threshold - number of signers a filter can represent
  * param: aggregate_filter - signer set filter contains 1 or more actual filters
  * outparam: filter_permutations_out - all the filters that can be extracted from the aggregate filter
  */
  void aggregate_multisig_signer_set_filter_to_permutations(const std::uint32_t num_signers,
    const std::uint32_t threshold,
    const signer_set_filter aggregate_filter,
    std::vector<signer_set_filter> &filter_permutations_out);
  /**
  * brief: allowed_multisig_signers_to_aggregate_filter - Represent a set of multisig signers as an aggregate filter.
  *   - Every permutation of 'threshold' number of signers from the allowed set is a separate signer set that can
  *     collaborate on a multisig signature. Dis-aggregating the aggregate filter will provide filters corresponding
  *     to each of those sets.
  * param: signer_list - list of signer ids
  * param: allowed_signers - the signers from the signer list that should be represented in the filter
  * param: threshold - number of signers a filter can represent
  * outparam: aggregate_filter_out - an aggregate filter that maps the signer list to the allowed signer list
  */
  void allowed_multisig_signers_to_aggregate_filter(const std::vector<rct::key> &signer_list,
    const std::vector<rct::key> &allowed_signers,
    const std::uint32_t threshold,
    signer_set_filter &aggregate_filter_out);
    /**
  * brief: get_filtered_multisig_signers - Filter a signer list using a signer_set_filter.
  * param: signer_list - list of signer ids
  * param: threshold - number of signers a filter can represent
  * param: filter - signer set filter
  * outparam: filtered_signers_out - a filtered set of multisig signer ids
  */
  void get_filtered_multisig_signers(const std::vector<rct::key> &signer_list,
    const std::uint32_t threshold,
    const signer_set_filter filter,
    std::vector<rct::key> &filtered_signers_out);
} //namespace multisig
