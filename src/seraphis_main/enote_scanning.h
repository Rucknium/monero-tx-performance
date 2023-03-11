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

// Interface for robust balance recovery framework (works for both legacy and seraphis backends).
// PRECONDITIONS:
// 1. chunks must be built from an atomic view of the source cache (ledger, unconfirmed cache, offchain cache)
// 2. per chunk: contextual_key_images must reference a tx recorded in basic_records_per_tx (even if you
//    need to add empty map entries to achieve that)
// 3. any call to get a chunk from an enote scanning context should produce a chunk that is at least as fresh as any
//    other chunk obtained from that context (atomic ordering)
// 4. any call to consume a chunk in an enote store updater should resolve all side-effects observable via the updater
//    interface by the time the call is complete (e.g. any changes to block ids observable by try_get_block_id() need
//    to be completed during the 'consume chunk' call)

#pragma once

//local headers
#include "contextual_enote_record_types.h"
#include "ringct/rctTypes.h"
#include "scan_machine.h"

//third party headers

//standard headers
#include <list>
#include <unordered_map>
#include <vector>

//forward declarations
namespace sp
{
    class EnoteScanningContextNonLedger;
    class EnoteScanningContextLedger;
    class EnoteStoreUpdater;
}

namespace sp
{

////
// EnoteScanningChunkNonLedgerV1
// - contextual basic enote records for owned enote candidates in a non-ledger context (at a single point in time)
// - key images from all txs with owned enote candidates
///
struct EnoteScanningChunkNonLedgerV1 final
{
    /// owned enote candidates in a non-ledger context (mapped to tx id)
    std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> basic_records_per_tx;
    /// key images from txs with owned enote candidates in the non-ledger context
    std::list<SpContextualKeyImageSetV1> contextual_key_images;
};

////
// EnoteScanningChunkLedgerV1
// - chunk context: tracks where this chunk exists on-chain
// - contextual basic enote records for owned enote candidates in the chunk of blocks
// - key images from each of the txs recorded in the basic records map
//   - add empty entries to that map if you want to include the key images of txs without owned enote candidates, e.g.
//     for legacy scanning where key images can appear in a tx even if none of the tx outputs were sent to you
//   - LEGACY OPTIMIZATION (optional): only key images of rings which include a received enote MUST be collected
//     - if filtering to get those key images is not possible then including all key images works too
///
struct EnoteScanningChunkLedgerV1 final
{
    /// chunk context (includes chunk block range, prefix block id, and chunk block ids)
    scan_machine::ChunkContext context;
    /// owned enote candidates in range [start index, end index)  (mapped to tx id)
    std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> basic_records_per_tx;
    /// key images from txs with owned enote candidates in range [start index, end index)
    std::list<SpContextualKeyImageSetV1> contextual_key_images;
};

////
// RefreshLedgerEnoteStoreConfig
// - configuration details for an on-chain scanning process, adjust these as needed for optimal performance
///
struct RefreshLedgerEnoteStoreConfig final
{
    /// number of blocks below highest known contiguous block to start scanning
    std::uint64_t reorg_avoidance_depth{10};
    /// max number of blocks per on-chain scanning chunk
    std::uint64_t max_chunk_size{100};
    /// maximum number of times to try rescanning if a partial reorg is detected
    std::uint64_t max_partialscan_attempts{3};
};

/**
* brief: check_v1_enote_scan_chunk_ledger_semantics_v1 - check semantics of an on-chain chunk
*   - throws on failure
* param: onchain_chunk -
* param: expected_prefix_index -
*/
void check_v1_enote_scan_chunk_ledger_semantics_v1(const EnoteScanningChunkLedgerV1 &onchain_chunk,
    const std::uint64_t expected_prefix_index);
/**
* brief: check_v1_enote_scan_chunk_nonledger_semantics_v1 - check semantics of an off-chain chunk
*   - throws on failure
* param: nonledger_chunk -
* param: expected_origin_status -
* param: expected_spent_status -
*/
void check_v1_enote_scan_chunk_nonledger_semantics_v1(const EnoteScanningChunkNonLedgerV1 &nonledger_chunk,
    const SpEnoteOriginStatus expected_origin_status,
    const SpEnoteSpentStatus expected_spent_status);
/**
* brief: chunk_is_empty - check if a chunk is empty (has no records)
* param: chunk -
* return: true if the chunk is empty
*/
bool chunk_is_empty(const EnoteScanningChunkNonLedgerV1 &chunk);
bool chunk_is_empty(const EnoteScanningChunkLedgerV1 &chunk);
/**
* brief: refresh_enote_store_offchain - perform an off-chain balance recovery process
* param: expected_origin_status -
* param: expected_spent_status -
* inoutparam: scanning_context_inout -
* inoutparam: enote_store_updater_inout -
* return: false if the refresh was not completely successful (an exception was encountered when getting or processing a
*         chunk)
*/
bool refresh_enote_store_nonledger(const SpEnoteOriginStatus expected_origin_status,
    const SpEnoteSpentStatus expected_spent_status,
    EnoteScanningContextNonLedger &scanning_context_inout,
    EnoteStoreUpdater &enote_store_updater_inout);
/**
* brief: refresh_enote_store_ledger - perform a complete on-chain + unconfirmed cache balance recovery process
* param: config -
* inoutparam: ledger_scanning_context_inout -
* inoutparam: nonledger_scanning_context_inout -
* inoutparam: enote_store_updater_inout -
* return: false if the refresh was not completely successful (a non-exceptional error was encountered, such as too many
*         partial-scan attempts or an exception being thrown deep in the scanning code that was caught and ignored)
*/
bool refresh_enote_store_ledger(const RefreshLedgerEnoteStoreConfig &config,
    EnoteScanningContextNonLedger &nonledger_scanning_context_inout,
    EnoteScanningContextLedger &ledger_scanning_context_inout,
    EnoteStoreUpdater &enote_store_updater_inout);

} //namespace sp
