#pragma once

#include "crypto/hash.h"
#include <cstdint>
#include <deque>
#include <map>

class Cache
{
  public:
    Cache(uint64_t max_cached_checkpoints,
        uint64_t refresh_height,
        uint64_t max_separation,
        uint64_t num_unprunable,
        uint64_t window_size)
        : m_checkpoints{}, m_max_cached_checkpoints(max_cached_checkpoints), m_refresh_height(refresh_height),
          m_max_separation(max_separation), m_num_unprunable(num_unprunable), m_window_size(window_size)
    {
        assert(max_cached_checkpoints > num_unprunable && num_unprunable >= 2 && "The first 2 blocks are not pruned anyway.");
        assert(window_size > 3 && "window_size has to be greater than 3.");
    }

    uint64_t get_nearest_block_height_clampdown(const uint64_t test_height);
    uint64_t stored_checkpoints()
    {
        return m_checkpoints.size();
    }

    void insert_new_block_ids(const uint64_t first_block_height, const std::vector<crypto::hash> &block_ids);

  private:
    std::map<uint64_t, crypto::hash> m_checkpoints;
    uint64_t m_max_cached_checkpoints;
    uint64_t m_refresh_height;
    uint64_t m_max_separation;
    uint64_t m_num_unprunable;
    uint64_t m_window_size;

    void clean_prunable_checkpoints();
    bool should_prune(const std::deque<uint64_t> &window, const uint64_t chain_top);
};
