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

// Simple implementations enote scanning contexts.

#pragma once

//local headers
#include "enote_finding_context.h"
#include "enote_scanning.h"
#include "enote_scanning_context.h"

//third party headers

//standard headers

//forward declarations


namespace sp
{

////
// EnoteScanningContextNonLedgerDummy
// - dummy nonledger scanning context
///
class EnoteScanningContextNonLedgerDummy final : public EnoteScanningContextNonLedger
{
public:
    void get_nonledger_chunk(EnoteScanningChunkNonLedgerV1 &chunk_out) override
    {
        chunk_out = EnoteScanningChunkNonLedgerV1{};
    }
    bool is_aborted() const override { return false; }
};

////
// EnoteScanningContextNonLedgerSimple
// - simple implementation: synchronously obtain chunks from an enote finding context
///
class EnoteScanningContextNonLedgerSimple final : public EnoteScanningContextNonLedger
{
public:
//constructor
    EnoteScanningContextNonLedgerSimple(const EnoteFindingContextNonLedger &enote_finding_context) :
        m_enote_finding_context{enote_finding_context}
    {}

//overloaded operators
    /// disable copy/move (this is a scoped manager [reference wrapper])
    EnoteScanningContextNonLedgerSimple& operator=(EnoteScanningContextNonLedgerSimple&&) = delete;

//member functions
    /// get a scanning chunk for the nonledger txs in the injected context
    void get_nonledger_chunk(EnoteScanningChunkNonLedgerV1 &chunk_out) override
    {
        m_enote_finding_context.get_nonledger_chunk(chunk_out);
    }
    /// test if scanning has been aborted
    bool is_aborted() const override { return false; }

//member variables
private:
    /// finds chunks of enotes that are potentially owned
    const EnoteFindingContextNonLedger &m_enote_finding_context;
};

////
// EnoteScanningContextLedgerSimple
// - simple implementation: synchronously obtain chunks from an enote finding context
///
class EnoteScanningContextLedgerSimple final : public EnoteScanningContextLedger
{
public:
//constructor
    EnoteScanningContextLedgerSimple(const EnoteFindingContextLedger &enote_finding_context) :
        m_enote_finding_context{enote_finding_context}
    {}

//overloaded operators
    /// disable copy/move (this is a scoped manager [reference wrapper])
    EnoteScanningContextLedgerSimple& operator=(EnoteScanningContextLedgerSimple&&) = delete;

//member functions
    /// start scanning from a specified block index
    void begin_scanning_from_index(const std::uint64_t initial_start_index, const std::uint64_t max_chunk_size) override
    {
        m_next_start_index = initial_start_index;
        m_max_chunk_size   = max_chunk_size;
    }
    /// get the next available onchain chunk (or empty chunk representing top of current chain)
    /// - start past the end of the last chunk acquired since starting to scan
    void get_onchain_chunk(EnoteScanningChunkLedgerV1 &chunk_out) override
    {
        m_enote_finding_context.get_onchain_chunk(m_next_start_index, m_max_chunk_size, chunk_out);
        m_next_start_index = chunk_out.start_index + chunk_out.block_ids.size();
    }
    /// stop the current scanning process (should be no-throw no-fail)
    void terminate_scanning() override { /* no-op */ }
    /// test if scanning has been aborted
    bool is_aborted() const override { return false; }

//member variables
private:
    /// finds chunks of enotes that are potentially owned
    const EnoteFindingContextLedger &m_enote_finding_context;

    std::uint64_t m_next_start_index{static_cast<std::uint64_t>(-1)};
    std::uint64_t m_max_chunk_size{0};
};

} //namespace sp
