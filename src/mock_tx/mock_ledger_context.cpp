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
#include "mock_sp_core_utils.h"
#include "mock_sp_transaction_component_types.h"
#include "mock_sp_txtype_concise_v1.h"
#include "mock_sp_txtype_merge_v1.h"
#include "mock_sp_txtype_plain_v1.h"
#include "mock_sp_txtype_squashed_v1.h"
#include "ringct/rctTypes.h"

//third party headers

//standard headers
#include <mutex>
#include <vector>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "mock_tx"

namespace mock_tx
{
//-------------------------------------------------------------------------------------------------------------------
bool MockLedgerContext::linking_tag_exists_sp_v1(const crypto::key_image &linking_tag) const
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    return linking_tag_exists_sp_v1_impl(linking_tag);
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::get_reference_set_sp_v1(const std::vector<std::size_t> &indices,
    std::vector<MockENoteSpV1> &enotes_out) const
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    std::vector<MockENoteSpV1> enotes_temp;
    enotes_temp.reserve(indices.size());

    for (const std::size_t index : indices)
    {
        CHECK_AND_ASSERT_THROW_MES(index < m_sp_enotes.size(), "Tried to get enote that doesn't exist.");
        enotes_temp.push_back(m_sp_enotes.at(index));
    }

    enotes_out = std::move(enotes_temp);
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::get_reference_set_components_sp_v1(const std::vector<std::size_t> &indices,
    rct::keyM &referenced_enotes_components_out) const
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    rct::keyM referenced_enotes_components_temp;
    referenced_enotes_components_temp.reserve(indices.size());

    for (const std::size_t index : indices)
    {
        CHECK_AND_ASSERT_THROW_MES(index < m_sp_enotes.size(), "Tried to get components of an enote that doesn't exist.");
        referenced_enotes_components_temp.emplace_back(
                rct::keyV{m_sp_enotes.at(index).m_onetime_address, m_sp_enotes.at(index).m_amount_commitment}
            );
    }

    referenced_enotes_components_out = std::move(referenced_enotes_components_temp);
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::get_reference_set_components_sp_v2(const std::vector<std::size_t> &indices,
        rct::keyM &referenced_enotes_components_out) const
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    // gets squashed enotes
    rct::keyM referenced_enotes_components_temp;
    referenced_enotes_components_temp.reserve(indices.size());

    for (const std::size_t index : indices)
    {
        CHECK_AND_ASSERT_THROW_MES(m_sp_squashed_enotes.find(index) != m_sp_squashed_enotes.end(),
            "Tried to get squashed enote that doesn't exist.");
        referenced_enotes_components_temp.emplace_back(
                rct::keyV{m_sp_squashed_enotes.at(index)}
            );
    }

    referenced_enotes_components_out = std::move(referenced_enotes_components_temp);
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::add_transaction_sp_concise_v1(const MockTxSpConciseV1 &tx_to_add)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    // add linking tags
    for (const auto &input_image : tx_to_add.m_input_images)
        this->add_linking_tag_sp_v1_impl(input_image.m_key_image);

    // add new enotes
    for (const auto &output_enote : tx_to_add.m_outputs)
        this->add_enote_sp_v1_impl(output_enote);

    // note: for mock ledger, don't store the whole tx
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::add_transaction_sp_merge_v1(const MockTxSpMergeV1 &tx_to_add)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    // add linking tags
    for (const auto &input_image : tx_to_add.m_input_images)
        this->add_linking_tag_sp_v1_impl(input_image.m_key_image);

    // add new enotes
    for (const auto &output_enote : tx_to_add.m_outputs)
        this->add_enote_sp_v1_impl(output_enote);

    // note: for mock ledger, don't store the whole tx
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::add_transaction_sp_plain_v1(const MockTxSpPlainV1 &tx_to_add)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    // add linking tags
    for (const auto &input_image : tx_to_add.m_input_images)
        this->add_linking_tag_sp_v1_impl(input_image.m_key_image);

    // add new enotes
    for (const auto &output_enote : tx_to_add.m_outputs)
        this->add_enote_sp_v1_impl(output_enote);

    // note: for mock ledger, don't store the whole tx
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::add_transaction_sp_squashed_v1(const MockTxSpSquashedV1 &tx_to_add)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    // add linking tags
    for (const auto &input_image : tx_to_add.m_input_images)
        this->add_linking_tag_sp_v1_impl(input_image.m_key_image);

    // add new enotes
    for (const auto &output_enote : tx_to_add.m_outputs)
        this->add_enote_sp_v2_impl(output_enote);

    // note: for mock ledger, don't store the whole tx
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::add_linking_tag_sp_v1(const crypto::key_image &linking_tag)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    add_linking_tag_sp_v1_impl(linking_tag);
}
//-------------------------------------------------------------------------------------------------------------------
std::size_t MockLedgerContext::add_enote_sp_v1(const MockENoteSpV1 &enote)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    return add_enote_sp_v1_impl(enote);
}
//-------------------------------------------------------------------------------------------------------------------
std::size_t MockLedgerContext::add_enote_sp_v2(const MockENoteSpV1 &enote)
{
    std::lock_guard<std::mutex> lock{m_ledger_mutex};

    return add_enote_sp_v2_impl(enote);
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
bool MockLedgerContext::linking_tag_exists_sp_v1_impl(const crypto::key_image &linking_tag) const
{
    return m_sp_linking_tags.find(linking_tag) != m_sp_linking_tags.end();
}
//-------------------------------------------------------------------------------------------------------------------
void MockLedgerContext::add_linking_tag_sp_v1_impl(const crypto::key_image &linking_tag)
{
    CHECK_AND_ASSERT_THROW_MES(!linking_tag_exists_sp_v1_impl(linking_tag),
        "Tried to add linking tag that already linking_tag_exists_sp_v1.");

    m_sp_linking_tags.insert(linking_tag);
}
//-------------------------------------------------------------------------------------------------------------------
std::size_t MockLedgerContext::add_enote_sp_v1_impl(const MockENoteSpV1 &enote)
{
    m_sp_enotes[m_sp_enotes.size()] = enote;

    return m_sp_enotes.size() - 1;
}
//-------------------------------------------------------------------------------------------------------------------
std::size_t MockLedgerContext::add_enote_sp_v2_impl(const MockENoteSpV1 &enote)
{
    // add the enote
    m_sp_enotes[m_sp_enotes.size()] = enote;

    // add the squashed enote
    seraphis_squashed_enote_Q(enote.m_onetime_address,
        enote.m_amount_commitment,
        m_sp_squashed_enotes[m_sp_enotes.size() - 1]);

    return m_sp_enotes.size() - 1;
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace mock_tx
