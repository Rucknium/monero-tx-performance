// Copyright (c) 2021, The Monero Project
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

// Mock ledger context: for testing


#pragma once

//local headers
#include "crypto/crypto.h"
#include "ringct/rctOps.h"
#include "ringct/rctTypes.h"
#include "sp_crypto_utils.h"
#include "tx_component_types.h"

//third party headers
#include <boost/thread/shared_mutex.hpp>

//standard headers
#include <map>
#include <tuple>
#include <unordered_set>
#include <vector>

//forward declarations
namespace sp
{
    struct SpEnoteV1;
    struct SpTxSquashedV1;
}


namespace sp
{

class MockLedgerContext final
{
public:
    /**
    * brief: get_chain_height - get current chain height
    *   - returns uint64{-1} if there are no blocks
    * return: current chain height (num blocks - 1)
    */
    std::uint64_t get_chain_height() const;
    /**
    * brief: key_image_exists_onchain_v1 - checks if a Seraphis linking tag (key image) exists in the ledger
    * param: key_image -
    * return: true/false on check result
    */
    bool key_image_exists_offchain_v1(const crypto::key_image &key_image) const;
    bool key_image_exists_unconfirmed_v1(const crypto::key_image &key_image) const;
    bool key_image_exists_onchain_v1(const crypto::key_image &key_image) const;
    /**
    * brief: get_reference_set_proof_elements_v1 - gets Seraphis squashed enotes stored in the ledger
    * param: indices -
    * outparam: proof_elements_out - {squashed enote}
    */
    void get_reference_set_proof_elements_v1(const std::vector<std::uint64_t> &indices,
        rct::keyV &proof_elements_out) const;
    /**
    * brief: min_enote_index - lowest index of an enote in the ledger
    *   TODO: version this somehow?
    * param: tx_to_add -
    * return: lowest enote index (defaults to 0 if no enotes)
    */
    std::uint64_t min_enote_index() const;
    /**
    * brief: max_enote_index - highest index of an enote in the ledger
    *   TODO: version this somehow?
    * return: highest enote index (defaults to std::uint64_t::max if no enotes)
    */
    std::uint64_t max_enote_index() const;
    /**
    * brief: num_enotes - number of enotes in the ledger
    *   TODO: version this somehow?
    * return: number of enotes in the ledger
    */
    std::uint64_t num_enotes() const { return max_enote_index() - min_enote_index() + 1; }
    /**
    * brief: try_add_unconfirmed_tx_v1 - try to add a full transaction to the 'unconfirmed' tx cache
    *   - fails if there are key image duplicates with: unconfirmed, onchain
    *   - auto-removes any offchain entries that have overlapping key images with this tx
    * param: tx -
    * return: true if adding succeeded
    */
    bool try_add_unconfirmed_tx_v1(const SpTxSquashedV1 &tx);
    /**
    * brief: commit_unconfirmed_cache_v1 - move all unconfirmed txs onto the chain in a new block, with new mock coinbase tx
    *   - clears the unconfirmed tx cache
    *   - todo: use a real coinbase tx instead, with height that is expected to match the next block height (try commit)
    * param: mock_coinbase_input_context -
    * param: mock_coinbase_tx_supplement -
    * param: mock_coinbase_output_enotes -
    * return: block height of newly added block
    */
    std::uint64_t commit_unconfirmed_txs_v1(const rct::key &mock_coinbase_input_context,
        SpTxSupplementV1 mock_coinbase_tx_supplement,
        std::vector<SpEnoteV1> mock_coinbase_output_enotes);
    /**
    * brief: remove_tx_from_unconfirmed_cache - remove a tx from the unconfirmed cache
    * param: tx_id - tx id of tx to remove
    */
    void remove_tx_from_unconfirmed_cache(const rct::key &tx_id);
    /**
    * brief: clear_unconfirmed_cache - remove all data stored in unconfirmed cache
    */
    void clear_unconfirmed_cache();
    /**
    * brief: pop_chain_at_height - remove all blocks >= the specified block height from the chain
    * param: pop_height - first block to pop from the chain
    * return: number of blocks popped
    */
    std::uint64_t pop_chain_at_height(const std::uint64_t pop_height);
    /**
    * brief: pop_blocks - remove a specified number of blocks from the chain
    * param: num_blocks - number of blocks to remove
    * return: number of blocks popped
    */
    std::uint64_t pop_blocks(const std::size_t num_blocks);

private:
    /// implementations of the above, without internally locking the ledger mutex (all expected to be no-fail)
    bool key_image_exists_unconfirmed_v1_impl(const crypto::key_image &key_image) const;
    bool key_image_exists_onchain_v1_impl(const crypto::key_image &key_image) const;
    bool try_add_unconfirmed_coinbase_v1_impl(const rct::key &tx_id,
        const rct::key &input_context,
        SpTxSupplementV1 tx_supplement,
        std::vector<SpEnoteV1> output_enotes);
    bool try_add_unconfirmed_tx_v1_impl(const SpTxSquashedV1 &tx);
    std::uint64_t commit_unconfirmed_txs_v1_impl(const rct::key &mock_coinbase_input_context,
        SpTxSupplementV1 mock_coinbase_tx_supplement,
        std::vector<SpEnoteV1> mock_coinbase_output_enotes);
    void remove_tx_from_unconfirmed_cache_impl(const rct::key &tx_id);
    void clear_unconfirmed_cache_impl();
    std::uint64_t pop_chain_at_height_impl(const std::uint64_t pop_height);
    std::uint64_t pop_blocks_impl(const std::size_t num_blocks);

    /// context mutex (mutable for use in const member functions)
    mutable boost::shared_mutex m_context_mutex;


    //// UNCONFIRMED TXs

    /// Seraphis key images
    std::unordered_set<crypto::key_image> m_unconfirmed_sp_key_images;
    /// map of tx key images
    std::map<
        sortable_key,     // tx id
        std::vector<crypto::key_image>  // key images in tx
    > m_unconfirmed_tx_key_images;
    /// map of tx outputs
    std::map<
        sortable_key,     // tx id
        std::tuple<       // tx output contents
            rct::key,                // input context
            SpTxSupplementV1,        // tx supplement
            std::vector<SpEnoteV1>   // output enotes
        >
    > m_unconfirmed_tx_output_contents;


    //// ON-CHAIN BLOCKS & TXs

    /// Seraphis key images
    std::unordered_set<crypto::key_image> m_sp_key_images;
    /// map of tx key images
    std::map<
        std::uint64_t,      // block height
        std::map<
            sortable_key,   // tx id
            std::vector<crypto::key_image>  // key images in tx
        >
    > m_blocks_of_tx_key_images;
    /// Seraphis squashed enotes (mapped to output index)
    std::map<std::uint64_t, rct::key> m_sp_squashed_enotes;
    /// map of accumulated output counts
    std::map<
        std::uint64_t,  // block height
        std::uint64_t   // total number of enotes including those in this block
    > m_accumulated_output_counts;
    /// map of tx outputs
    std::map<
        std::uint64_t,        // block height
        std::map<
            sortable_key,     // tx id
            std::tuple<       // tx output contents
                rct::key,                // input context
                SpTxSupplementV1,        // tx supplement
                std::vector<SpEnoteV1>   // output enotes
            >
        >
    > m_blocks_of_tx_output_contents;
    /// map of block IDs
    std::map<
        std::uint64_t,  // block height
        rct::key        // block ID
    > m_block_ids;
};

bool try_add_tx_to_ledger(const SpTxSquashedV1 &tx_to_add, MockLedgerContext &ledger_context_inout);

} //namespace sp
