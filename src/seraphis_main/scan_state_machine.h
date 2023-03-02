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

// State machine for scanning a ledger.

#pragma once

//local headers
#include "ringct/rctTypes.h"

//third party headers
#include <boost/optional/optional.hpp>

//standard headers

//forward declarations
namespace sp
{
    class EnoteScanningContextLedger;
    class EnoteStoreUpdater;
}

namespace sp
{
namespace scan_machine
{

////
// ScanConfig
// - configuration details for the scan state machine
///
struct ScanConfig final
{
    /// increment for avoiding reorgs
    /// - each fullscan attempt looks (10^attempts * increment) blocks below the requested start index
    std::uint64_t reorg_avoidance_increment{10};
    /// max number of blocks per on-chain scanning chunk
    std::uint64_t max_chunk_size{100};
    /// maximum number of times to try rescanning if a partial reorg is detected
    std::uint64_t max_partialscan_attempts{3};
};

////
// ScanStatus
// - helper enum for tracking the state of a scan process
///
enum class ScanStatus : unsigned char
{
    NEED_FULLSCAN,
    NEED_PARTIALSCAN,
    START_SCAN,
    DO_SCAN,
    SUCCESS,
    FAIL,
    ABORTED
};

////
// ChainContiguityMarker
// - marks the end of a contiguous chain of blocks
// - if the contiguous chain is empty, then the block id will be unspecified and the block index will equal the chain's
//   initial index minus one
// - a 'contiguous chain' does not have to start at 'block 0', it can start at any predefined block index where you
//   want to start tracking contiguity
// - example: if your refresh index is 'block 101' and you haven't loaded/scanned any blocks, then your initial
//   contiguity marker will start at 'block 100' with an unspecified block id; if you scanned blocks [101, 120], then
//   your contiguity marker will be at block 120 with that block's block id
///
struct ChainContiguityMarker final
{
    /// index of the block
    std::uint64_t block_index;
    /// id of the block (optional)
    boost::optional<rct::key> block_id;
};

////
// ScanMetadata
// - metadata for the scan state machine
///
struct ScanMetadata final
{
    ScanConfig config;

    ScanStatus status{ScanStatus::NEED_FULLSCAN};
    std::size_t partialscan_attempts{0};
    std::size_t fullscan_attempts{0};

    ChainContiguityMarker contiguity_marker;
    std::uint64_t first_contiguity_index;
};

/**
* brief: try_find_legacy_enotes_in_tx - obtain contextual basic records from a legacy tx's contents
* param: legacy_base_spend_pubkey -
* param: legacy_subaddress_map -
* param: legacy_view_privkey -
* param: block_index -
* param: block_timestamp -
* param: transaction_id -
* param: total_enotes_before_tx - number of legacy enotes ordered before this tx (set to '0' if tx is non-ledger)
* param: unlock_time -
* param: tx_memo -
* param: enotes_in_tx -
* param: origin_status -
* inoutparam: hwdev -
* outparam: basic_records_in_tx_out -
*/
bool try_advance_state_machine(ScanMetadata &metadata_inout,
    EnoteScanningContextLedger &scanning_context_inout,
    EnoteStoreUpdater &enote_store_updater_inout);

} //namespace scan_machine
} //namespace sp
