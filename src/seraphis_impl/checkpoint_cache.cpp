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

//paired header
#include "checkpoint_cache.h"

//local headers
#include "misc_log_ex.h"
#include "ringct/rctOps.h"
#include "ringct/rctTypes.h"
#include "seraphis_crypto/math_utils.h"

//third party headers

//standard headers
#include <deque>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis_impl"

namespace sp
{
//-------------------------------------------------------------------------------------------------------------------
CheckpointCache::CheckpointCache(const std::uint64_t min_checkpoint_index,
    const std::uint64_t max_separation,
    const std::uint64_t num_unprunable,
    const std::uint64_t density_factor) :
        m_min_checkpoint_index{min_checkpoint_index},
        m_max_separation{max_separation},
        m_num_unprunable{num_unprunable},
        m_density_factor{density_factor}
{
    CHECK_AND_ASSERT_THROW_MES(m_max_separation < math::uint_pow(2, 32),
        "checkpoint cache (constructor): max_separation must be < 2^32.");  //heuristic to avoid overflow issues
    CHECK_AND_ASSERT_THROW_MES(m_density_factor >= 1,
        "checkpoint cache (constructor): density_factor must be >= 1.");
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t CheckpointCache::top_block_index() const
{
    if (num_stored_checkpoints() == 0)
        return -1;

    return m_checkpoints.crbegin()->first;
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t CheckpointCache::bottom_block_index() const
{
    if (num_stored_checkpoints() == 0)
        return -1;

    return m_checkpoints.cbegin()->first;
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t CheckpointCache::get_next_block_index(const std::uint64_t test_index) const
{
    // 1. get closest checkpoint > test index
    auto test_checkpoint = m_checkpoints.upper_bound(test_index);

    // 2. edge condition: no checkpoints above test index
    if (test_checkpoint == m_checkpoints.cend())
        return -1;

    // 3. return next block index
    return test_checkpoint->first;
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t CheckpointCache::get_nearest_block_index_clampdown(const std::uint64_t test_index) const
{
    // get the block index of the closest checkpoint <= the test index

    // 1. early return if no checkpoints
    if (this->num_stored_checkpoints() == 0)
        return -1;

    // 2. get closest checkpoint > test index
    auto test_checkpoint = m_checkpoints.upper_bound(test_index);

    // 3. edge condition: if test index >= highest checkpoint, return the highest checkpoint
    if (test_checkpoint == m_checkpoints.cend())
        return m_checkpoints.crbegin()->first;

    // 4. edge condition: if test index < lowest checkpoint, return failure
    if (test_checkpoint == m_checkpoints.cbegin())
        return -1;

    // 5. normal case: there is a checkpoint <= the test index
    return (--test_checkpoint)->first;
}
//-------------------------------------------------------------------------------------------------------------------
bool CheckpointCache::try_get_block_id(const std::uint64_t block_index, rct::key &block_id_out) const
{
    // 1. check if the index is known
    const auto checkpoint = m_checkpoints.find(block_index);
    if (checkpoint == m_checkpoints.end())
        return false;

    // 2. return the block id
    block_id_out = checkpoint->second;
    return true;
}
//-------------------------------------------------------------------------------------------------------------------
void CheckpointCache::insert_new_block_ids(const std::uint64_t first_block_index,
    const std::vector<rct::key> &new_block_ids)
{
    // 1. get index into new_block_ids at first block that overlaps with [min index, +inf)
    // - we will ignore block ids below our min index
    const std::uint64_t ids_index_offset{
            first_block_index < m_min_checkpoint_index
            ? m_min_checkpoint_index - first_block_index
            : 0
        };

    // 2. remove checkpoints in range [start of blocks to insert, end)
    // - we always crop checkpoints even if the new block ids are all below our min index
    m_checkpoints.erase(
            m_checkpoints.lower_bound(first_block_index + ids_index_offset),
            m_checkpoints.end()
        );

    // 3. insert new ids
    for (std::uint64_t i{ids_index_offset}; i < new_block_ids.size(); ++i)
        m_checkpoints[first_block_index + i] = new_block_ids[i];

    // 4. prune excess checkpoints
    this->prune_checkpoints();
}
//-------------------------------------------------------------------------------------------------------------------
// CHECKPOINT CACHE INTERNAL
//-------------------------------------------------------------------------------------------------------------------
std::deque<std::uint64_t>::const_iterator CheckpointCache::get_window_prune_candidate(
    const std::deque<std::uint64_t> &window) const
{
    // return the lowest element
    CHECK_AND_ASSERT_THROW_MES(window.size() > 0,
        "checkpoint cache (get window prune candidate): window size is zero.");
    auto it = window.begin();
    std::advance(it, window.size() / 2);
    return it;
}
//-------------------------------------------------------------------------------------------------------------------
// CHECKPOINT CACHE INTERNAL
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t CheckpointCache::expected_checkpoint_density_inv(const std::uint64_t distance_from_highest_prunable) const
{
    // expected density = density_factor/distance -[invert]-> distance/density_factor
    // - we return the inverted density in order to only deal in integers
    if ((m_density_factor == 0) ||
        (distance_from_highest_prunable < m_density_factor))
        return 1;
    return distance_from_highest_prunable/m_density_factor;
};
//-------------------------------------------------------------------------------------------------------------------
// CHECKPOINT CACHE INTERNAL
//-------------------------------------------------------------------------------------------------------------------
bool CheckpointCache::window_is_prunable(const std::deque<std::uint64_t> &window,
    const std::uint64_t max_candidate_index) const
{
    // 1. sanity checks
    CHECK_AND_ASSERT_THROW_MES(window.front() >= window.back(),
        "checkpoint cache (should prune window): window range is invalid.");

    // 2. get the window's prune candidate in the window
    const auto prune_candidate_it{this->get_window_prune_candidate(window)};
    CHECK_AND_ASSERT_THROW_MES(prune_candidate_it != window.end(),
        "checkpoint cache (should prune window): could not get prune candidate.");

    // 3. window is not prunable if its candidate is above the max candidate index
    const std::uint64_t prune_candidate{*prune_candidate_it};
    if (prune_candidate > max_candidate_index)
        return false;

    CHECK_AND_ASSERT_THROW_MES(prune_candidate <= window.front() &&
            prune_candidate >= window.back(),
        "checkpoint cache (should prune window): prune candidate outside window range.");

    // 4. don't prune if our prune candidate is in the 'don't prune' range
    if (prune_candidate + m_num_unprunable > max_candidate_index)
        return false;

    // 5. don't prune if our density is <= 1/max_separation
    // - subtract 1 to account for the number of deltas in the window range
    const std::uint64_t window_range{window.front() - window.back()};
    if (window_range >= (window.size() - 1) * m_max_separation)
        return false;

    // 6. prune candidate's distance from the highest prunable element
    const std::uint64_t distance_from_highest_prunable{(max_candidate_index - m_num_unprunable) - prune_candidate};

    // 7. expected density at this distance from the top (inverted)
    const std::uint64_t expected_density_inv{this->expected_checkpoint_density_inv(distance_from_highest_prunable)};

    // 8. test the expected density
    // - subtract 1 to account for the number of deltas in the window range
    if (window_range >= (window.size() - 1) * expected_density_inv)
        return false;

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
// CHECKPOINT CACHE INTERNAL
//-------------------------------------------------------------------------------------------------------------------
void CheckpointCache::prune_checkpoints()
{
    // 1. sanity checks
    if (this->num_stored_checkpoints() < m_num_unprunable)
        return;

    // 2. highest checkpoint index
    const std::uint64_t highest_checkpoint_index{m_checkpoints.rbegin()->first};

    // 3. initialize window with simulated elements above our highest checkpoint
    // - window is sorted from highest to lowest
    std::deque<std::uint64_t> window;

    for (std::uint64_t window_index{0}; window_index < m_window_size; ++window_index)
        window.push_front(highest_checkpoint_index + window_index);

    // 4. slide the window from our highest checkpoint to our lowest checkpoint, pruning elements as we go
    for (auto checkpoint_it = m_checkpoints.rbegin(); checkpoint_it != m_checkpoints.rend();)
    {
        // a. insert this checkpoint to our window (it is the lowest index in our window)
        window.push_back(checkpoint_it->first);

        // b. early-increment the iterator so it is ready for whatever happens next
        ++checkpoint_it;

        // c. skip to next checkpoint if our window is too small
        if (window.size() < m_window_size)
            continue;

        // d. trim the highest indices in our window
        while (window.size() > m_window_size)
            window.pop_front();

        // e. skip to next checkpoint if this window is not prunable
        if (!this->window_is_prunable(window, highest_checkpoint_index))
            continue;

        // f. get window element to prune
        const auto window_prune_element{this->get_window_prune_candidate(window)};
        CHECK_AND_ASSERT_THROW_MES(window_prune_element != window.end(),
            "checkpoint cache (pruning checkpoints): could not get prune candidate.");

        // g. remove the window element from our checkpoints (if it exists)
        // - reverse iterators store the next element's iterator, so we need to do a little dance to avoid iterator
        //   invalidation
        if (checkpoint_it != m_checkpoints.rend() &&
            checkpoint_it.base()->first == *window_prune_element)
        {
            ++checkpoint_it;
            m_checkpoints.erase(*window_prune_element);
            --checkpoint_it;
        }
        else if (checkpoint_it == m_checkpoints.rend())
        {
            m_checkpoints.erase(*window_prune_element);
            checkpoint_it = std::reverse_iterator<decltype(m_checkpoints)::iterator>(m_checkpoints.begin());
        }
        else
            m_checkpoints.erase(*window_prune_element);

        // i. remove the pruned element from our window
        window.erase(window_prune_element);
    }
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace sp
