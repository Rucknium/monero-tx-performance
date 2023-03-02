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

//paired header
#include "enote_scanning.h"

//local headers
#include "contextual_enote_record_types.h"
#include "enote_finding_context.h"
#include "enote_scanning_context.h"
#include "enote_store_updater.h"
#include "scan_state_machine.h"
#include "ringct/rctTypes.h"

//third party headers

//standard headers
#include <list>
#include <unordered_map>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis"

namespace sp
{
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static void check_enote_scan_chunk_map_semantics_v1(
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images,
    const SpEnoteOriginStatus expected_origin_status,
    const SpEnoteSpentStatus expected_spent_status)
{
    // 1. check contextual basic records
    for (const auto &tx_basic_records : chunk_basic_records_per_tx)
    {
        for (const ContextualBasicRecordVariant &contextual_basic_record : tx_basic_records.second)
        {
            CHECK_AND_ASSERT_THROW_MES(origin_context_ref(contextual_basic_record).origin_status ==
                    expected_origin_status,
                "enote chunk semantics check: contextual basic record doesn't have expected origin status.");
            CHECK_AND_ASSERT_THROW_MES(origin_context_ref(contextual_basic_record).transaction_id ==
                    tx_basic_records.first,
                "enote chunk semantics check: contextual basic record doesn't have origin tx id matching mapped id.");
        }
    }

    // 2. check contextual key images
    for (const auto &contextual_key_image_set : chunk_contextual_key_images)
    {
        CHECK_AND_ASSERT_THROW_MES(contextual_key_image_set.spent_context.spent_status == expected_spent_status,
            "enote chunk semantics check: contextual key image doesn't have expected spent status.");

        // notes:
        // - in seraphis tx building, tx authors must always put a selfsend output enote in their txs; during balance
        //   recovery, the view tag check will pass for those selfsend enotes; this means to identify if your enotes are
        //   spent, you only need to look at key images in txs with view tag matches
        // - in support of that expectation, we enforce that the key images in a scanning chunk must come from txs
        //   recorded in the 'basic records per tx' map, which will contain only owned enote candidates (in seraphis
        //   scanning, that's all the enotes that passed the view tag check)
        // - if you want to include key images from txs that have no owned enote candidates, then you must add empty
        //   entries to the 'basic records per tx' map for those txs
        //   - when doing legacy scanning, you need to include all key images from the chain since legacy tx construction
        //     does/did not require all txs to have a self-send output
        CHECK_AND_ASSERT_THROW_MES(
                chunk_basic_records_per_tx.find(contextual_key_image_set.spent_context.transaction_id) !=
                chunk_basic_records_per_tx.end(),
            "enote chunk semantics check: contextual key image transaction id is not mirrored in basic records map.");
    }
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
void check_v1_enote_scan_chunk_nonledger_semantics_v1(const EnoteScanningChunkNonLedgerV1 &nonledger_chunk,
    const SpEnoteOriginStatus expected_origin_status,
    const SpEnoteSpentStatus expected_spent_status)
{
    check_enote_scan_chunk_map_semantics_v1(nonledger_chunk.basic_records_per_tx,
        nonledger_chunk.contextual_key_images,
        expected_origin_status,
        expected_spent_status);
}
//-------------------------------------------------------------------------------------------------------------------
void check_v1_enote_scan_chunk_ledger_semantics_v1(const EnoteScanningChunkLedgerV1 &onchain_chunk,
    const std::uint64_t expected_prefix_index)
{
    // 1. misc. checks
    CHECK_AND_ASSERT_THROW_MES(onchain_chunk.context.start_index - 1 == expected_prefix_index,
        "enote scan chunk semantics check (ledger): chunk range doesn't start at expected prefix index.");

    const std::uint64_t num_blocks_in_chunk{onchain_chunk.context.element_ids.size()};
    CHECK_AND_ASSERT_THROW_MES(num_blocks_in_chunk >= 1,
        "enote scan chunk semantics check (ledger): chunk has no blocks.");    

    check_enote_scan_chunk_map_semantics_v1(onchain_chunk.basic_records_per_tx,
        onchain_chunk.contextual_key_images,
        SpEnoteOriginStatus::ONCHAIN,
        SpEnoteSpentStatus::SPENT_ONCHAIN);

    // 2. get start and end block indices
    // - start block = prefix block + 1
    const std::uint64_t allowed_lowest_index{onchain_chunk.context.start_index};
    // - end block
    const std::uint64_t allowed_heighest_index{onchain_chunk.context.start_index + num_blocks_in_chunk - 1};

    // 3. contextual basic records: index checks
    for (const auto &tx_basic_records : onchain_chunk.basic_records_per_tx)
    {
        for (const ContextualBasicRecordVariant &contextual_basic_record : tx_basic_records.second)
        {
            CHECK_AND_ASSERT_THROW_MES(origin_context_ref(contextual_basic_record).block_index ==
                    origin_context_ref(*tx_basic_records.second.begin()).block_index,
                "enote chunk semantics check (ledger): contextual record tx index doesn't match other records in tx.");

            CHECK_AND_ASSERT_THROW_MES(
                    origin_context_ref(contextual_basic_record).block_index >= allowed_lowest_index &&
                    origin_context_ref(contextual_basic_record).block_index <= allowed_heighest_index,
                "enote chunk semantics check (ledger): contextual record block index is out of the expected range.");
        }
    }

    // 4. contextual key images: index checks
    for (const SpContextualKeyImageSetV1 &contextual_key_image_set : onchain_chunk.contextual_key_images)
    {
        CHECK_AND_ASSERT_THROW_MES(
                contextual_key_image_set.spent_context.block_index >= allowed_lowest_index &&
                contextual_key_image_set.spent_context.block_index <= allowed_heighest_index,
            "enote chunk semantics check (ledger): contextual key image block index is out of the expected range.");
    }
}
//-------------------------------------------------------------------------------------------------------------------
bool chunk_is_empty(const EnoteScanningChunkNonLedgerV1 &chunk)
{
    return chunk.basic_records_per_tx.size() == 0 &&
        chunk.contextual_key_images.size() == 0;
}
//-------------------------------------------------------------------------------------------------------------------
bool chunk_is_empty(const EnoteScanningChunkLedgerV1 &chunk)
{
    return chunk.context.element_ids.size() == 0;
}
//-------------------------------------------------------------------------------------------------------------------
bool refresh_enote_store_nonledger(const SpEnoteOriginStatus expected_origin_status,
    const SpEnoteSpentStatus expected_spent_status,
    EnoteScanningContextNonLedger &scanning_context_inout,
    EnoteStoreUpdater &enote_store_updater_inout)
{
    try
    {
        // 1. get the scan chunk
        EnoteScanningChunkNonLedgerV1 nonledger_chunk;
        scanning_context_inout.get_nonledger_chunk(nonledger_chunk);

        check_v1_enote_scan_chunk_nonledger_semantics_v1(nonledger_chunk,
            expected_origin_status,
            expected_spent_status);

        // 2. check if the scan context was aborted
        // - always consume non-empty chunks (it's possible for a scan context to be aborted after acquiring a chunk)
        // - don't consume empty chunks when aborted because they may not represent the real state of the nonledger
        //   cache
        if (chunk_is_empty(nonledger_chunk) && scanning_context_inout.is_aborted())
            return false;

        // 3. consume the chunk
        enote_store_updater_inout.consume_nonledger_chunk(expected_origin_status,
            nonledger_chunk.basic_records_per_tx,
            nonledger_chunk.contextual_key_images);
    }
    catch (...)
    {
        LOG_ERROR("refresh enote store nonledger failed.");
        return false;
    }

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
bool refresh_enote_store_ledger(const RefreshLedgerEnoteStoreConfig &config,
    EnoteScanningContextNonLedger &nonledger_scanning_context_inout,
    EnoteScanningContextLedger &ledger_scanning_context_inout,
    EnoteStoreUpdater &enote_store_updater_inout)
{
    //translate config types (todo: only use one config type)
    const scan_machine::ScanConfig scan_config{
            .reorg_avoidance_increment = config.reorg_avoidance_depth,
            .max_chunk_size            = config.max_chunk_size,
            .max_partialscan_attempts  = config.max_partialscan_attempts
        };

    // 1. perform a full scan
    scan_machine::ScanMetadata initial_fullscan_metadata{
            .config = scan_config,
            .status = scan_machine::ScanStatus::NEED_FULLSCAN
        };
    while (scan_machine::try_advance_state_machine(initial_fullscan_metadata,
        ledger_scanning_context_inout,
        enote_store_updater_inout))
    {}

    if (initial_fullscan_metadata.status != scan_machine::ScanStatus::SUCCESS)
        return false;

    // 2. try to perform an unconfirmed scan
    if (!refresh_enote_store_nonledger(SpEnoteOriginStatus::UNCONFIRMED,
            SpEnoteSpentStatus::SPENT_UNCONFIRMED,
            nonledger_scanning_context_inout,
            enote_store_updater_inout))
        return false;

    // 3. perform a follow-up full scan
    // rationale:
    // - blocks may have been added between the initial on-chain pass and the unconfirmed pass, and those blocks may
    //   contain txs not seen by the unconfirmed pass (i.e. sneaky txs)
    // - we want scan results to be chronologically contiguous (it is better for the unconfirmed scan results to be stale
    //   than the on-chain scan results)
    scan_machine::ScanMetadata followup_fullscan_metadata{
            .config = scan_config,
            .status = scan_machine::ScanStatus::NEED_FULLSCAN
        };
    while (scan_machine::try_advance_state_machine(followup_fullscan_metadata,
        ledger_scanning_context_inout,
        enote_store_updater_inout))
    {}

    if (followup_fullscan_metadata.status != scan_machine::ScanStatus::SUCCESS)
        return false;

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace sp
