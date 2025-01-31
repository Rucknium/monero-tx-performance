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
#include "mock_sp_txtype_squashed_v1.h"

//local headers
#include "ledger_context.h"
#include "misc_log_ex.h"
#include "mock_ledger_context.h"
#include "mock_sp_base_types.h"
#include "mock_sp_transaction_builder_types.h"
#include "mock_sp_transaction_component_types.h"
#include "mock_sp_transaction_utils.h"
#include "mock_sp_validators.h"
#include "mock_tx_utils.h"
#include "ringct/bulletproofs_plus.h"
#include "ringct/rctTypes.h"
#include "seraphis_crypto_utils.h"

//third party headers

//standard headers
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "mock_tx"

namespace mock_tx
{
//-------------------------------------------------------------------------------------------------------------------
MockTxSpSquashedV1::MockTxSpSquashedV1(const std::vector<MockInputProposalSpV1> &input_proposals,
    const std::size_t max_rangeproof_splits,
    const std::vector<MockDestinationSpV1> &destinations,
    const std::vector<MockMembershipReferenceSetSpV1> &membership_ref_sets,
    const MockTxSpSquashedV1::ValidationRulesVersion validation_rules_version)
{
    CHECK_AND_ASSERT_THROW_MES(input_proposals.size() > 0, "Tried to make tx without any inputs.");
    CHECK_AND_ASSERT_THROW_MES(destinations.size() > 0, "Tried to make tx without any outputs.");
    CHECK_AND_ASSERT_THROW_MES(balance_check_in_out_amnts_sp_v1(input_proposals, destinations),
        "Tried to make tx with unbalanced amounts.");  //TODO: include fee in balance check

    // versioning for proofs
    std::string version_string;
    version_string.reserve(3);
    MockTxSpSquashedV1::get_versioning_string(validation_rules_version, version_string);

    // tx components
    std::vector<MockENoteImageSpV1> input_images;
    std::vector<MockENoteSpV1> outputs;
    std::shared_ptr<MockBalanceProofSpV1> balance_proof;
    std::vector<MockImageProofSpV1> tx_image_proofs;
    std::vector<MockMembershipProofSortableSpV1> tx_membership_proofs_sortable;
    std::vector<MockMembershipProofSpV1> tx_membership_proofs;
    MockSupplementSpV1 tx_supplement;

    // info shuttles for making components
    std::vector<rct::xmr_amount> output_amounts;
    std::vector<crypto::secret_key> output_amount_commitment_blinding_factors;
    std::vector<crypto::secret_key> image_address_masks;
    std::vector<crypto::secret_key> image_amount_masks;

    make_v1_tx_outputs_sp_v1(destinations,
        outputs,
        output_amounts,
        output_amount_commitment_blinding_factors,
        tx_supplement);
    make_v1_tx_images_sp_v2(input_proposals,
        input_images,
        image_address_masks,
        image_amount_masks);
    // the API here around sorting is clumsy and not well thought-out (TODO: improve if this tx variant is to be used)
    std::vector<MockMembershipReferenceSetSpV1> membership_ref_sets_sorted{membership_ref_sets};
    std::vector<MockInputProposalSpV1> input_proposals_sorted{input_proposals};
    sort_tx_inputs_sp_v2(input_images,
        image_address_masks,
        image_amount_masks,
        membership_ref_sets_sorted,
        input_proposals_sorted);  //sort now so range proofs line up with input images
    std::vector<rct::xmr_amount> input_amounts;
    std::vector<crypto::secret_key> input_image_amount_commitment_blinding_factors;
    prepare_input_commitment_factors_for_balance_proof_v1(input_proposals_sorted,
        image_amount_masks,
        input_amounts,
        input_image_amount_commitment_blinding_factors);
    make_v1_tx_balance_proof_sp_v3(input_amounts, //note: must range proof input image commitments in squashed enote model
        output_amounts,
        input_image_amount_commitment_blinding_factors,
        output_amount_commitment_blinding_factors,
        max_rangeproof_splits,
        balance_proof);
    rct::key image_proofs_message{get_tx_image_proof_message_sp_v1(version_string, outputs, tx_supplement)};
    make_v1_tx_image_proofs_sp_v3(input_proposals_sorted,
        input_images,
        image_address_masks,
        image_proofs_message,
        tx_image_proofs);
    make_v1_tx_membership_proofs_sp_v2(membership_ref_sets_sorted,
        image_address_masks,
        image_amount_masks,
        tx_membership_proofs_sortable);
    sort_v1_tx_membership_proofs_sp_v1(input_images, tx_membership_proofs_sortable, tx_membership_proofs);

    *this = MockTxSpSquashedV1{std::move(input_images), std::move(outputs),
        std::move(balance_proof), std::move(tx_image_proofs), std::move(tx_membership_proofs),
        std::move(tx_supplement), MockTxSpSquashedV1::ValidationRulesVersion::ONE};
}
//-------------------------------------------------------------------------------------------------------------------
bool MockTxSpSquashedV1::validate_tx_semantics() const
{
    // validate component counts (num inputs/outputs/etc.)
    if (!validate_mock_tx_sp_semantics_component_counts_v3(m_input_images.size(),
        m_membership_proofs.size(),
        m_image_proofs.size(),
        m_outputs.size(),
        m_supplement.m_output_enote_pubkeys.size(),
        m_balance_proof))
    {
        return false;
    }

    // validate input proof reference set sizes
    if (!validate_mock_tx_sp_semantics_ref_set_size_v1(m_membership_proofs))
    {
        return false;
    }

    // validate linking tag semantics
    if (!validate_mock_tx_sp_semantics_input_images_v1(m_input_images))
    {
        return false;
    }

    // validate membershio proof ref sets and input images are sorted
    if (!validate_mock_tx_sp_semantics_sorting_v1(m_membership_proofs, m_input_images))
    {
        return false;
    }

    // validate memo semantics: none for mockup

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
bool MockTxSpSquashedV1::validate_tx_linking_tags(const std::shared_ptr<const LedgerContext> ledger_context) const
{
    // unspentness proof (key images not in ledger)
    if (!validate_mock_tx_sp_linking_tags_v1(m_input_images, ledger_context))
    {
        return false;
    }

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
bool MockTxSpSquashedV1::validate_tx_amount_balance(const bool defer_batchable) const
{
    if (!validate_mock_tx_sp_amount_balance_v3(m_input_images, m_outputs, m_balance_proof, defer_batchable))
    {
        return false;
    }

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
bool MockTxSpSquashedV1::validate_tx_input_proofs(const std::shared_ptr<const LedgerContext> ledger_context,
    const bool defer_batchable) const
{
    // membership proofs (can be deferred for batching)
    if (!defer_batchable)
    {
        std::vector<const MockMembershipProofSpV1*> membership_proof_ptrs;
        std::vector<const MockENoteImageSpV1*> input_image_ptrs;
        membership_proof_ptrs.reserve(m_membership_proofs.size());
        input_image_ptrs.reserve(m_input_images.size());

        for (const auto &membership_proof : m_membership_proofs)
            membership_proof_ptrs.push_back(&membership_proof);

        for (const auto &input_image : m_input_images)
            input_image_ptrs.push_back(&input_image);

        if (!validate_mock_tx_sp_membership_proofs_v2(membership_proof_ptrs, input_image_ptrs, ledger_context))
        {
            return false;
        }
    }

    // ownership proof (and proof that key images are well-formed)
    std::string version_string;
    version_string.reserve(3);
    this->MockTx::get_versioning_string(version_string);

    rct::key image_proofs_message{
            get_tx_image_proof_message_sp_v1(version_string, m_outputs, m_supplement)
        };

    if (!validate_mock_tx_sp_composition_proofs_v1(m_image_proofs,
        m_input_images,
        image_proofs_message))
    {
        return false;
    }

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
std::size_t MockTxSpSquashedV1::get_size_bytes() const
{
    // doesn't include (compared to a real tx):
    // - ring member references (e.g. indices or explicit copies)
    // - tx fees
    // - memos
    // - miscellaneous serialization bytes
    std::size_t size{0};

    // input images
    size += m_input_images.size() * MockENoteImageSpV1::get_size_bytes();

    // outputs
    size += m_outputs.size() * MockENoteSpV1::get_size_bytes();

    // balance proof
    if (m_balance_proof.get() != nullptr)
        size += m_balance_proof->get_size_bytes();

    // membership proofs
    // - assumes all have the same size
    if (m_membership_proofs.size())
        size += m_membership_proofs.size() * m_membership_proofs[0].get_size_bytes();

    // ownership/key-image-legitimacy proof for all inputs
    // - assumes all have the same size
    if (m_image_proofs.size())
        size += m_image_proofs.size() * m_image_proofs[0].get_size_bytes();

    // extra data in tx
    size += m_supplement.get_size_bytes();

    return size;
}
//-------------------------------------------------------------------------------------------------------------------
template <>
std::shared_ptr<MockTxSpSquashedV1> make_mock_tx<MockTxSpSquashedV1>(const MockTxParamPack &params,
    const std::vector<rct::xmr_amount> &in_amounts,
    const std::vector<rct::xmr_amount> &out_amounts,
    std::shared_ptr<MockLedgerContext> ledger_context_inout)
{
    CHECK_AND_ASSERT_THROW_MES(in_amounts.size() > 0, "Tried to make tx without any inputs.");
    CHECK_AND_ASSERT_THROW_MES(out_amounts.size() > 0, "Tried to make tx without any outputs.");
    CHECK_AND_ASSERT_THROW_MES(balance_check_in_out_amnts(in_amounts, out_amounts),
        "Tried to make tx with unbalanced amounts.");

    // make mock inputs
    // enote, ks, view key stuff, amount, amount blinding factor
    std::vector<MockInputProposalSpV1> input_proposals{gen_mock_sp_input_proposals_v1(in_amounts)};

    // make mock destinations
    // - (in practice) for 2-out tx, need special treatment when making change/dummy destination
    std::vector<MockDestinationSpV1> destinations{gen_mock_sp_destinations_v1(out_amounts)};

    // make mock membership proof ref sets
    std::vector<MockENoteSpV1> input_enotes;
    input_enotes.reserve(input_proposals.size());

    for (const auto &input_proposal : input_proposals)
        input_enotes.emplace_back(input_proposal.m_enote);

    std::vector<MockMembershipReferenceSetSpV1> membership_ref_sets{
            gen_mock_sp_membership_ref_sets_v2(input_enotes,
                params.ref_set_decomp_n,
                params.ref_set_decomp_m,
                ledger_context_inout)
        };

    // make tx
    return std::make_shared<MockTxSpSquashedV1>(input_proposals, params.max_rangeproof_splits, destinations,
        membership_ref_sets, MockTxSpSquashedV1::ValidationRulesVersion::ONE);
}
//-------------------------------------------------------------------------------------------------------------------
template <>
bool validate_mock_txs<MockTxSpSquashedV1>(const std::vector<std::shared_ptr<MockTxSpSquashedV1>> &txs_to_validate,
    const std::shared_ptr<const LedgerContext> ledger_context)
{
    std::vector<const MockMembershipProofSpV1*> membership_proof_ptrs;
    std::vector<const MockENoteImageSpV1*> input_image_ptrs;
    std::vector<const rct::BulletproofPlus*> range_proof_ptrs;
    membership_proof_ptrs.reserve(txs_to_validate.size()*20);  //heuristic... (most tx have 1-2 inputs)
    input_image_ptrs.reserve(txs_to_validate.size()*20);
    range_proof_ptrs.reserve(txs_to_validate.size());

    // prepare for batch-verification
    for (const auto &tx : txs_to_validate)
    {
        if (!tx)
            return false;

        // validate unbatchable parts of tx
        if (!tx->validate(ledger_context, true))
            return false;

        // gather membership proof pieces
        for (const auto &membership_proof : tx->m_membership_proofs)
            membership_proof_ptrs.push_back(&membership_proof);

        for (const auto &input_image : tx->m_input_images)
            input_image_ptrs.push_back(&(input_image));

        // gather range proofs
        const std::shared_ptr<const MockBalanceProofSpV1> balance_proof{tx->m_balance_proof};

        if (balance_proof.get() == nullptr)
            return false;

        for (const auto &range_proof : balance_proof->m_bpp_proofs)
            range_proof_ptrs.push_back(&range_proof);
    }

    // batch verification: collect pippenger data sets
    std::vector<rct::pippenger_prep_data> prep_datas;
    prep_datas.resize(2);

    // membership proofs
    if (!try_get_mock_tx_sp_membership_proofs_v2_validation_data(membership_proof_ptrs,
        input_image_ptrs,
        ledger_context,
        prep_datas[0]))
    {
        return false;
    }

    // range proofs
    if (!rct::try_get_bulletproof_plus_verification_data(range_proof_ptrs, prep_datas[1]))
        return false;

    // batch verify
    if (!sp::check_pippenger_data(prep_datas))
        return false;

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace mock_tx
