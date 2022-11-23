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
#include "tx_contextual_enote_record_types.h"

//local headers
#include "crypto/crypto.h"
#include "ringct/rctTypes.h"
#include "seraphis_crypto/sp_variant.h"

//third party headers

//standard headers
#include <algorithm>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis"

namespace sp
{
//-------------------------------------------------------------------------------------------------------------------
bool SpEnoteOriginContextV1::is_older_than(const SpEnoteOriginContextV1 &other_context) const
{
    // 1. origin status (higher statuses are assumed to be 'older')
    if (m_origin_status > other_context.m_origin_status)
        return true;

    // 2. block height
    if (m_block_height < other_context.m_block_height)
        return true;

    // 3. enote ledger index
    if (m_enote_ledger_index < other_context.m_enote_ledger_index)
        return true;

    // 4. block timestamp
    if (m_block_timestamp < other_context.m_block_timestamp)
        return true;

    return false;
}
//-------------------------------------------------------------------------------------------------------------------
bool SpEnoteSpentContextV1::is_older_than(const SpEnoteSpentContextV1 &other_context) const
{
    // 1. spent status (higher statuses are assumed to be 'older')
    if (m_spent_status > other_context.m_spent_status)
        return true;

    // 2. block height
    if (m_block_height < other_context.m_block_height)
        return true;

    // 3. block timestamp
    if (m_block_timestamp < other_context.m_block_timestamp)
        return true;

    return false;
}
//-------------------------------------------------------------------------------------------------------------------
bool LegacyContextualBasicEnoteRecordV1::have_same_destination(const LegacyContextualBasicEnoteRecordV1 &record1,
    const LegacyContextualBasicEnoteRecordV1 &record2)
{
    return onetime_address_ref(record1.m_record.m_enote) == onetime_address_ref(record2.m_record.m_enote);
}
//-------------------------------------------------------------------------------------------------------------------
void LegacyContextualIntermediateEnoteRecordV1::get_onetime_address(rct::key &onetime_address_out) const
{
    onetime_address_out = onetime_address_ref(m_record.m_enote);
}
//-------------------------------------------------------------------------------------------------------------------
bool LegacyContextualIntermediateEnoteRecordV1::have_same_destination(
    const LegacyContextualIntermediateEnoteRecordV1 &record1,
    const LegacyContextualIntermediateEnoteRecordV1 &record2)
{
    return onetime_address_ref(record1.m_record.m_enote) == onetime_address_ref(record2.m_record.m_enote);
}
//-------------------------------------------------------------------------------------------------------------------
bool LegacyContextualEnoteRecordV1::have_same_destination(const LegacyContextualEnoteRecordV1 &record1,
    const LegacyContextualEnoteRecordV1 &record2)
{
    return onetime_address_ref(record1.m_record.m_enote) == onetime_address_ref(record2.m_record.m_enote);
}
//-------------------------------------------------------------------------------------------------------------------
bool LegacyContextualEnoteRecordV1::has_origin_status(const SpEnoteOriginStatus test_status) const
{
    return m_origin_context.m_origin_status == test_status;
}
//-------------------------------------------------------------------------------------------------------------------
bool LegacyContextualEnoteRecordV1::has_spent_status(const SpEnoteSpentStatus test_status) const
{
    return m_spent_context.m_spent_status == test_status;
}
//-------------------------------------------------------------------------------------------------------------------
bool SpContextualBasicEnoteRecordV1::have_same_destination(const SpContextualBasicEnoteRecordV1 &record1,
    const SpContextualBasicEnoteRecordV1 &record2)
{
    return onetime_address_ref(record1.m_record.m_enote) == onetime_address_ref(record2.m_record.m_enote);
}
//-------------------------------------------------------------------------------------------------------------------
void SpContextualIntermediateEnoteRecordV1::get_onetime_address(rct::key &onetime_address_out) const
{
    onetime_address_out = onetime_address_ref(m_record.m_enote);
}
//-------------------------------------------------------------------------------------------------------------------
bool SpContextualIntermediateEnoteRecordV1::have_same_destination(const SpContextualIntermediateEnoteRecordV1 &record1,
    const SpContextualIntermediateEnoteRecordV1 &record2)
{
    rct::key onetime_address_1;
    rct::key onetime_address_2;
    record1.get_onetime_address(onetime_address_1);
    record2.get_onetime_address(onetime_address_2);

    return onetime_address_1 == onetime_address_2;
}
//-------------------------------------------------------------------------------------------------------------------
bool SpContextualEnoteRecordV1::have_same_destination(const SpContextualEnoteRecordV1 &record1,
    const SpContextualEnoteRecordV1 &record2)
{
    return onetime_address_ref(record1.m_record.m_enote) == onetime_address_ref(record2.m_record.m_enote);
}
//-------------------------------------------------------------------------------------------------------------------
bool SpContextualEnoteRecordV1::has_origin_status(const SpEnoteOriginStatus test_status) const
{
    return m_origin_context.m_origin_status == test_status;
}
//-------------------------------------------------------------------------------------------------------------------
bool SpContextualEnoteRecordV1::has_spent_status(const SpEnoteSpentStatus test_status) const
{
    return m_spent_context.m_spent_status == test_status;
}
//-------------------------------------------------------------------------------------------------------------------
const SpEnoteOriginContextV1& origin_context_ref(const ContextualBasicRecordVariant &variant)
{
    struct visitor : public SpVariantStaticVisitor<const SpEnoteOriginContextV1&>
    {
        using SpVariantStaticVisitor::operator();  //for blank overload
        const SpEnoteOriginContextV1& operator()(const LegacyContextualBasicEnoteRecordV1 &record) const
        { return record.m_origin_context; }
        const SpEnoteOriginContextV1& operator()(const SpContextualBasicEnoteRecordV1 &record) const
        { return record.m_origin_context; }
    };

    return variant.visit(visitor{});
}
//-------------------------------------------------------------------------------------------------------------------
rct::xmr_amount amount_ref(const ContextualRecordVariant &variant)
{
    struct visitor : public SpVariantStaticVisitor<rct::xmr_amount>
    {
        using SpVariantStaticVisitor::operator();  //for blank overload
        rct::xmr_amount operator()(const LegacyContextualEnoteRecordV1 &record) const { return record.amount(); }
        rct::xmr_amount operator()(const SpContextualEnoteRecordV1 &record) const { return record.amount(); }
    };

    return variant.visit(visitor{});
}
//-------------------------------------------------------------------------------------------------------------------
const SpEnoteOriginContextV1& origin_context_ref(const ContextualRecordVariant &variant)
{
    struct visitor : public SpVariantStaticVisitor<const SpEnoteOriginContextV1&>
    {
        using SpVariantStaticVisitor::operator();  //for blank overload
        const SpEnoteOriginContextV1& operator()(const LegacyContextualEnoteRecordV1 &record) const
        { return record.m_origin_context; }
        const SpEnoteOriginContextV1& operator()(const SpContextualEnoteRecordV1 &record) const
        { return record.m_origin_context; }
    };

    return variant.visit(visitor{});
}
//-------------------------------------------------------------------------------------------------------------------
const SpEnoteSpentContextV1& spent_context_ref(const ContextualRecordVariant &variant)
{
    struct visitor : public SpVariantStaticVisitor<const SpEnoteSpentContextV1&>
    {
        using SpVariantStaticVisitor::operator();  //for blank overload
        const SpEnoteSpentContextV1& operator()(const LegacyContextualEnoteRecordV1 &record) const
        { return record.m_spent_context; }
        const SpEnoteSpentContextV1& operator()(const SpContextualEnoteRecordV1 &record) const
        { return record.m_spent_context; }
    };

    return variant.visit(visitor{});
}
//-------------------------------------------------------------------------------------------------------------------
bool SpContextualKeyImageSetV1::has_key_image(const crypto::key_image &test_key_image) const
{
    return std::find(m_legacy_key_images.begin(), m_legacy_key_images.end(), test_key_image) !=
            m_legacy_key_images.end() ||
        std::find(m_sp_key_images.begin(), m_sp_key_images.end(), test_key_image) != m_sp_key_images.end();
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace sp
