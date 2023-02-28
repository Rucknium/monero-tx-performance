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
#include "ringct/rctTypes.h"
#include "seraphis_crypto/math_utils.h"

//third party headers
#include <boost/optional/optional.hpp>

//standard headers
#include <iterator>
#include <list>
#include <unordered_map>
#include <vector>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis"

namespace sp
{

////
// EnoteScanProcessLedger
// - raii wrapper on an EnoteScanningContextLedger for a specific scanning process: begin ... terminate
///
class EnoteScanProcessLedger final
{
public:
//constructors
    /// normal constructor
    EnoteScanProcessLedger(const std::uint64_t initial_start_index,
        const std::uint64_t max_chunk_size,
        EnoteScanningContextLedger &enote_scan_context) :
            m_enote_scan_context{enote_scan_context}
    {
        m_enote_scan_context.begin_scanning_from_index(initial_start_index, max_chunk_size);
    }

//overloaded operators
    /// disable copy/move (this is a scoped manager [reference wrapper])
    EnoteScanProcessLedger& operator=(EnoteScanProcessLedger&&) = delete;

//destructor
    ~EnoteScanProcessLedger()
    {
        try { m_enote_scan_context.terminate_scanning(); }
        catch (...) { LOG_ERROR("seraphis enote scan process ledger (destructor): scan context termination failed."); }
    }

//member functions
    /// get the next available onchain chunk (must be contiguous with the last chunk acquired since starting to scan)
    /// note: when no more chunks to get, obtain an empty chunk representing the top of the current chain
    void get_onchain_chunk(EnoteScanningChunkLedgerV1 &chunk_out)
    {
        try { m_enote_scan_context.get_onchain_chunk(chunk_out); }
        catch (...)
        {
            LOG_ERROR("seraphis enote scan process ledger (get onchain chunk): get chunk failed.");
            throw;
        }
    }
    /// try to get a scanning chunk for the unconfirmed txs that are pending inclusion in a ledger
    void get_unconfirmed_chunk(EnoteScanningChunkNonLedgerV1 &chunk_out)
    {
        try { m_enote_scan_context.get_unconfirmed_chunk(chunk_out); }
        catch (...)
        {
            LOG_ERROR("seraphis enote scan process ledger (get unconfirmed chunk): get chunk failed.");
            throw;
        }
    }
    /// test if the scan process has been aborted
    bool is_aborted() const
    {
        return m_enote_scan_context.is_aborted();
    }

//member variables
private:
    /// reference to an enote scanning context
    EnoteScanningContextLedger &m_enote_scan_context;
};

////
// ScanStatus
// - helper enum for reporting the outcome of a scan process
///
enum class ScanStatus
{
    NEED_FULLSCAN,
    NEED_PARTIALSCAN,
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

//-------------------------------------------------------------------------------------------------------------------
// - this is the number of extra elements to scan below our desired start index in case there was a reorg lower
//   than that start index
// - we use an exponential back-off as a function of fullscan attempts because if a fullscan fails then
//   the true location of alignment divergence is unknown; moreover, the distance between the desired
//   start index and the lowest scannable index may be very large; if a fixed back-off were
//   used, then it could take many fullscan attempts to find the point of divergence
//-------------------------------------------------------------------------------------------------------------------
static std::uint64_t get_reorg_avoidance_depth(const std::uint64_t default_reorg_avoidance_depth,
    const std::uint64_t completed_fullscan_attempts)
{
    // 1. start at a depth of zero
    // - this avoids accidentally reorging your data store if the scanning backend only has a portion
    //   of the elements in your initial reorg avoidance depth range available when 'get chunk' is called (in the case
    //   where there wasn't actually a reorg and the backend is just catching up)
    if (completed_fullscan_attempts == 0)
        return 0;

    // 2. check that the default depth is not 0
    // - check this after one fullscan attempt to support unit tests that set the reorg avoidance depth to 0
    CHECK_AND_ASSERT_THROW_MES(default_reorg_avoidance_depth > 0,
        "refresh ledger for enote store: tried more than one fullscan with zero reorg avoidance depth.");

    // 3. 10 ^ (fullscan attempts) * default depth
    return math::uint_pow(10, completed_fullscan_attempts - 1) * default_reorg_avoidance_depth;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static std::uint64_t get_start_scan_index(const std::uint64_t reorg_avoidance_increment,
    const std::uint64_t completed_fullscan_attempts,
    const std::uint64_t lowest_scannable_index,
    const std::uint64_t desired_start_index)
{
    // 1. set reorg avoidance depth
    // - this is the number of extra elements to scan below our desired start index in case there was a reorg lower
    //   than our initial contiguity marker before this scan attempt
    // note: we use an exponential back-off as a function of fullscan attempts because if a fullscan fails then
    //       the true location of alignment divergence is unknown; moreover, the distance between the first
    //       desired start index and the enote store's refresh index may be very large; if a fixed back-off were
    //       used, then it could take many fullscan attempts to find the point of divergence
    // note: we start at '0', which means if a NEED_PARTIALSCAN is detected before any NEED_FULLSCANs then we will
    //       have to loop through and get a NEED_FULLSCAN before the reorg can be resolved (it should be fairly cheap)
    const std::uint64_t reorg_avoidance_depth{
            get_reorg_avoidance_depth(reorg_avoidance_increment, completed_fullscan_attempts)
        };

    // 2. initial block to scan = max(desired first block - reorg depth, enote store's min scan index)
    if (desired_start_index >= reorg_avoidance_depth + lowest_scannable_index)
        return desired_start_index - reorg_avoidance_depth;
    else
        return lowest_scannable_index;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static void set_initial_contiguity_marker(const EnoteStoreUpdater &enote_store_updater,
    const std::uint64_t initial_refresh_index,
    ChainContiguityMarker &contiguity_marker_inout)
{
    // 1. set the block index
    contiguity_marker_inout.block_index = initial_refresh_index - 1;

    // 2. set the block id if we aren't at the updater's prefix block
    if (contiguity_marker_inout.block_index != enote_store_updater.refresh_index() - 1)
    {
        // getting a block id should always succeed if we are starting past the prefix block of the updater
        contiguity_marker_inout.block_id = rct::zero();
        CHECK_AND_ASSERT_THROW_MES(enote_store_updater.try_get_block_id(initial_refresh_index - 1,
                *(contiguity_marker_inout.block_id)),
            "refresh ledger for enote store: could not get block id for start of scanning but a block id was "
            "expected (bug).");
    }
}
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
static bool chunk_is_empty(const EnoteScanningChunkLedgerV1 &chunk)
{
    return chunk.block_ids.size() == 0;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool chunk_is_empty(const EnoteScanningChunkNonLedgerV1 &chunk)
{
    return chunk.basic_records_per_tx.size() == 0 &&
        chunk.contextual_key_images.size() == 0;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool contiguity_check(const ChainContiguityMarker &marker_A, const ChainContiguityMarker &marker_B)
{
    // 1. a marker with unspecified block id is contiguous with all markers below and equal to its index (but not
    //    contiguous with markers above them)
    // note: this odd rule exists so that if the chain index is below our start index, we will be considered
    //       contiguous with it and won't erroneously think we have encountered a reorg (i.e. a broken contiguity);
    //       to explore that situation, change the '<=' to '==' then step through the unit tests that break
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
static ScanStatus get_scan_status(const ChainContiguityMarker &contiguity_marker,
    const EnoteScanningChunkLedgerV1 &chunk,
    const std::uint64_t first_contiguity_index,
    const std::uint64_t full_discontinuity_test_index)
{
    // 1. success case: check if this chunk is contiguous with our marker
    if (contiguity_check(contiguity_marker, ChainContiguityMarker{chunk.start_index - 1, chunk.prefix_block_id}))
        return ScanStatus::SUCCESS;

    // 2. failure case: the chunk is not contiguous, check if we need to full scan
    // - in this case, there was a reorg that affected our first expected point of contiguity (i.e. we obtained no new
    //   chunks that were contiguous with our existing known contiguous chain)
    // note: +1 in case either index is '-1'
    if (first_contiguity_index + 1 >= full_discontinuity_test_index + 1)
        return ScanStatus::NEED_FULLSCAN;

    // 3. failure case: the chunk is not contiguous, but we don't need a full scan
    // - there was a reorg detected but there is new chunk data that wasn't affected
    return ScanStatus::NEED_PARTIALSCAN;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static void update_alignment_marker(const EnoteStoreUpdater &enote_store_updater,
    const std::uint64_t start_index,
    const std::vector<rct::key> &block_ids,
    ChainContiguityMarker &alignment_marker_inout)
{
    // trace through the block ids to find the heighest one that matches with the enote store's recorded block ids
    rct::key next_block_id;
    for (std::size_t block_index{0}; block_index < block_ids.size(); ++block_index)
    {
        if (!enote_store_updater.try_get_block_id(start_index + block_index, next_block_id))
            return;

        if (!(next_block_id == block_ids[block_index]))
            return;

        alignment_marker_inout.block_index = start_index + block_index;
        alignment_marker_inout.block_id    = next_block_id;
    }
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static void align_block_ids(const EnoteStoreUpdater &enote_store_updater,
    const EnoteScanningChunkLedgerV1 &chunk,
    ChainContiguityMarker &alignment_marker_inout,
    std::vector<rct::key> &scanned_block_ids_cropped_inout)
{
    // 1. update the alignment marker
    update_alignment_marker(enote_store_updater, chunk.start_index, chunk.block_ids, alignment_marker_inout);

    // 2. sanity checks
    CHECK_AND_ASSERT_THROW_MES(alignment_marker_inout.block_index + 1 >= chunk.start_index,
        "enote scanning (align block ids): chunk start index exceeds the post-alignment block (bug).");
    CHECK_AND_ASSERT_THROW_MES(alignment_marker_inout.block_index + 1 - chunk.start_index <=
            chunk.block_ids.size(),
        "enote scanning (align block ids): the alignment range is larger than the chunk's block range (bug).");

    // 3. crop the chunk block ids
    scanned_block_ids_cropped_inout.clear();
    scanned_block_ids_cropped_inout.insert(scanned_block_ids_cropped_inout.end(),
        std::next(chunk.block_ids.begin(), alignment_marker_inout.block_index + 1 - chunk.start_index),
        chunk.block_ids.end());
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static ScanStatus process_ledger_onchain_pass(const std::uint64_t first_contiguity_index,
    EnoteScanProcessLedger &scan_process_inout,
    EnoteStoreUpdater &enote_store_updater_inout,
    ChainContiguityMarker &contiguity_marker_inout)
{
    // 1. get new chunks until we encounter an empty chunk (or detect a reorg)
    EnoteScanningChunkLedgerV1 new_onchain_chunk;
    scan_process_inout.get_onchain_chunk(new_onchain_chunk);
    std::vector<rct::key> scanned_block_ids_cropped;

    while (!chunk_is_empty(new_onchain_chunk))
    {
        // a. set alignment marker for before the chunk has been processed (assume we always start aligned)
        // - alignment means a chunk's block id matches the enote store's block id at the alignment block index
        ChainContiguityMarker alignment_marker{contiguity_marker_inout};

        // b. validate chunk semantics (this should check all array bounds to prevent out-of-range accesses below)
        check_v1_enote_scan_chunk_ledger_semantics_v1(new_onchain_chunk, contiguity_marker_inout.block_index);

        // c. check if this chunk is contiguous with the contiguity marker
        // - if not contiguous, then there must have been a reorg, so we need to rescan
        const ScanStatus scan_status{
                get_scan_status(contiguity_marker_inout,
                    new_onchain_chunk,
                    first_contiguity_index,
                    contiguity_marker_inout.block_index)
            };

        if (scan_status != ScanStatus::SUCCESS)
            return scan_status;

        // d. align the chunk's block ids with the enote store
        // - update the point of alignment if this chunk overlaps with blocks known by the enote store
        // - crop the chunk's block ids to only include block ids unknown to the enote store
        align_block_ids(enote_store_updater_inout, new_onchain_chunk, alignment_marker, scanned_block_ids_cropped);

        // e. consume the chunk if it's not empty
        // - if the chunk is empty after aligning, that means our enote store already knows about the entire span
        //   of the chunk; we don't want to pass the chunk in, because there may be blocks in the NEXT chunk that
        //   our enote store also knows about; we don't want the enote store to think it needs to roll back its state
        //   to the top of this chunk
        if (scanned_block_ids_cropped.size() > 0)
        {
            enote_store_updater_inout.consume_onchain_chunk(new_onchain_chunk.basic_records_per_tx,
                new_onchain_chunk.contextual_key_images,
                alignment_marker.block_index + 1,
                alignment_marker.block_id ? *(alignment_marker.block_id) : rct::zero(),
                scanned_block_ids_cropped);
        }

        // f. set contiguity marker to last block of this chunk
        contiguity_marker_inout.block_index = new_onchain_chunk.start_index + new_onchain_chunk.block_ids.size() - 1;
        contiguity_marker_inout.block_id    = new_onchain_chunk.block_ids.back();

        // g. get next chunk
        scan_process_inout.get_onchain_chunk(new_onchain_chunk);
    }

    // 2. verify that the last chunk obtained is an empty chunk representing the top of the current blockchain
    CHECK_AND_ASSERT_THROW_MES(new_onchain_chunk.block_ids.size() == 0,
        "process ledger for onchain pass: final chunk does not have zero block ids as expected.");

    // 3. check if the scan process is aborted
    // - when a scan process is aborted, the empty chunk returned may not represent the end of the chain, so we don't
    //   want to consume that chunk
    // - note: we don't check if aborted during the non-empty chunk loop because the process could be aborted after
    //   the chunk was acquired
    if (scan_process_inout.is_aborted())
        return ScanStatus::ABORTED;

    // 4. verify that our termination chunk is contiguous with the chunks received so far
    // - this can fail if a reorg dropped below our contiguity marker without replacing the dropped blocks, causing the
    //   first chunk obtained after the reorg to be this empty termination chunk
    // note: this test won't fail if the chain's top index is below our contiguity marker when our contiguity marker has
    //       an unspecified block id; we don't care if the top index is lower than our scanning 'backstop' (i.e.
    //       lowest point in our enote store) when we haven't actually scanned any blocks
    const ScanStatus scan_status{
            get_scan_status(contiguity_marker_inout,
                new_onchain_chunk,
                first_contiguity_index,
                new_onchain_chunk.start_index - 1)
        };

    if (scan_status != ScanStatus::SUCCESS)
        return scan_status;

    // 5. final update for our enote store
    // - we need to update with the termination chunk in case a reorg popped blocks, so the enote store can roll back
    //   its state
    enote_store_updater_inout.consume_onchain_chunk({},
        {},
        contiguity_marker_inout.block_index + 1,
        contiguity_marker_inout.block_id ? *(contiguity_marker_inout.block_id) : rct::zero(),
        {});

    return ScanStatus::SUCCESS;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static ScanStatus process_ledger_unconfirmed_pass(EnoteScanProcessLedger &scan_process_inout,
    EnoteStoreUpdater &enote_store_updater_inout)
{
    // 1. get unconfirmed chunk
    EnoteScanningChunkNonLedgerV1 unconfirmed_chunk;
    scan_process_inout.get_unconfirmed_chunk(unconfirmed_chunk);

    // 2. check if the scan process was aborted
    // - always consume non-empty chunks (it's possible for a scan process to be aborted after acquiring a chunk)
    // - don't consume empty chunks when aborted because they may not represent the real state of the unconfirmed cache
    if (chunk_is_empty(unconfirmed_chunk) && scan_process_inout.is_aborted())
        return ScanStatus::ABORTED;

    // 3. consume the chunk
    enote_store_updater_inout.consume_nonledger_chunk(SpEnoteOriginStatus::UNCONFIRMED,
        unconfirmed_chunk.basic_records_per_tx,
        unconfirmed_chunk.contextual_key_images);

    return ScanStatus::SUCCESS;
}
//-------------------------------------------------------------------------------------------------------------------
// IMPORTANT: chunk processing can't be parallelized since key image checks are sequential/cumulative
// - 'scanning_context_inout' can internally collect chunks in parallel
//-------------------------------------------------------------------------------------------------------------------
static ScanStatus process_ledger_for_full_refresh(const std::uint64_t max_chunk_size,
    ChainContiguityMarker contiguity_marker,
    EnoteScanningContextLedger &scanning_context_inout,
    EnoteStoreUpdater &enote_store_updater_inout)
{
    // 1. save the initial index of our existing known contiguous chain
    const std::uint64_t first_contiguity_index{contiguity_marker.block_index};

    // 2. create the scan process (initiates the scan process on construction)
    EnoteScanProcessLedger scan_process{first_contiguity_index + 1, max_chunk_size, scanning_context_inout};

    // 3. on-chain initial scanning pass
    const ScanStatus scan_status_first_onchain_pass{
        process_ledger_onchain_pass(first_contiguity_index,
            scan_process,
            enote_store_updater_inout,
            contiguity_marker)
        };

    if (scan_status_first_onchain_pass != ScanStatus::SUCCESS)
        return scan_status_first_onchain_pass;

    // 4. unconfirmed scanning pass
    const ScanStatus scan_status_unconfirmed_pass{
            process_ledger_unconfirmed_pass(scan_process, enote_store_updater_inout)
        };

    if (scan_status_unconfirmed_pass != ScanStatus::SUCCESS)
        return scan_status_unconfirmed_pass;

    // 5. on-chain follow-up pass
    // rationale:
    // - blocks may have been added between the initial on-chain pass and the unconfirmed pass, and those blocks may
    //   contain txs not seen by the unconfirmed pass (i.e. sneaky txs)
    // - we want scan results to be chronologically contiguous (it is better for the unconfirmed scan results to be stale
    //   than the on-chain scan results)
    return process_ledger_onchain_pass(first_contiguity_index,
        scan_process,
        enote_store_updater_inout,
        contiguity_marker);
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
void check_v1_enote_scan_chunk_ledger_semantics_v1(const EnoteScanningChunkLedgerV1 &onchain_chunk,
    const std::uint64_t expected_prefix_index)
{
    // 1. misc. checks
    CHECK_AND_ASSERT_THROW_MES(onchain_chunk.start_index - 1 == expected_prefix_index,
        "enote scan chunk semantics check (ledger): chunk range doesn't start at expected prefix index.");

    const std::uint64_t num_blocks_in_chunk{onchain_chunk.block_ids.size()};
    CHECK_AND_ASSERT_THROW_MES(num_blocks_in_chunk >= 1,
        "enote scan chunk semantics check (ledger): chunk has no blocks.");    

    check_enote_scan_chunk_map_semantics_v1(onchain_chunk.basic_records_per_tx,
        onchain_chunk.contextual_key_images,
        SpEnoteOriginStatus::ONCHAIN,
        SpEnoteSpentStatus::SPENT_ONCHAIN);

    // 2. get start and end block indices
    // - start block = prefix block + 1
    const std::uint64_t allowed_lowest_index{onchain_chunk.start_index};
    // - end block
    const std::uint64_t allowed_heighest_index{onchain_chunk.start_index + num_blocks_in_chunk - 1};

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
bool refresh_enote_store_ledger(const RefreshLedgerEnoteStoreConfig &config,
    EnoteScanningContextLedger &scanning_context_inout,
    EnoteStoreUpdater &enote_store_updater_inout)
{
    // make scan attempts until succeeding or encountering an error
    ScanStatus scan_status{ScanStatus::NEED_FULLSCAN};
    std::size_t partialscan_attempts{0};
    std::size_t fullscan_attempts{0};

    while (scan_status == ScanStatus::NEED_PARTIALSCAN ||
        scan_status == ScanStatus::NEED_FULLSCAN)
    {
        // 1. get index of the first block to scan
        const std::uint64_t start_scan_index{
                get_start_scan_index(config.reorg_avoidance_depth,
                    fullscan_attempts,
                    enote_store_updater_inout.refresh_index(),
                    enote_store_updater_inout.desired_first_block())
            };

        // 2. set initial contiguity marker
        // - this starts as the prefix of the first block to scan, and should either be known to the enote store
        //   updater or have an unspecified block id
        ChainContiguityMarker contiguity_marker;
        set_initial_contiguity_marker(enote_store_updater_inout, start_scan_index, contiguity_marker);

        // 3. record the scan attempt
        if (scan_status == ScanStatus::NEED_PARTIALSCAN)
            ++partialscan_attempts;
        else if (scan_status == ScanStatus::NEED_FULLSCAN)
            ++fullscan_attempts;

        CHECK_AND_ASSERT_THROW_MES(fullscan_attempts < 50,
            "refresh ledger for enote store: fullscan attempts exceeded 50 (sanity check fail).");

        // 4. fail if we have exceeded the max number of partial scanning attempts (i.e. too many reorgs were detected,
        //    so now we abort)
        if (partialscan_attempts > config.max_partialscan_attempts)
        {
            scan_status = ScanStatus::FAIL;
            break;
        }

        // 5. process the ledger
        try
        {
            scan_status = process_ledger_for_full_refresh(config.max_chunk_size,
                contiguity_marker,
                scanning_context_inout,
                enote_store_updater_inout);
        }
        catch (...) { scan_status = ScanStatus::FAIL; }
    }

    if (scan_status == ScanStatus::FAIL)
        LOG_ERROR("refresh ledger for enote store: refreshing failed!");
    else if (scan_status == ScanStatus::ABORTED)
        LOG_ERROR("refresh ledger for enote store: refreshing aborted!");
    else if (scan_status != ScanStatus::SUCCESS)
        LOG_ERROR("refresh ledger for enote store: refreshing failed (unknown failure)!");

    return scan_status == ScanStatus::SUCCESS;
}
//-------------------------------------------------------------------------------------------------------------------
bool refresh_enote_store_offchain(const EnoteFindingContextOffchain &enote_finding_context,
    EnoteStoreUpdater &enote_store_updater_inout)
{
    // 1. get an offchain scan chunk
    EnoteScanningChunkNonLedgerV1 offchain_chunk;
    try
    {
        enote_finding_context.get_offchain_chunk(offchain_chunk);
        check_v1_enote_scan_chunk_nonledger_semantics_v1(offchain_chunk,
            SpEnoteOriginStatus::OFFCHAIN,
            SpEnoteSpentStatus::SPENT_OFFCHAIN);
    }
    catch (...)
    {
        LOG_ERROR("refresh offchain for enote store: get chunk failed.");
        return false;
    }

    // 2. consume the chunk
    try
    {
        enote_store_updater_inout.consume_nonledger_chunk(SpEnoteOriginStatus::OFFCHAIN,
            offchain_chunk.basic_records_per_tx,
            offchain_chunk.contextual_key_images);
    } catch (...)
    {
        LOG_ERROR("refresh offchain for enote store: consume chunk failed.");
        return false;
    }

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace sp
