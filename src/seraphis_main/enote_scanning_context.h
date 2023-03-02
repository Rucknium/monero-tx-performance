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

// Dependency injectors for managing the find-received step of enote scanning. Intended to be stateful, managing
//   a connection to a context and linking together successive 'get chunk' calls.

#pragma once

//local headers
#include "enote_scanning.h"

//third party headers

//standard headers

//forward declarations


namespace sp
{

////
// EnoteScanningContextNonLedger
// - manages a source of non-ledger-based enote scanning chunks
///
class EnoteScanningContextNonLedger
{
public:
//destructor
    virtual ~EnoteScanningContextNonLedger() = default;

//overloaded operators
    /// disable copy/move (this is a virtual base class)
    EnoteScanningContextNonLedger& operator=(EnoteScanningContextNonLedger&&) = delete;

//member functions
    /// get a scanning chunk for the nonledger txs associated with this context
    virtual void get_nonledger_chunk(EnoteScanningChunkNonLedgerV1 &chunk_out) = 0;
    /// test if scanning has been aborted
    /// EXPECTATION: if this returns true then all subsequent calls to 'get chunk' should return an empty chunk
    virtual bool is_aborted() const = 0;
};

////
// EnoteScanningContextLedger
// - manages a source of ledger-based enote scanning chunks (i.e. finding potentially owned enotes in a ledger)
///
class EnoteScanningContextLedger
{
public:
//destructor
    virtual ~EnoteScanningContextLedger() = default;

//overloaded operators
    /// disable copy/move (this is a virtual base class)
    EnoteScanningContextLedger& operator=(EnoteScanningContextLedger&&) = delete;

//member functions
    /// tell the scanning context a block index to start scanning from
    virtual void begin_scanning_from_index(const std::uint64_t initial_start_index,
        const std::uint64_t max_chunk_size) = 0;
    /// get the next available onchain chunk (must be contiguous with the last chunk acquired since starting to scan)
    /// note: if there is no chunk to return, return an empty chunk representing the top of the current chain
    virtual void get_onchain_chunk(EnoteScanningChunkLedgerV1 &chunk_out) = 0;
    /// tell the scanning context to stop its scanning process (should be no-throw no-fail)
    virtual void terminate_scanning() = 0;
    /// test if scanning has been aborted
    /// EXPECTATION: if this returns true then all subsequent calls to 'get chunk' should return an empty chunk
    virtual bool is_aborted() const = 0;
};

} //namespace sp
