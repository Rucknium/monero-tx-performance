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
#include "scan_misc_utils.h"

//local headers
#include "misc_log_ex.h"
#include "seraphis_main/scan_core_types.h"
#include "seraphis_main/scan_ledger_chunk.h"
#include "seraphis_main/scan_machine_types.h"
#include "seraphis_main/scan_misc_utils.h"

//third party headers

//standard headers

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis"

namespace sp
{
namespace scanning
{
//-------------------------------------------------------------------------------------------------------------------
bool chunk_is_empty(const ChunkData &chunk_data)
{
    return chunk_data.basic_records_per_tx.size() == 0 &&
        chunk_data.contextual_key_images.size() == 0;
}
//-------------------------------------------------------------------------------------------------------------------
bool chunk_is_empty(const ChunkContext &chunk_context)
{
    return chunk_context.block_ids.size() == 0;
}
//-------------------------------------------------------------------------------------------------------------------
bool chunk_is_empty(const LedgerChunk &chunk)
{
    if (!chunk_is_empty(chunk.get_context()))
        return false;
    CHECK_AND_ASSERT_THROW_MES(chunk_is_empty(chunk.get_data()),
        "scan machine chunk: context indicates an empty chunk but the data is not empty.");

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
void check_chunk_data_semantics_v1(const ChunkData &chunk_data,
    const SpEnoteOriginStatus expected_origin_status,
    const SpEnoteSpentStatus expected_spent_status,
    const std::uint64_t allowed_lowest_index,
    const std::uint64_t allowed_heighest_index)
{
    // 1. check contextual basic records
    for (const auto &tx_basic_records : chunk_data.basic_records_per_tx)
    {
        for (const ContextualBasicRecordVariant &contextual_basic_record : tx_basic_records.second)
        {
            CHECK_AND_ASSERT_THROW_MES(origin_context_ref(contextual_basic_record).origin_status ==
                    expected_origin_status,
                "scan chunk data semantics check: contextual basic record doesn't have expected origin status.");
            CHECK_AND_ASSERT_THROW_MES(origin_context_ref(contextual_basic_record).transaction_id ==
                    tx_basic_records.first,
                "scan chunk data semantics check: contextual basic record doesn't have origin tx id matching mapped id.");
            CHECK_AND_ASSERT_THROW_MES(origin_context_ref(contextual_basic_record).block_index ==
                    origin_context_ref(*tx_basic_records.second.begin()).block_index,
                "scan chunk data semantics check: contextual record tx index doesn't match other records in tx.");

            CHECK_AND_ASSERT_THROW_MES(
                    origin_context_ref(contextual_basic_record).block_index >= allowed_lowest_index &&
                    origin_context_ref(contextual_basic_record).block_index <= allowed_heighest_index,
                "scan chunk data semantics check: contextual record block index is out of the expected range.");
        }
    }

    // 2. check contextual key images
    for (const auto &contextual_key_image_set : chunk_data.contextual_key_images)
    {
        CHECK_AND_ASSERT_THROW_MES(contextual_key_image_set.spent_context.spent_status == expected_spent_status,
            "scan chunk data semantics check: contextual key image doesn't have expected spent status.");

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
                chunk_data.basic_records_per_tx.find(contextual_key_image_set.spent_context.transaction_id) !=
                chunk_data.basic_records_per_tx.end(),
            "scan chunk data semantics check: contextual key image transaction id is not mirrored in basic records map.");

        CHECK_AND_ASSERT_THROW_MES(
                contextual_key_image_set.spent_context.block_index >= allowed_lowest_index &&
                contextual_key_image_set.spent_context.block_index <= allowed_heighest_index,
            "scan chunk data semantics check: contextual key image block index is out of the expected range.");
    }
}
//-------------------------------------------------------------------------------------------------------------------
void check_ledger_chunk_semantics_v1(const ChunkContext &chunk_context,
    const ChunkData &chunk_data,
    const std::uint64_t expected_prefix_index)
{
    // 1. check context semantics
    CHECK_AND_ASSERT_THROW_MES(chunk_context.start_index - 1 == expected_prefix_index,
        "scan machine chunk semantics check: chunk range doesn't start at expected prefix index.");

    const std::uint64_t num_blocks_in_chunk{chunk_context.block_ids.size()};
    CHECK_AND_ASSERT_THROW_MES(num_blocks_in_chunk >= 1,
        "scan machine chunk semantics check: chunk has no blocks.");    

    // 2. get start and end block indices
    // - start block = prefix block + 1
    const std::uint64_t allowed_lowest_index{chunk_context.start_index};
    // - end block
    const std::uint64_t allowed_heighest_index{chunk_context.start_index + num_blocks_in_chunk - 1};

    // 3. check data semantics
    check_chunk_data_semantics_v1(chunk_data,
        SpEnoteOriginStatus::ONCHAIN,
        SpEnoteSpentStatus::SPENT_ONCHAIN,
        allowed_lowest_index,
        allowed_heighest_index);
}
//-------------------------------------------------------------------------------------------------------------------
void check_ledger_chunk_semantics_v1(const LedgerChunk &onchain_chunk, const std::uint64_t expected_prefix_index)
{
    check_ledger_chunk_semantics_v1(onchain_chunk.get_context(), onchain_chunk.get_data(), expected_prefix_index);
}
//-------------------------------------------------------------------------------------------------------------------
void initialize_scan_machine_metadata(const ScanMachineConfig &scan_config, ScanMachineMetadata &metadata_out)
{
    metadata_out = ScanMachineMetadata{
            .config                = scan_config,
            .status                = ScanMachineStatus::NEED_FULLSCAN,
            .partialscan_attempts  = 0,
            .fullscan_attempts     = 0,
            .contiguity_marker     = {},
            .first_contiguity_index = static_cast<std::uint64_t>(-1)
        };
}
//-------------------------------------------------------------------------------------------------------------------
bool is_terminal_state(const ScanMachineStatus status)
{
    // 1. non-terminal states
    switch (status)
    {
        case ScanMachineStatus::NEED_FULLSCAN    :
        case ScanMachineStatus::NEED_PARTIALSCAN :
        case ScanMachineStatus::START_SCAN       :
        case ScanMachineStatus::DO_SCAN          :
            return false;
        default:;
    }

    // 2. terminal states: everything else
    return true;
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace scanning
} //namespace sp
