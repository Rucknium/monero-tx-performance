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

// Enote store for a seraphis 'payment validator' that can read the amounts and destinations of incoming normal enotes.

#pragma once

//local headers
#include "crypto/crypto.h"
#include "enote_store_event_types.h"
#include "enote_store_mock_v1.h"
#include "seraphis_main/contextual_enote_record_types.h"

//third party headers
#include "boost/multiprecision/cpp_int.hpp"

//standard headers
#include <unordered_map>
#include <unordered_set>

//forward declarations


namespace sp
{
namespace mocks
{

////
// SpEnoteStoreMockPaymentValidatorV1
// - tracks amounts and destinations non-selfsend seraphis enotes
///
class SpEnoteStoreMockPaymentValidatorV1 final
{
public:
//constructors
    /// default constructor
    SpEnoteStoreMockPaymentValidatorV1() = default;

    /// normal constructor
    SpEnoteStoreMockPaymentValidatorV1(const std::uint64_t refresh_index, const std::uint64_t default_spendable_age) :
        m_refresh_index{refresh_index},
        m_default_spendable_age{default_spendable_age}
    {}

//member functions
    /// get current total amount received using specified origin statuses
    boost::multiprecision::uint128_t get_received_sum(const std::unordered_set<SpEnoteOriginStatus> &origin_statuses,
        const std::unordered_set<EnoteStoreBalanceUpdateExclusions> &exclusions = {}) const;
    /// get index of first block the enote store cares about
    std::uint64_t refresh_index() const { return m_refresh_index; }
    /// get index of heighest recorded block (refresh index - 1 if no recorded blocks) (heighest block PayVal-scanned)
    std::uint64_t top_block_index() const { return m_refresh_index + m_block_ids.size() - 1; }
    /// try to get the recorded block id for a given index
    bool try_get_block_id(const std::uint64_t block_index, rct::key &block_id_out) const;

    /// update the store with enote records, with associated context
    void update_with_sp_records_from_nonledger(const SpEnoteOriginStatus nonledger_origin_status,
        const std::unordered_map<rct::key, SpContextualIntermediateEnoteRecordV1> &found_enote_records,
        std::list<SpPaymentValidatorStoreEvent> &events_inout);
    void update_with_sp_records_from_ledger(const std::uint64_t first_new_block,
        const rct::key &alignment_block_id,
        const std::unordered_map<rct::key, SpContextualIntermediateEnoteRecordV1> &found_enote_records,
        const std::vector<rct::key> &new_block_ids,
        std::list<SpPaymentValidatorStoreEvent> &events_inout);

private:
    /// add a record
    void add_record(const SpContextualIntermediateEnoteRecordV1 &new_record,
        std::list<SpPaymentValidatorStoreEvent> &events_inout);

//member variables
protected:
    /// seraphis enotes
    std::unordered_map<rct::key, SpContextualIntermediateEnoteRecordV1> m_sp_contextual_enote_records;

    /// refresh index
    std::uint64_t m_refresh_index{0};
    /// stored block ids in range [refresh index, end of known chain]
    std::vector<rct::key> m_block_ids;

    /// configuration value: default spendable age; an enote is considered 'spendable' in the next block if it's on-chain
    //      and the hext index is >= 'origin index + max(1, default_spendable_age)'
    std::uint64_t m_default_spendable_age{0};
};

} //namespace mocks
} //namespace sp
