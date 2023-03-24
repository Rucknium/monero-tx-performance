// Copyright (c) 2022, The Monero Project
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

// NOT FOR PRODUCTION

// Utilities related to enote stores.

#pragma once

//local headers
#include "ringct/rctTypes.h"
#include "seraphis_main/contextual_enote_record_types.h"

//third party headers
#include "boost/multiprecision/cpp_int.hpp"

//standard headers
#include <unordered_set>
#include <vector>

//forward declarations
namespace sp
{
    class CheckpointCache;
    class SpEnoteStore;
    class SpEnoteStorePaymentValidator;
}

namespace sp
{

enum class EnoteStoreBalanceExclusions
{
    LEGACY_FULL,
    LEGACY_INTERMEDIATE,
    SERAPHIS,
    ORIGIN_LEDGER_LOCKED
};

//todo
void update_block_ids_with_new_block_ids(const std::uint64_t first_allowed_index,
    const std::uint64_t first_new_block_index,
    const rct::key &alignment_block_id,
    const std::vector<rct::key> &new_block_ids,
    std::vector<rct::key> &block_ids_inout,
    std::uint64_t &old_top_index_out,
    std::uint64_t &range_start_index_out,
    std::uint64_t &num_blocks_added_out);
void update_block_ids_with_new_block_ids(const std::uint64_t first_new_block_index,
    const rct::key &alignment_block_id,
    const std::vector<rct::key> &new_block_ids,
    CheckpointCache &cache_inout,
    std::uint64_t &old_top_index_out,
    std::uint64_t &range_start_index_out,
    std::uint64_t &num_blocks_added_out);

/// get current balance of an enote store using specified origin/spent statuses and exclusions
boost::multiprecision::uint128_t get_balance(const SpEnoteStore &enote_store,
    const std::unordered_set<SpEnoteOriginStatus> &origin_statuses,
    const std::unordered_set<SpEnoteSpentStatus> &spent_statuses = {},
    const std::unordered_set<EnoteStoreBalanceExclusions> &exclusions = {});

/// get current total amount received using specified origin statuses
boost::multiprecision::uint128_t get_received_sum(const SpEnoteStorePaymentValidator &payment_validator,
    const std::unordered_set<SpEnoteOriginStatus> &origin_statuses,
    const std::unordered_set<EnoteStoreBalanceExclusions> &exclusions = {});

} //namespace sp
