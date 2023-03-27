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
#include "scan_machine.h"

//local headersx
#include "ringct/rctTypes.h"
#include "seraphis_crypto/math_utils.h"
#include "seraphis_main/scan_chunk_consumer.h"
#include "seraphis_main/scan_core_types.h"
#include "seraphis_main/scan_ledger_chunk.h"
#include "seraphis_main/scan_machine_types.h"
#include "seraphis_main/scan_misc_utils.h"
#include "seraphis_main/scanning_context.h"

//third party headers

//standard headers
#include <iterator>
#include <vector>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis"

namespace sp
{
namespace scanning
{
//-------------------------------------------------------------------------------------------------------------------
// reorg avoidance depth: this is the number of extra blocks to scan below our desired start index in case there was a
//   reorg affecting blocks lower than that start index
// - we use an exponential back-off as a function of fullscan attempts because if a fullscan fails then
//   the true location of alignment divergence is unknown; the distance between the desired start index and the
//   lowest scannable index may be very large, so if a fixed back-off were used it could take many fullscan attempts
//   to find the point of divergence
//-------------------------------------------------------------------------------------------------------------------
static std::uint64_t get_reorg_avoidance_depth(const std::uint64_t reorg_avoidance_increment,
    const std::uint64_t completed_fullscan_attempts)
{
    // 1. start at a depth of zero
    // - this avoids accidentally reorging your data store if the scanning backend only has a portion
    //   of the blocks in your initial reorg avoidance depth range available when 'get chunk' is called (in the case
    //   where there wasn't actually a reorg and the backend is just catching up)
    if (completed_fullscan_attempts == 0)
        return 0;

    // 2. check that the increment is not 0
    // - check this after one fullscan attempt to support unit tests that set the increment to 0
    CHECK_AND_ASSERT_THROW_MES(reorg_avoidance_increment > 0,
        "seraphis scan state machine (get reorg avoidance depth): tried more than one fullscan with zero reorg "
        "avoidance increment.");

    // 3. 10 ^ (fullscan attempts - 1) * increment
    return math::saturating_mul(math::uint_pow(10, completed_fullscan_attempts - 1), reorg_avoidance_increment, -1);
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static std::uint64_t get_estimated_start_scan_index(const std::uint64_t reorg_avoidance_increment,
    const std::uint64_t completed_fullscan_attempts,
    const std::uint64_t lowest_scannable_index,
    const std::uint64_t desired_start_index)
{
    // 1. set reorg avoidance depth
    const std::uint64_t reorg_avoidance_depth{
            get_reorg_avoidance_depth(reorg_avoidance_increment, completed_fullscan_attempts)
        };

    // 2. initial block to scan = max(desired first block - reorg depth, chunk consumer's min scan index)
    return math::saturating_sub(desired_start_index, reorg_avoidance_depth, lowest_scannable_index);
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static void set_initial_contiguity_marker(const ChunkConsumer &chunk_consumer,
    const std::uint64_t estimated_start_scan_index,
    ContiguityMarker &contiguity_marker_out)
{
    // our initial point of contiguity is the consumer's block nearest to the block < our estimated start index,
    //   or the consumer's prefix block
    contiguity_marker_out = chunk_consumer.get_nearest_block(estimated_start_scan_index - 1);

    CHECK_AND_ASSERT_THROW_MES(contiguity_marker_out.block_index + 1 >= chunk_consumer.refresh_index(),
        "sp scan machine (set initial contiguity marker): contiguity marker is below refresh index.");
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool contiguity_check(const ContiguityMarker &marker_A, const ContiguityMarker &marker_B)
{
    // 1. a marker with unspecified block id is contiguous with all markers below and equal to its index (but not
    //    contiguous with markers above it)
    // note: this rule exists so that if the chain index is below our start index, we will be considered
    //       contiguous with it and won't erroneously think we have encountered a reorg (i.e. a broken contiguity);
    //       to see why that matters, change the '<=' to '==' then step through the unit tests that break
    if (!marker_A.block_id &&
        marker_B.block_index + 1 <= marker_A.block_index + 1)
        return true;

    if (!marker_B.block_id &&
        marker_A.block_index + 1 <= marker_B.block_index + 1)
        return true;

    // 2. otherwise, indices must match
    if (marker_A.block_index != marker_B.block_index)
        return false;

    // 3. specified block ids must match
    if (marker_A.block_id &&
        marker_B.block_id &&
        marker_A.block_id != marker_B.block_id)
        return false;

    // 4. unspecified block ids automatically match with specified and unspecified block ids
    return true;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static ScanMachineStatus new_chunk_scan_status(const ContiguityMarker &contiguity_marker,
    const ChunkContext &chunk_context,
    const std::uint64_t first_contiguity_index)
{
    // 1. success case: check if this chunk is contiguous with our marker
    if (contiguity_check(
            contiguity_marker,
            ContiguityMarker{
                chunk_context.start_index - 1,
                chunk_context.start_index > 0
                ? boost::optional<rct::key>{chunk_context.prefix_block_id}
                : boost::none
            }
        ))
        return ScanMachineStatus::SUCCESS;

    // 2. failure case: the chunk is not contiguous, check if we need to full scan
    // - in this case, there was a reorg that affected our first expected point of contiguity (i.e. we obtained no new
    //   chunks that were contiguous with our existing known contiguous chain)
    // note: +1 in case either index is '-1'
    if (first_contiguity_index + 1 >= contiguity_marker.block_index + 1)
        return ScanMachineStatus::NEED_FULLSCAN;

    // 3. failure case: the chunk is not contiguous, but we don't need a full scan
    // - there was a reorg detected but there is new chunk data that wasn't affected
    return ScanMachineStatus::NEED_PARTIALSCAN;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static void update_alignment_marker(const ChunkConsumer &chunk_consumer,
    const std::uint64_t start_index,
    const std::vector<rct::key> &block_ids,
    ContiguityMarker &alignment_marker_inout)
{
    // trace through the block ids to find the highest one that aligns with the chunk consumer's cached block ids
    for (auto ids_it{block_ids.begin()}; ids_it != block_ids.end(); ++ids_it)
    {
        // a. get the chunk consumer's block index closest to this block (i.e. >= this block) in the input set
        const std::uint64_t block_index{
                start_index + std::distance(block_ids.begin(), ids_it)
            };
        const ContiguityMarker consumer_closest_block{chunk_consumer.get_next_block(block_index - 1)};

        // b. exit if the consumer's block is not within the input block range
        if (consumer_closest_block.block_index + 1 < start_index + 1 ||
            consumer_closest_block.block_index + 1 >= start_index + block_ids.size() + 1)
            return;

        // c, sanity check
        // - this is after the range check in case the consumer returned a null marker
        CHECK_AND_ASSERT_THROW_MES(consumer_closest_block.block_index + 1 >= block_index + 1,
            "seraphis scan state machine (update alignment marker): consumer's closest block index is below the "
            "specified block index.");

        // d. move to the consumer's closest block's index
        std::advance(ids_it, consumer_closest_block.block_index - block_index);

        // e. exit if the consumer is not aligned with this block
        // - we are automatically aligned if the consumer's block id is null
        if (consumer_closest_block.block_id &&
            !(*ids_it == *consumer_closest_block.block_id))
            return;

        // f. update the alignment marker
        alignment_marker_inout.block_index = block_index;
        alignment_marker_inout.block_id    = *ids_it;
    }
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static std::vector<rct::key> get_aligned_block_ids(const ChunkConsumer &chunk_consumer,
    const ChunkContext &chunk_context,
    ContiguityMarker &alignment_marker_inout)
{
    // 1. update the alignment marker
    update_alignment_marker(chunk_consumer,
        chunk_context.start_index,
        chunk_context.block_ids,
        alignment_marker_inout);

    // 2. sanity checks
    CHECK_AND_ASSERT_THROW_MES(alignment_marker_inout.block_index + 1 >= chunk_context.start_index,
        "seraphis scan state machine (align block ids): chunk start index exceeds the post-alignment block (bug).");
    CHECK_AND_ASSERT_THROW_MES(alignment_marker_inout.block_index + 1 - chunk_context.start_index <=
            chunk_context.block_ids.size(),
        "seraphis scan state machine (align block ids): the alignment range is larger than the chunk's block range "
        "(bug).");

    // 3. crop chunk block ids that are <= the alignment marker
    return std::vector<rct::key>{
            std::next(
                    chunk_context.block_ids.begin(),
                    alignment_marker_inout.block_index + 1 - chunk_context.start_index
                ),
            chunk_context.block_ids.end()
        };
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static ScanMachineStatus handle_nonempty_chunk(const std::uint64_t first_contiguity_index,
    const LedgerChunk &ledger_chunk,
    ChunkConsumer &chunk_consumer_inout,
    ContiguityMarker &contiguity_marker_inout)
{
    // note: we don't check if the scanning context is aborted here because the process could have been aborted after
    //   the chunk was acquired
    const ChunkContext &chunk_context{ledger_chunk.get_context()};

    // 1. verify this is a non-empty chunk
    CHECK_AND_ASSERT_THROW_MES(!chunk_is_empty(ledger_chunk),
        "seraphis scan state machine (handle nonempty chunk): chunk is empty unexpectedly.");

    // 2. check if this chunk is contiguous with the contiguity marker
    // - if not contiguous then there must have been a reorg, so we need to rescan
    const ScanMachineStatus scan_status{
            new_chunk_scan_status(contiguity_marker_inout, chunk_context, first_contiguity_index)
        };

    if (scan_status != ScanMachineStatus::SUCCESS)
        return scan_status;

    // 3. set alignment marker (assume we always start aligned)
    // - alignment means a block id in a chunk matches the chunk consumer's block id at the alignment block index
    ContiguityMarker alignment_marker{contiguity_marker_inout};

    // 4. align the chunk's block ids with the chunk consumer
    // - update the point of alignment if this chunk overlaps with blocks known by the chunk consumer
    // - crop the chunk's block ids to only include block ids unknown to the chunk consumer
    const std::vector<rct::key> aligned_block_ids{
            get_aligned_block_ids(chunk_consumer_inout, chunk_context, alignment_marker)
        };

    // 5. validate chunk semantics
    // - do this after checking the new chunk's scan status in case the chunk data is deferred; we don't want to block
    //   on getting the data until we know we will need it
    check_ledger_chunk_semantics_v1(ledger_chunk, contiguity_marker_inout.block_index);

    // 6. consume the chunk if it's not empty
    // - if the chunk is empty after aligning, that means our chunk consumer already knows about the entire span
    //   of the chunk; we don't want to pass the chunk in, because there may be blocks in the NEXT chunk that
    //   our chunk consumer also knows about; we don't want the chunk consumer to think it needs to roll back its state
    //   to the top of this chunk
    if (aligned_block_ids.size() > 0)
    {
        chunk_consumer_inout.consume_onchain_chunk(ledger_chunk,
            alignment_marker.block_id ? *(alignment_marker.block_id) : rct::zero(),
            alignment_marker.block_index + 1,
            aligned_block_ids);
    }

    // 7. set contiguity marker to last block of this chunk
    CHECK_AND_ASSERT_THROW_MES(chunk_context.block_ids.size() > 0,
        "seraphis scan state machine (handle nonempty chunk): no block ids (bug).");

    contiguity_marker_inout.block_index = chunk_context.start_index + chunk_context.block_ids.size() - 1;
    contiguity_marker_inout.block_id    = chunk_context.block_ids.back();

    // 8. next scan state: scan another chunk
    return ScanMachineStatus::DO_SCAN;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static ScanMachineStatus handle_empty_chunk(const std::uint64_t first_contiguity_index,
    const LedgerChunk &ledger_chunk,
    ScanningContextLedger &scanning_context_inout,
    ChunkConsumer &chunk_consumer_inout,
    ContiguityMarker &contiguity_marker_inout)
{
    const ChunkContext &chunk_context{ledger_chunk.get_context()};

    // 1. verify that the chunk obtained is an empty chunk representing the top of the current blockchain
    CHECK_AND_ASSERT_THROW_MES(chunk_is_empty(chunk_context),
        "seraphis scan state machine (handle empty chunk): chunk is not empty as expected.");

    // 2. check if the scan process is aborted
    // - when a scan process is aborted, the empty chunk returned may not represent the end of the chain, so we don't
    //   want to consume that chunk
    if (scanning_context_inout.is_aborted())
        return ScanMachineStatus::ABORTED;

    // 3. verify that our termination chunk is contiguous with the chunks received so far
    // - this can fail if a reorg dropped below our contiguity marker without replacing the dropped blocks, causing the
    //   first chunk obtained after the reorg to be this empty termination chunk
    // note: this test won't fail if the chain's top index is below our contiguity marker when our contiguity marker has
    //       an unspecified block id; we don't care if the top index is lower than our scanning 'backstop' (i.e.
    //       lowest point in our chunk consumer) when we haven't actually scanned any blocks
    const ScanMachineStatus scan_status{
            new_chunk_scan_status(contiguity_marker_inout, chunk_context, first_contiguity_index)
        };

    if (scan_status != ScanMachineStatus::SUCCESS)
        return scan_status;

    // 4. final update for our chunk consumer
    // - we need to update with the termination chunk in case a reorg popped blocks, so the chunk consumer can roll back
    //   its state
    chunk_consumer_inout.consume_onchain_chunk(ledger_chunk,
        contiguity_marker_inout.block_id ? *(contiguity_marker_inout.block_id) : rct::zero(),
        contiguity_marker_inout.block_index + 1,
        {});

    // 5. no more scanning required
    return ScanMachineStatus::SUCCESS;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static ScanMachineStatus do_scan_pass(const std::uint64_t first_contiguity_index,
    ScanningContextLedger &scanning_context_inout,
    ChunkConsumer &chunk_consumer_inout,
    ContiguityMarker &contiguity_marker_inout)
{
    // 1. get a new chunk
    std::unique_ptr<LedgerChunk> new_chunk;
    try
    {
        new_chunk = scanning_context_inout.get_onchain_chunk();
        CHECK_AND_ASSERT_THROW_MES(new_chunk, "seraphis scan state machine (do scan pass): chunk obtained is null.");
    }
    catch (...)
    {
        LOG_ERROR("seraphis scan state machine (do scan pass): get chunk failed.");
        throw;
    }

    // 2. handle the chunk
    if (chunk_is_empty(*new_chunk))
    {
        return handle_empty_chunk(first_contiguity_index,
            *new_chunk,
            scanning_context_inout,
            chunk_consumer_inout,
            contiguity_marker_inout);
    }
    else
    {
        return handle_nonempty_chunk(first_contiguity_index,
            *new_chunk,
            chunk_consumer_inout,
            contiguity_marker_inout);
    }
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool try_handle_need_fullscan(const ChunkConsumer &chunk_consumer, ScanMachineMetadata &metadata_inout)
{
    if (metadata_inout.status != ScanMachineStatus::NEED_FULLSCAN)
        return false;

    // 1. get index of the first block we want to scan
    // - this is only an estimate since the chunk consumer may not have the block at this exact index cached
    const std::uint64_t estimated_start_scan_index{
            get_estimated_start_scan_index(metadata_inout.config.reorg_avoidance_increment,
                metadata_inout.fullscan_attempts,
                chunk_consumer.refresh_index(),
                chunk_consumer.desired_first_block())
        };

    // 2. set initial contiguity marker
    // - this starts as the prefix of the first block to scan, and should either be known to the chunk consumer
    //   or have an unspecified block id
    metadata_inout.contiguity_marker = ContiguityMarker{};
    set_initial_contiguity_marker(chunk_consumer, estimated_start_scan_index, metadata_inout.contiguity_marker);

    // 3. record the scan attempt
    metadata_inout.fullscan_attempts += 1;

    if (metadata_inout.fullscan_attempts > 50)
    {
        LOG_ERROR("scan state machine (handle need fullscan): fullscan attempts exceeded 50 (sanity check fail).");
        metadata_inout.status = ScanMachineStatus::FAIL;
        return true;
    }

    // 4. prepare the next state
    metadata_inout.status = ScanMachineStatus::START_SCAN;

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool try_handle_need_partialscan(const ChunkConsumer &chunk_consumer, ScanMachineMetadata &metadata_inout)
{
    if (metadata_inout.status != ScanMachineStatus::NEED_PARTIALSCAN)
        return false;

    // 1. get index of the first block we want to scan
    const std::uint64_t estimated_start_scan_index{
            get_estimated_start_scan_index(metadata_inout.config.reorg_avoidance_increment,
                1,  //in partial scans always back off by just one reorg avoidance increment
                chunk_consumer.refresh_index(),
                chunk_consumer.desired_first_block())
        };

    // 2. set initial contiguity marker
    // - this starts as the prefix of the first block to scan, and should either be known to the chunk consumer
    //   or have an unspecified block id
    metadata_inout.contiguity_marker = ContiguityMarker{};
    set_initial_contiguity_marker(chunk_consumer, estimated_start_scan_index, metadata_inout.contiguity_marker);

    // 3. record the scan attempt
    metadata_inout.partialscan_attempts += 1;

    // 4. prepare the next state
    // a. fail if we have exceeded the max number of partial scanning attempts (i.e. too many reorgs were detected,
    //    so now we abort)
    if (metadata_inout.partialscan_attempts > metadata_inout.config.max_partialscan_attempts)
        metadata_inout.status = ScanMachineStatus::FAIL;
    // b. otherwise, scan
    else
        metadata_inout.status = ScanMachineStatus::START_SCAN;

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool try_handle_start_scan(ScanningContextLedger &scanning_context_inout, ScanMachineMetadata &metadata_inout)
{
    if (metadata_inout.status != ScanMachineStatus::START_SCAN)
        return false;

    try
    {
        // a. initialize the scanning context
        scanning_context_inout.begin_scanning_from_index(metadata_inout.contiguity_marker.block_index + 1,
            metadata_inout.config.max_chunk_size);

        // b. prepare the next state
        metadata_inout.status                 = ScanMachineStatus::DO_SCAN;
        metadata_inout.first_contiguity_index = metadata_inout.contiguity_marker.block_index;
    }
    catch (...) { metadata_inout.status = ScanMachineStatus::FAIL; }

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool try_handle_do_scan(ScanningContextLedger &scanning_context_inout,
    ChunkConsumer &chunk_consumer_inout,
    ScanMachineMetadata &metadata_inout)
{
    if (metadata_inout.status != ScanMachineStatus::DO_SCAN)
        return false;

    // 1. perform one scan pass then update the status
    try
    {
        metadata_inout.status = do_scan_pass(metadata_inout.first_contiguity_index,
            scanning_context_inout,
            chunk_consumer_inout,
            metadata_inout.contiguity_marker);
    }
    catch (...) { metadata_inout.status = ScanMachineStatus::FAIL; }

    // 2. try to terminate the scanning context if the next state is not another scan pass
    try
    {
        if (metadata_inout.status != ScanMachineStatus::DO_SCAN)
            scanning_context_inout.terminate_scanning();
    } catch (...) { LOG_ERROR("seraphis scan state machine (try handle do scan): scan context termination failed."); }

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool is_terminal_state_with_log(const ScanMachineStatus status)
{
    // 1. check if in a terminal state
    if (!is_terminal_state(status))
        return false;

    // 2. log error as needed
    if (status == ScanMachineStatus::FAIL)
        LOG_ERROR("seraphis scan state machine (terminal state): scan failed!");
    else if (status == ScanMachineStatus::ABORTED)
        LOG_ERROR("seraphis scan state machine (terminal state): scan aborted!");
    else if (status != ScanMachineStatus::SUCCESS)
        LOG_ERROR("seraphis scan state machine (terminal state): unknown failure!");

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
bool try_advance_state_machine(ScanMachineMetadata &metadata_inout,
    ScanningContextLedger &scanning_context_inout,
    ChunkConsumer &chunk_consumer_inout)
{
    // check terminal states
    if (is_terminal_state_with_log(metadata_inout.status))
        return false;

    // NEED_FULLSCAN
    if (try_handle_need_fullscan(chunk_consumer_inout, metadata_inout))
        return true;

    // NEED_PARTIALSCAN
    if (try_handle_need_partialscan(chunk_consumer_inout, metadata_inout))
        return true;

    // START_SCAN
    if (try_handle_start_scan(scanning_context_inout, metadata_inout))
        return true;

    // DO_SCAN
    if (try_handle_do_scan(scanning_context_inout, chunk_consumer_inout, metadata_inout))
        return true;

    return false;
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace scanning
} //namespace sp
