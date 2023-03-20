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

// State machine for scanning a LIFO chain of blocks by incrementally processing chunks of that chain.

// Core interface for balance recovery framework (works for both legacy and seraphis backends).
// PRECONDITIONS:
// 1. chunks must be built from an atomic view of the source cache (ledger, unconfirmed cache, offchain cache)
// 2. chunk data: contextual_key_images must reference a tx recorded in basic_records_per_tx (even if you
//    need to add empty map entries to achieve that)
// 3. any call to get a chunk from a scanning context should produce a chunk that is at least as fresh as any
//    other chunk obtained from that context (atomic ordering)
// 4. any call to consume a chunk in a chunk consumer should resolve all side-effects observable via the consumer's
//    interface by the time the call is complete (e.g. any changes to block ids observable by try_get_block_id() need
//    to be completed during the 'consume chunk' call)

#pragma once

//local headers
#include "seraphis_main/scan_machine_types.h"

//third party headers

//standard headers

//forward declarations
namespace sp
{
namespace scanning
{
    class ScanningContextLedger;
    class ChunkConsumer;
}
}

namespace sp
{
namespace scanning
{

/**
* brief: try_advance_state_machine - advance the scan state machine to the next state
* inoutparam: metadata_inout -
* inoutparam: scanning_context_inout -
* inoutparam: chunk_consumer_inout -
* return: true if the machine was advanced to a new non-terminal state, false if the machine is in a terminal state
*/
bool try_advance_state_machine(ScanMachineMetadata &metadata_inout,
    ScanningContextLedger &scanning_context_inout,
    ChunkConsumer &chunk_consumer_inout);

} //namespace scanning
} //namespace sp
