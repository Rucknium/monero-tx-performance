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
#include "enote_store_utils.h"

//local headers
#include "misc_log_ex.h"
#include "seraphis_main/contextual_enote_record_types.h"
#include "seraphis_main/contextual_enote_record_utils.h"
#include "seraphis_main/enote_record_utils_legacy.h"
#include "seraphis_mocks/enote_store_mock_v1.h"
#include "seraphis_mocks/enote_store_mock_validator_v1.h"

//third party headers

//standard headers
#include <ctime>
#include <unordered_map>
#include <unordered_set>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis_mocks"

namespace sp
{
namespace mocks
{
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static boost::multiprecision::uint128_t get_balance_intermediate_legacy(
    // [ legacy identifier : legacy intermediate record ]
    const std::unordered_map<rct::key, LegacyContextualIntermediateEnoteRecordV1> &legacy_intermediate_records,
    // [ Ko : legacy identifier ]
    const std::unordered_map<rct::key, std::unordered_set<rct::key>> &legacy_onetime_address_identifier_map,
    const std::uint64_t top_block_index,
    const std::uint64_t default_spendable_age,
    const std::unordered_set<SpEnoteOriginStatus> &origin_statuses,
    const std::unordered_set<SpEnoteSpentStatus> &spent_statuses,
    const std::unordered_set<EnoteStoreBalanceExclusions> &exclusions)
{
    boost::multiprecision::uint128_t balance{0};

    // 1. ignore if excluded
    if (exclusions.find(EnoteStoreBalanceExclusions::LEGACY_INTERMEDIATE) != exclusions.end())
        return 0;

    // 2. accumulate balance
    // note: it is unknown if enotes in intermediate records are spent
    for (const auto &mapped_contextual_record : legacy_intermediate_records)
    {
        const LegacyContextualIntermediateEnoteRecordV1 &current_contextual_record{mapped_contextual_record.second};

        // a. only include this enote if its origin status is requested
        if (origin_statuses.find(current_contextual_record.origin_context.origin_status) == origin_statuses.end())
            continue;

        // b. ignore onchain enotes that are locked
        if (exclusions.find(EnoteStoreBalanceExclusions::ORIGIN_LEDGER_LOCKED) != exclusions.end() &&
            current_contextual_record.origin_context.origin_status == SpEnoteOriginStatus::ONCHAIN &&
            onchain_legacy_enote_is_locked(
                    current_contextual_record.origin_context.block_index,
                    current_contextual_record.record.unlock_time,
                    top_block_index,
                    default_spendable_age,
                    static_cast<std::uint64_t>(std::time(nullptr)))
                )
            continue;

        // c. ignore enotes that share onetime addresses with other enotes but don't have the highest amount among them
        CHECK_AND_ASSERT_THROW_MES(legacy_onetime_address_identifier_map
                    .find(onetime_address_ref(current_contextual_record.record.enote)) !=
                legacy_onetime_address_identifier_map.end(),
            "enote store get balance (intermediate legacy): tracked legacy duplicates is missing a onetime address (bug).");

        if (!legacy_enote_has_highest_amount_in_set(mapped_contextual_record.first,
                current_contextual_record.record.amount,
                origin_statuses,
                legacy_onetime_address_identifier_map.at(
                    onetime_address_ref(current_contextual_record.record.enote)
                ),
                [&](const rct::key &identifier) -> const SpEnoteOriginStatus&
                {
                    CHECK_AND_ASSERT_THROW_MES(legacy_intermediate_records.find(identifier) !=
                            legacy_intermediate_records.end(),
                        "enote store get balance (intermediate legacy): tracked legacy duplicates has an entry that "
                        "doesn't line up 1:1 with the legacy intermediate map even though it should (bug).");

                    return legacy_intermediate_records
                        .at(identifier)
                        .origin_context
                        .origin_status;
                },
                [&](const rct::key &identifier) -> rct::xmr_amount
                {
                    CHECK_AND_ASSERT_THROW_MES(legacy_intermediate_records.find(identifier) !=
                            legacy_intermediate_records.end(),
                        "enote store get balance (intermediate legacy): tracked legacy duplicates has an entry that "
                        "doesn't line up 1:1 with the legacy intermediate map even though it should (bug).");

                    return legacy_intermediate_records.at(identifier).record.amount;
                }))
            continue;

        // d. update balance
        balance += current_contextual_record.record.amount;
    }

    return balance;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static boost::multiprecision::uint128_t get_balance_full_legacy(
    // [ legacy identifier : legacy record ]
    const std::unordered_map<rct::key, LegacyContextualEnoteRecordV1> &legacy_records,
    // [ Ko : legacy identifier ]
    const std::unordered_map<rct::key, std::unordered_set<rct::key>> &legacy_onetime_address_identifier_map,
    const std::uint64_t top_block_index,
    const std::uint64_t default_spendable_age,
    const std::unordered_set<SpEnoteOriginStatus> &origin_statuses,
    const std::unordered_set<SpEnoteSpentStatus> &spent_statuses,
    const std::unordered_set<EnoteStoreBalanceExclusions> &exclusions)
{
    boost::multiprecision::uint128_t balance{0};

    // 1. ignore if excluded
    if (exclusions.find(EnoteStoreBalanceExclusions::LEGACY_FULL) != exclusions.end())
        return 0;

    // 2. accumulate balance
    for (const auto &mapped_contextual_record : legacy_records)
    {
        const LegacyContextualEnoteRecordV1 &current_contextual_record{mapped_contextual_record.second};

        // a. only include this enote if its origin status is requested
        if (origin_statuses.find(current_contextual_record.origin_context.origin_status) == origin_statuses.end())
            continue;

        // b. if the enote's spent status is requested, then DON'T include this enote
        if (spent_statuses.find(current_contextual_record.spent_context.spent_status) != spent_statuses.end())
            continue;

        // c. ignore onchain enotes that are locked
        if (exclusions.find(EnoteStoreBalanceExclusions::ORIGIN_LEDGER_LOCKED) != exclusions.end() &&
            current_contextual_record.origin_context.origin_status == SpEnoteOriginStatus::ONCHAIN &&
            onchain_legacy_enote_is_locked(
                    current_contextual_record.origin_context.block_index,
                    current_contextual_record.record.unlock_time,
                    top_block_index,
                    default_spendable_age,
                    static_cast<std::uint64_t>(std::time(nullptr)))
                )
            continue;

        // d. ignore enotes that share onetime addresses with other enotes but don't have the highest amount among them
        CHECK_AND_ASSERT_THROW_MES(legacy_onetime_address_identifier_map
                    .find(onetime_address_ref(current_contextual_record.record.enote)) !=
                legacy_onetime_address_identifier_map.end(),
            "enote store get balance (legacy): tracked legacy duplicates is missing a onetime address (bug).");

        if (!legacy_enote_has_highest_amount_in_set(mapped_contextual_record.first,
                current_contextual_record.record.amount,
                origin_statuses,
                legacy_onetime_address_identifier_map.at(
                    onetime_address_ref(current_contextual_record.record.enote)
                ),
                [&](const rct::key &identifier) -> const SpEnoteOriginStatus&
                {
                    CHECK_AND_ASSERT_THROW_MES(legacy_records.find(identifier) != legacy_records.end(),
                        "enote store get balance (legacy): tracked legacy duplicates has an entry that doesn't line up "
                        "1:1 with the legacy map even though it should (bug).");

                    return legacy_records
                        .at(identifier)
                        .origin_context
                        .origin_status;
                },
                [&](const rct::key &identifier) -> rct::xmr_amount
                {
                    CHECK_AND_ASSERT_THROW_MES(legacy_records.find(identifier) !=  legacy_records.end(),
                        "enote store get balance (legacy): tracked legacy duplicates has an entry that doesn't line up "
                        "1:1 with the legacy map even though it should (bug).");

                    return legacy_records.at(identifier).record.amount;
                }))
            continue;

        // e. update balance
        balance += current_contextual_record.record.amount;
    }

    return balance;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static boost::multiprecision::uint128_t get_balance_seraphis(
    const std::unordered_map<crypto::key_image, SpContextualEnoteRecordV1> &sp_records,
    const std::uint64_t top_block_index,
    const std::uint64_t default_spendable_age,
    const std::unordered_set<SpEnoteOriginStatus> &origin_statuses,
    const std::unordered_set<SpEnoteSpentStatus> &spent_statuses,
    const std::unordered_set<EnoteStoreBalanceExclusions> &exclusions)
{
    boost::multiprecision::uint128_t balance{0};

    // 1. ignore if excluded
    if (exclusions.find(EnoteStoreBalanceExclusions::SERAPHIS) != exclusions.end())
        return 0;

    // 2. accumulate balance
    for (const auto &mapped_contextual_record : sp_records)
    {
        const SpContextualEnoteRecordV1 &current_contextual_record{mapped_contextual_record.second};

        // a. only include this enote if its origin status is requested
        if (origin_statuses.find(current_contextual_record.origin_context.origin_status) == origin_statuses.end())
            continue;

        // b. if the enote's spent status is requested, then DON'T include this enote
        if (spent_statuses.find(current_contextual_record.spent_context.spent_status) != spent_statuses.end())
            continue;

        // c. ignore onchain enotes that are locked
        if (exclusions.find(EnoteStoreBalanceExclusions::ORIGIN_LEDGER_LOCKED) != exclusions.end() &&
            current_contextual_record.origin_context.origin_status == SpEnoteOriginStatus::ONCHAIN &&
            onchain_sp_enote_is_locked(
                    current_contextual_record.origin_context.block_index,
                    top_block_index,
                    default_spendable_age
                ))
            continue;

        // d. update balance
        balance += current_contextual_record.record.amount;
    }

    return balance;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
boost::multiprecision::uint128_t get_balance(const SpEnoteStoreMockV1 &enote_store,
    const std::unordered_set<SpEnoteOriginStatus> &origin_statuses,
    const std::unordered_set<SpEnoteSpentStatus> &spent_statuses,
    const std::unordered_set<EnoteStoreBalanceExclusions> &exclusions)
{
    boost::multiprecision::uint128_t balance{0};

    // 1. intermediate legacy enotes (it is unknown if these enotes are spent)
    balance += get_balance_intermediate_legacy(enote_store.legacy_intermediate_records(),
        enote_store.legacy_onetime_address_identifier_map(),
        enote_store.top_block_index(),
        enote_store.default_spendable_age(),
        origin_statuses,
        spent_statuses,
        exclusions);

    // 2. full legacy enotes
    balance += get_balance_full_legacy(enote_store.legacy_records(),
        enote_store.legacy_onetime_address_identifier_map(),
        enote_store.top_block_index(),
        enote_store.default_spendable_age(),
        origin_statuses,
        spent_statuses,
        exclusions);

    // 3. seraphis enotes
    balance += get_balance_seraphis(enote_store.sp_records(),
        enote_store.top_block_index(),
        enote_store.default_spendable_age(),
        origin_statuses,
        spent_statuses,
        exclusions);

    return balance;
}
//-------------------------------------------------------------------------------------------------------------------
boost::multiprecision::uint128_t get_received_sum(const SpEnoteStoreMockPaymentValidatorV1 &payment_validator,
    const std::unordered_set<SpEnoteOriginStatus> &origin_statuses,
    const std::unordered_set<EnoteStoreBalanceExclusions> &exclusions)
{
    boost::multiprecision::uint128_t received_sum{0};

    for (const auto &mapped_contextual_record : payment_validator.sp_intermediate_records())
    {
        const SpContextualIntermediateEnoteRecordV1 &contextual_record{mapped_contextual_record.second};

        // ignore enotes with unrequested origins
        if (origin_statuses.find(contextual_record.origin_context.origin_status) == origin_statuses.end())
            continue;

        // ignore onchain enotes that are locked
        if (exclusions.find(EnoteStoreBalanceExclusions::ORIGIN_LEDGER_LOCKED) != exclusions.end() &&
            contextual_record.origin_context.origin_status == SpEnoteOriginStatus::ONCHAIN &&
            onchain_sp_enote_is_locked(
                    contextual_record.origin_context.block_index,
                    payment_validator.top_block_index(),
                    payment_validator.default_spendable_age()
                ))
            continue;

        // update received sum
        received_sum += contextual_record.record.amount;
    }

    return received_sum;
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace mocks
} //namespace sp
