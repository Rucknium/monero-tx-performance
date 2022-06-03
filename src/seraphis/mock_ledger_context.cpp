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

//paired header
#include "mock_ledger_context.h"

//local headers
#include "crypto/crypto.h"
#include "misc_log_ex.h"
#include "ringct/rctTypes.h"
#include "sp_core_enote_utils.h"
#include "tx_component_types.h"
#include "txtype_squashed_v1.h"

//third party headers

//standard headers
#include <mutex>
#include <vector>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis"

namespace sp
{
//-------------------------------------------------------------------------------------------------------------------
bool MockLedgerContext::key_image_exists_v1(const crypto::key_image &key_image) const
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    return key_image_exists_v1_impl(key_image);
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::get_reference_set_proof_elements_v1(const std::vector<std::uint64_t> &indices,
    rct::keyV &proof_elements_out) const
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    // gets squashed enotes
    proof_elements_out.clear();
    proof_elements_out.reserve(indices.size());

    for (const std::uint64_t index : indices)
    {
        CHECK_AND_ASSERT_THROW_MES(index < m_sp_squashed_enotes.size(), "Tried to get squashed enote that doesn't exist.");
        proof_elements_out.emplace_back(m_sp_squashed_enotes.at(index));
    }
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t MockLedgerContext::min_enote_index() const
{
    return 0;
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t MockLedgerContext::max_enote_index() const
{
    return m_sp_enotes.size() - 1;
}
//-------------------------------------------------------------------------------------------------------------------
bool MockLedgerContext::try_add_transaction_sp_squashed_v1(const SpTxSquashedV1 &tx_to_add)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    // check that key images (linking tags) can all be added
    for (const auto &input_image : tx_to_add.m_input_images)
    {
        if (key_image_exists_v1_impl(input_image.m_core.m_key_image))
            return false;
    }
    // add key images
    for (const auto &input_image : tx_to_add.m_input_images)
        this->add_key_image_v1_impl(input_image.m_core.m_key_image);

    // add new enotes
    for (const auto &output_enote : tx_to_add.m_outputs)
        this->add_enote_v1_impl(output_enote);

    // note: for mock ledger, don't store the whole tx
    return true;
}
//-------------------------------------------------------------------------------------------------------------------
bool MockLedgerContext::try_add_key_image_v1(const crypto::key_image &key_image)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    if (key_image_exists_v1_impl(key_image))
        return false;

    add_key_image_v1_impl(key_image);
    return true;
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t MockLedgerContext::add_enote_v1(const SpEnoteV1 &enote)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    return add_enote_v1_impl(enote);
}
//-------------------------------------------------------------------------------------------------------------------
// internal implementation details
//-------------------------------------------------------------------------------------------------------------------
bool MockLedgerContext::key_image_exists_v1_impl(const crypto::key_image &key_image) const
{
    return m_sp_key_images.find(key_image) != m_sp_key_images.end();
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::add_key_image_v1_impl(const crypto::key_image &key_image)
{
    CHECK_AND_ASSERT_THROW_MES(!key_image_exists_v1_impl(key_image),
        "Tried to add key image (linking tag) that already exists.");  //extra double sanity check

    m_sp_key_images.insert(key_image);
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t MockLedgerContext::add_enote_v1_impl(const SpEnoteV1 &enote)
{
    const std::size_t current_max_enote_index{max_enote_index()};  //defaults to std::uint64_t::max if no enotes

    // add the enote
    m_sp_enotes[current_max_enote_index + 1] = enote;

    // add the squashed enote
    make_seraphis_squashed_enote_Q(enote.m_core.m_onetime_address,
        enote.m_core.m_amount_commitment,
        m_sp_squashed_enotes[current_max_enote_index + 1]);

    return max_enote_index();
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace sp
