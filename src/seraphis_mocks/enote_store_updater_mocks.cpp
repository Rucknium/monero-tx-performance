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
#include "enote_store_updater_mocks.h"

//local headers
#include "crypto/crypto.h"
#include "crypto/x25519.h"
#include "enote_finding_context_mocks.h"
#include "enote_store_mock_v1.h"
#include "ringct/rctTypes.h"
#include "seraphis_core/jamtis_core_utils.h"
#include "seraphis_main/enote_record_types.h"
#include "seraphis_main/enote_scanning.h"
#include "seraphis_main/enote_scanning_utils.h"

//third party headers

//standard headers
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis_mocks"

namespace sp
{
namespace mocks
{
//-------------------------------------------------------------------------------------------------------------------
// Legacy Intermediate
//-------------------------------------------------------------------------------------------------------------------
EnoteStoreUpdaterMockLegacyIntermediate::EnoteStoreUpdaterMockLegacyIntermediate(
    const rct::key &legacy_base_spend_pubkey,
    const crypto::secret_key &legacy_view_privkey,
    const LegacyScanMode legacy_scan_mode,
    SpEnoteStoreMockV1 &enote_store) :
        m_legacy_base_spend_pubkey{legacy_base_spend_pubkey},
        m_legacy_view_privkey{legacy_view_privkey},
        m_legacy_scan_mode{legacy_scan_mode},
        m_enote_store{enote_store}
{}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t EnoteStoreUpdaterMockLegacyIntermediate::refresh_index() const
{
    return m_enote_store.legacy_refresh_index();
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t EnoteStoreUpdaterMockLegacyIntermediate::desired_first_block() const
{
    if (m_legacy_scan_mode == LegacyScanMode::KEY_IMAGES_ONLY)
        return m_enote_store.top_legacy_fullscanned_block_index() + 1;
    else
        return m_enote_store.top_legacy_partialscanned_block_index() + 1;
}
//-------------------------------------------------------------------------------------------------------------------
bool EnoteStoreUpdaterMockLegacyIntermediate::try_get_block_id(const std::uint64_t block_index,
    rct::key &block_id_out) const
{
    if (m_legacy_scan_mode == LegacyScanMode::KEY_IMAGES_ONLY)
        return m_enote_store.try_get_block_id_for_legacy_fullscan(block_index, block_id_out);
    else
        return m_enote_store.try_get_block_id_for_legacy_partialscan(block_index, block_id_out);
}
//-------------------------------------------------------------------------------------------------------------------
void EnoteStoreUpdaterMockLegacyIntermediate::consume_nonledger_chunk(const SpEnoteOriginStatus nonledger_origin_status,
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images)
{
    // 1. process the chunk
    std::unordered_map<rct::key, LegacyContextualIntermediateEnoteRecordV1> found_enote_records;
    std::unordered_map<crypto::key_image, SpEnoteSpentContextV1> found_spent_key_images;

    process_chunk_intermediate_legacy(m_legacy_base_spend_pubkey,
        m_legacy_view_privkey,
        [this](const crypto::key_image &key_image) -> bool
        {
            return this->m_enote_store.has_enote_with_key_image(key_image);
        },
        chunk_basic_records_per_tx,
        chunk_contextual_key_images,
        hw::get_device("default"),
        found_enote_records,
        found_spent_key_images);

    // 2. save the results
    std::list<EnoteStoreEvent> events;
    if (m_legacy_scan_mode == LegacyScanMode::KEY_IMAGES_ONLY)
        m_enote_store.update_with_intermediate_legacy_found_spent_key_images(found_spent_key_images, events);
    else
    {
        m_enote_store.update_with_intermediate_legacy_records_from_nonledger(nonledger_origin_status,
            found_enote_records,
            found_spent_key_images,
            events);
    }
}
//-------------------------------------------------------------------------------------------------------------------
void EnoteStoreUpdaterMockLegacyIntermediate::consume_onchain_chunk(
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images,
    const std::uint64_t first_new_block,
    const rct::key &alignment_block_id,
    const std::vector<rct::key> &new_block_ids)
{
    // 1. process the chunk
    std::unordered_map<rct::key, LegacyContextualIntermediateEnoteRecordV1> found_enote_records;
    std::unordered_map<crypto::key_image, SpEnoteSpentContextV1> found_spent_key_images;

    process_chunk_intermediate_legacy(m_legacy_base_spend_pubkey,
        m_legacy_view_privkey,
        [this](const crypto::key_image &key_image) -> bool
        {
            return this->m_enote_store.has_enote_with_key_image(key_image);
        },
        chunk_basic_records_per_tx,
        chunk_contextual_key_images,
        hw::get_device("default"),
        found_enote_records,
        found_spent_key_images);

    // 2. save the results
    std::list<EnoteStoreEvent> events;
    if (m_legacy_scan_mode == LegacyScanMode::KEY_IMAGES_ONLY)
        m_enote_store.update_with_intermediate_legacy_found_spent_key_images(found_spent_key_images, events);
    else
    {
        m_enote_store.update_with_intermediate_legacy_records_from_ledger(first_new_block,
            alignment_block_id,
            new_block_ids,
            found_enote_records,
            found_spent_key_images,
            events);
    }
}
//-------------------------------------------------------------------------------------------------------------------
// Legacy
//-------------------------------------------------------------------------------------------------------------------
EnoteStoreUpdaterMockLegacy::EnoteStoreUpdaterMockLegacy(const rct::key &legacy_base_spend_pubkey,
    const crypto::secret_key &legacy_spend_privkey,
    const crypto::secret_key &legacy_view_privkey,
    SpEnoteStoreMockV1 &enote_store) :
        m_legacy_base_spend_pubkey{legacy_base_spend_pubkey},
        m_legacy_spend_privkey{legacy_spend_privkey},
        m_legacy_view_privkey{legacy_view_privkey},
        m_enote_store{enote_store}
{}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t EnoteStoreUpdaterMockLegacy::refresh_index() const
{
    return m_enote_store.legacy_refresh_index();
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t EnoteStoreUpdaterMockLegacy::desired_first_block() const
{
    return m_enote_store.top_legacy_fullscanned_block_index() + 1;
}
//-------------------------------------------------------------------------------------------------------------------
bool EnoteStoreUpdaterMockLegacy::try_get_block_id(const std::uint64_t block_index, rct::key &block_id_out) const
{
    return m_enote_store.try_get_block_id_for_legacy_fullscan(block_index, block_id_out);
}
//-------------------------------------------------------------------------------------------------------------------
void EnoteStoreUpdaterMockLegacy::consume_nonledger_chunk(const SpEnoteOriginStatus nonledger_origin_status,
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images)
{
    // 1. process the chunk
    std::unordered_map<rct::key, LegacyContextualEnoteRecordV1> found_enote_records;
    std::unordered_map<crypto::key_image, SpEnoteSpentContextV1> found_spent_key_images;

    process_chunk_full_legacy(m_legacy_base_spend_pubkey,
        m_legacy_spend_privkey,
        m_legacy_view_privkey,
        [this](const crypto::key_image &key_image) -> bool
        {
            return this->m_enote_store.has_enote_with_key_image(key_image);
        },
        chunk_basic_records_per_tx,
        chunk_contextual_key_images,
        hw::get_device("default"),
        found_enote_records,
        found_spent_key_images);

    // 2. save the results
    std::list<EnoteStoreEvent> events;
    m_enote_store.update_with_legacy_records_from_nonledger(nonledger_origin_status,
        found_enote_records,
        found_spent_key_images,
        events);
}
//-------------------------------------------------------------------------------------------------------------------
void EnoteStoreUpdaterMockLegacy::consume_onchain_chunk(
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images,
    const std::uint64_t first_new_block,
    const rct::key &alignment_block_id,
    const std::vector<rct::key> &new_block_ids)
{
    // 1. process the chunk
    std::unordered_map<rct::key, LegacyContextualEnoteRecordV1> found_enote_records;
    std::unordered_map<crypto::key_image, SpEnoteSpentContextV1> found_spent_key_images;

    process_chunk_full_legacy(m_legacy_base_spend_pubkey,
        m_legacy_spend_privkey,
        m_legacy_view_privkey,
        [this](const crypto::key_image &key_image) -> bool
        {
            return this->m_enote_store.has_enote_with_key_image(key_image);
        },
        chunk_basic_records_per_tx,
        chunk_contextual_key_images,
        hw::get_device("default"),
        found_enote_records,
        found_spent_key_images);

    // 2. save the results
    std::list<EnoteStoreEvent> events;
    m_enote_store.update_with_legacy_records_from_ledger(first_new_block,
        alignment_block_id,
        new_block_ids,
        found_enote_records,
        found_spent_key_images,
        events);
}
//-------------------------------------------------------------------------------------------------------------------
// Seraphis Intermediate
//-------------------------------------------------------------------------------------------------------------------
EnoteStoreUpdaterMockSpIntermediate::EnoteStoreUpdaterMockSpIntermediate(const rct::key &jamtis_spend_pubkey,
    const crypto::x25519_secret_key &xk_unlock_amounts,
    const crypto::x25519_secret_key &xk_find_received,
    const crypto::secret_key &s_generate_address,
    SpEnoteStoreMockPaymentValidatorV1 &enote_store) :
        m_jamtis_spend_pubkey{jamtis_spend_pubkey},
        m_xk_unlock_amounts{xk_unlock_amounts},
        m_xk_find_received{xk_find_received},
        m_s_generate_address{s_generate_address},
        m_enote_store{enote_store}
{
    jamtis::make_jamtis_ciphertag_secret(m_s_generate_address, m_s_cipher_tag);

    m_cipher_context = std::make_unique<jamtis::jamtis_address_tag_cipher_context>(m_s_cipher_tag);
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t EnoteStoreUpdaterMockSpIntermediate::refresh_index() const
{
    return m_enote_store.refresh_index();
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t EnoteStoreUpdaterMockSpIntermediate::desired_first_block() const
{
    return m_enote_store.top_block_index() + 1;
}
//-------------------------------------------------------------------------------------------------------------------
bool EnoteStoreUpdaterMockSpIntermediate::try_get_block_id(const std::uint64_t block_index,
    rct::key &block_id_out) const
{
    return m_enote_store.try_get_block_id(block_index, block_id_out);
}
//-------------------------------------------------------------------------------------------------------------------
void EnoteStoreUpdaterMockSpIntermediate::consume_nonledger_chunk(const SpEnoteOriginStatus nonledger_origin_status,
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images)
{
    // 1. process the chunk
    std::unordered_map<rct::key, SpContextualIntermediateEnoteRecordV1> found_enote_records;

    process_chunk_intermediate_sp(m_jamtis_spend_pubkey,
        m_xk_unlock_amounts,
        m_xk_find_received,
        m_s_generate_address,
        *m_cipher_context,
        chunk_basic_records_per_tx,
        found_enote_records);

    // 2. save the results
    std::list<SpPaymentValidatorStoreEvent> events;
    m_enote_store.update_with_sp_records_from_nonledger(nonledger_origin_status, found_enote_records, events);
}
//-------------------------------------------------------------------------------------------------------------------
void EnoteStoreUpdaterMockSpIntermediate::consume_onchain_chunk(
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images,
    const std::uint64_t first_new_block,
    const rct::key &alignment_block_id,
    const std::vector<rct::key> &new_block_ids)
{
    // 1. process the chunk
    std::unordered_map<rct::key, SpContextualIntermediateEnoteRecordV1> found_enote_records;

    process_chunk_intermediate_sp(m_jamtis_spend_pubkey,
        m_xk_unlock_amounts,
        m_xk_find_received,
        m_s_generate_address,
        *m_cipher_context,
        chunk_basic_records_per_tx,
        found_enote_records);

    // 2. save the results
    std::list<SpPaymentValidatorStoreEvent> events;
    m_enote_store.update_with_sp_records_from_ledger(first_new_block,
        alignment_block_id,
        found_enote_records,
        new_block_ids,
        events);
}
//-------------------------------------------------------------------------------------------------------------------
// Seraphis
//-------------------------------------------------------------------------------------------------------------------
EnoteStoreUpdaterMockSp::EnoteStoreUpdaterMockSp(const rct::key &jamtis_spend_pubkey,
    const crypto::secret_key &k_view_balance,
    SpEnoteStoreMockV1 &enote_store) :
        m_jamtis_spend_pubkey{jamtis_spend_pubkey},
        m_k_view_balance{k_view_balance},
        m_enote_store{enote_store}
{
    jamtis::make_jamtis_unlockamounts_key(m_k_view_balance, m_xk_unlock_amounts);
    jamtis::make_jamtis_findreceived_key(m_k_view_balance, m_xk_find_received);
    jamtis::make_jamtis_generateaddress_secret(m_k_view_balance, m_s_generate_address);
    jamtis::make_jamtis_ciphertag_secret(m_s_generate_address, m_s_cipher_tag);

    m_cipher_context = std::make_unique<jamtis::jamtis_address_tag_cipher_context>(m_s_cipher_tag);
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t EnoteStoreUpdaterMockSp::refresh_index() const
{
    return m_enote_store.sp_refresh_index();
}
//-------------------------------------------------------------------------------------------------------------------
std::uint64_t EnoteStoreUpdaterMockSp::desired_first_block() const
{
    return m_enote_store.top_sp_scanned_block_index() + 1;
}
//-------------------------------------------------------------------------------------------------------------------
bool EnoteStoreUpdaterMockSp::try_get_block_id(const std::uint64_t block_index, rct::key &block_id_out) const
{
    return m_enote_store.try_get_block_id_for_sp(block_index, block_id_out);
}
//-------------------------------------------------------------------------------------------------------------------
void EnoteStoreUpdaterMockSp::consume_nonledger_chunk(const SpEnoteOriginStatus nonledger_origin_status,
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images)
{
    // 1. process the chunk
    std::unordered_map<crypto::key_image, SpContextualEnoteRecordV1> found_enote_records;
    std::unordered_map<crypto::key_image, SpEnoteSpentContextV1> found_spent_key_images;
    std::unordered_map<crypto::key_image, SpEnoteSpentContextV1> legacy_key_images_in_sp_selfsends;

    process_chunk_full_sp(m_jamtis_spend_pubkey,
        m_k_view_balance,
        m_xk_unlock_amounts,
        m_xk_find_received,
        m_s_generate_address,
        *m_cipher_context,
        [this](const crypto::key_image &key_image) -> bool
        {
            return this->m_enote_store.has_enote_with_key_image(key_image);
        },
        chunk_basic_records_per_tx,
        chunk_contextual_key_images,
        found_enote_records,
        found_spent_key_images,
        legacy_key_images_in_sp_selfsends);

    // 2. save the results
    std::list<EnoteStoreEvent> events;
    m_enote_store.update_with_sp_records_from_nonledger(nonledger_origin_status,
        found_enote_records,
        found_spent_key_images,
        legacy_key_images_in_sp_selfsends,
        events);
}
//-------------------------------------------------------------------------------------------------------------------
void EnoteStoreUpdaterMockSp::consume_onchain_chunk(
    const std::unordered_map<rct::key, std::list<ContextualBasicRecordVariant>> &chunk_basic_records_per_tx,
    const std::list<SpContextualKeyImageSetV1> &chunk_contextual_key_images,
    const std::uint64_t first_new_block,
    const rct::key &alignment_block_id,
    const std::vector<rct::key> &new_block_ids)
{
    // 1. process the chunk
    std::unordered_map<crypto::key_image, SpContextualEnoteRecordV1> found_enote_records;
    std::unordered_map<crypto::key_image, SpEnoteSpentContextV1> found_spent_key_images;
    std::unordered_map<crypto::key_image, SpEnoteSpentContextV1> legacy_key_images_in_sp_selfsends;

    process_chunk_full_sp(m_jamtis_spend_pubkey,
        m_k_view_balance,
        m_xk_unlock_amounts,
        m_xk_find_received,
        m_s_generate_address,
        *m_cipher_context,
        [this](const crypto::key_image &key_image) -> bool
        {
            return this->m_enote_store.has_enote_with_key_image(key_image);
        },
        chunk_basic_records_per_tx,
        chunk_contextual_key_images,
        found_enote_records,
        found_spent_key_images,
        legacy_key_images_in_sp_selfsends);

    // 2. save the results
    std::list<EnoteStoreEvent> events;
    m_enote_store.update_with_sp_records_from_ledger(first_new_block,
        alignment_block_id,
        new_block_ids,
        found_enote_records,
        found_spent_key_images,
        legacy_key_images_in_sp_selfsends,
        events);
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace mocks
} //namespace sp
