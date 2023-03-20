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

//paired header
#include "enote_store_payment_validator.h"

//local headers
#include "common/container_helpers.h"
#include "misc_log_ex.h"
#include "seraphis_impl/enote_store.h"
#include "seraphis_impl/enote_store_event_types.h"
#include "seraphis_impl/enote_store_utils.h"
#include "seraphis_main/contextual_enote_record_types.h"
#include "seraphis_main/contextual_enote_record_utils.h"

//third party headers

//standard headers
#include <algorithm>
#include <ctime>
#include <functional>
#include <iterator>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis_impl"

namespace sp
{
//-------------------------------------------------------------------------------------------------------------------
bool SpEnoteStorePaymentValidator::try_get_block_id(const std::uint64_t block_index, rct::key &block_id_out) const
{
    if (block_index < m_refresh_index ||
        block_index > m_refresh_index + m_block_ids.size() - 1 ||
        m_block_ids.size() == 0)
        return false;

    block_id_out = m_block_ids[block_index - m_refresh_index];

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
void SpEnoteStorePaymentValidator::update_with_sp_records_from_nonledger(
    const SpEnoteOriginStatus nonledger_origin_status,
    const std::unordered_map<rct::key, SpContextualIntermediateEnoteRecordV1> &found_enote_records,
    std::list<PaymentValidatorStoreEvent> &events_inout)
{
    CHECK_AND_ASSERT_THROW_MES(nonledger_origin_status == SpEnoteOriginStatus::OFFCHAIN ||
            nonledger_origin_status == SpEnoteOriginStatus::UNCONFIRMED,
        "sp payment validator (clean maps for sp nonledger update): invalid origin status.");

    // 1. remove records that will be replaced
    tools::for_all_in_map_erase_if(m_sp_contextual_enote_records,
            [&](const auto &mapped_contextual_enote_record) -> bool
            {
                // ignore enotes that don't have our specified origin
                if (mapped_contextual_enote_record.second.origin_context.origin_status != nonledger_origin_status)
                    return false;

                events_inout.emplace_back(RemovedSpIntermediateRecord{mapped_contextual_enote_record.first});

                return true;
            }
        );

    // 2. add found enotes
    for (const auto &found_enote_record : found_enote_records)
        this->add_record(found_enote_record.second, events_inout);
}
//-------------------------------------------------------------------------------------------------------------------
void SpEnoteStorePaymentValidator::update_with_sp_records_from_ledger(const std::uint64_t first_new_block,
    const rct::key &alignment_block_id,
    const std::unordered_map<rct::key, SpContextualIntermediateEnoteRecordV1> &found_enote_records,
    const std::vector<rct::key> &new_block_ids,
    std::list<PaymentValidatorStoreEvent> &events_inout)
{
    // 1. set new block ids in range [first_new_block, end of chain]
    SpIntermediateBlocksDiff diff{};
    update_block_ids_with_new_block_ids(m_refresh_index,
        first_new_block,
        alignment_block_id,
        new_block_ids,
        m_block_ids,
        diff.old_top_index,
        diff.range_start_index,
        diff.num_blocks_added);
    events_inout.emplace_back(diff);

    // 2. remove records that will be replaced
    tools::for_all_in_map_erase_if(m_sp_contextual_enote_records,
            [&](const auto &mapped_contextual_enote_record) -> bool
            {
                // a. remove onchain enotes in range [first_new_block, end of chain]
                if (mapped_contextual_enote_record.second.origin_context.origin_status ==
                        SpEnoteOriginStatus::ONCHAIN &&
                    mapped_contextual_enote_record.second.origin_context.block_index >= first_new_block)
                {
                    events_inout.emplace_back(RemovedSpIntermediateRecord{mapped_contextual_enote_record.first});
                    return true;
                }

                // b. remove all unconfirmed enotes
                if (mapped_contextual_enote_record.second.origin_context.origin_status ==
                        SpEnoteOriginStatus::UNCONFIRMED)
                {
                    events_inout.emplace_back(RemovedSpIntermediateRecord{mapped_contextual_enote_record.first});
                    return true;
                }

                return false;
            }
        );

    // 3. add found enotes
    for (const auto &found_enote_record : found_enote_records)
        this->add_record(found_enote_record.second, events_inout);
}
//-------------------------------------------------------------------------------------------------------------------
// PAYMENT VALIDATOR INTERNAL
//-------------------------------------------------------------------------------------------------------------------
void SpEnoteStorePaymentValidator::add_record(const SpContextualIntermediateEnoteRecordV1 &new_record,
    std::list<PaymentValidatorStoreEvent> &events_inout)
{
    const rct::key record_onetime_address{onetime_address_ref(new_record)};

    // add the record or update an existing record's origin context
    if (m_sp_contextual_enote_records.find(record_onetime_address) == m_sp_contextual_enote_records.end())
    {
        m_sp_contextual_enote_records[record_onetime_address] = new_record;
        events_inout.emplace_back(NewSpIntermediateRecord{record_onetime_address});
    }
    else
    {
        if (try_update_enote_origin_context_v1(new_record.origin_context,
                m_sp_contextual_enote_records[record_onetime_address].origin_context))
            events_inout.emplace_back(UpdatedSpIntermediateOriginContext{record_onetime_address});
    }
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace sp
