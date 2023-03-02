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

// State machine for scanning a LIFO chain of elements by incrementally processing chunks of that chain.

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
    /// - each fullscan attempt looks (10^attempts * increment) elements below the requested start index
    std::uint64_t reorg_avoidance_increment{10};
    /// max number of elements per on-chain scanning chunk
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
// ContiguityMarker
// - marks the end of a contiguous chain of elements
// - if the contiguous chain is empty, then the element id will be unspecified and the element index will equal the chain's
//   initial index minus one
// - a 'contiguous chain' does not have to start at 'element 0', it can start at any predefined element index where you
//   want to start tracking contiguity
// - example: if your refresh index is 'element 101' and you haven't loaded/scanned any elements, then your initial
//   contiguity marker will start at 'element 100' with an unspecified element id; if you scanned elements [101, 120], then
//   your contiguity marker will be at element 120 with that element's element id
///
struct ContiguityMarker final
{
    /// index of the element
    std::uint64_t element_index;
    /// id of the element (optional)
    boost::optional<rct::key> element_id;
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

    ContiguityMarker contiguity_marker;
    std::uint64_t first_contiguity_index;
};

////
// ChunkContext
// - chunk range (in element indices): [start index, end index)
//   - end index = start index + num elements
// - prefix element id: id of element that comes before the chunk range, used for contiguity checks between chunks and with
//   the chunk consumer
///
struct ChunkContext final
{
    /// start index
    std::uint64_t start_index;
    /// element id at 'start index - 1'  (implicitly ignored if start_index == 0)
    rct::key prefix_element_id;
    /// element ids in range [start index, end index)
    std::vector<rct::key> element_ids;
};

/**
* brief: chunk_is_empty - check if a chunk is empty (has no records)
* param: chunk_context -
* return: true if the chunk is empty
*/
bool chunk_is_empty(const ChunkContext &chunk_context);
/**
* brief: try_advance_state_machine - advance the scan state machine to the next state
* inoutparam: metadata_inout -
* inoutparam: scanning_context_inout -
* inoutparam: enote_store_updater_inout -
* return: true if the machine was advanced to a new state, false if the machine is in a terminal state
*/
bool try_advance_state_machine(ScanMetadata &metadata_inout,
    EnoteScanningContextLedger &scanning_context_inout,
    EnoteStoreUpdater &enote_store_updater_inout);

} //namespace scan_machine
} //namespace sp
