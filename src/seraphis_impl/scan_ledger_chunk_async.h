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

// Async ledger chunk.

#pragma once

//local headers
#include "async/misc_utils.h"
#include "async/threadpool.h"
#include "misc_log_ex.h"
#include "ringct/rctTypes.h"
#include "seraphis_main/scan_core_types.h"
#include "seraphis_main/scan_ledger_chunk.h"
#include "seraphis_main/scan_misc_utils.h"

//third party headers

//standard headers
#include <future>
#include <vector>

//forward declarations


namespace sp
{
namespace scanning
{

//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------

struct PendingChunkContext final
{
    std::promise<void> stop_signal;                  //for canceling the pending context request
    std::shared_future<ChunkContext> chunk_context;  //start index, element ids, prefix id
    async::join_condition_t context_join_condition;  //for waiting on the chunk context
};

struct PendingChunkData final
{
    std::promise<void> stop_signal;              //for canceling the pending data request
    std::shared_future<ChunkData> chunk_data;    //basic enote records and contextual key image sets
    async::join_condition_t data_join_condition; //for waiting on the chunk data
};

//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------

class AsyncLedgerChunk final : public LedgerChunk
{
    void wait_for_context() const
    {
        if (async::future_is_ready(m_pending_context.chunk_context))
            return;

        m_threadpool.work_while_waiting(m_pending_context.context_join_condition, async::DefaultPriorityLevels::MAX);
        assert(async::future_is_ready(m_pending_context.chunk_context));  //should be ready at this point
    }

    void wait_for_data(const std::size_t pending_data_index) const
    {
        if (pending_data_index >= m_pending_data.size())
            return;
        if (async::future_is_ready(m_pending_data[pending_data_index].chunk_data))
            return;

        m_threadpool.work_while_waiting(m_pending_data[pending_data_index].data_join_condition,
            async::DefaultPriorityLevels::MAX);
        assert(async::future_is_ready(m_pending_data[pending_data_index].chunk_data));  //should be ready at this point
    }

public:
    AsyncLedgerChunk(async::Threadpool &threadpool,
        PendingChunkContext &&pending_context,
        std::vector<PendingChunkData> &&pending_data,
        std::vector<rct::key> subconsumer_ids) :
            m_threadpool{threadpool},
            m_pending_context{std::move(pending_context)},
            m_pending_data{std::move(pending_data)},
            m_subconsumer_ids{std::move(subconsumer_ids)}
    {
        CHECK_AND_ASSERT_THROW_MES(m_pending_data.size() == m_subconsumer_ids.size(),
            "async ledger chunk: pending data and subconsumer ids size mismatch.");
    }

    const ChunkContext& get_context() const override
    {
        this->wait_for_context();
        return m_pending_context.chunk_context.get();
    }
    const ChunkData* try_get_data(const rct::key &subconsumer_id) const override
    {
        auto id_it = std::find(m_subconsumer_ids.begin(), m_subconsumer_ids.end(), subconsumer_id);
        if (id_it == m_subconsumer_ids.end()) return nullptr;
        const std::size_t pending_data_index{static_cast<size_t>(std::distance(m_subconsumer_ids.begin(), id_it))};

        this->wait_for_data(pending_data_index);
        return &(m_pending_data[pending_data_index].chunk_data.get());
    }
    const std::vector<rct::key>& subconsumer_ids() const override
    {
        return m_subconsumer_ids;
    }

private:
    async::Threadpool &m_threadpool;
    mutable PendingChunkContext m_pending_context;
    mutable std::vector<PendingChunkData> m_pending_data;
    const std::vector<rct::key> m_subconsumer_ids;
};

} //namespace scanning
} //namespace sp
