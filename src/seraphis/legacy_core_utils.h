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

// Miscellaneous legacy utilities.
// Note: these are the bare minimum for unit testing and legacy enote recovery, so are not fully-featured.


#pragma once

//local headers
#include "crypto/crypto.h"
#include "cryptonote_basic/subaddress_index.h"
#include "ringct/rctTypes.h"

//third party headers

//standard headers

//forward declarations


namespace sp
{

/**
* brief: make_legacy_enote_v1 - make a v1 legacy enote sending to an address or subaddress
* param: destination_spendkey - [address: K^s = k^s G] [subaddress: K^{s,i} = (Hn(k^v, i) + k^s) G]
* param: destination_viewkey - [address: K^v = k^v G] [subaddress: K^{v,i} = k^v*(Hn(k^v, i) + k^s) G]
* param: amount - a
* param: output_index - t
* param: enote_ephemeral_privkey - [address: r] [subaddres: r_t]
* outparam: enote_out - [K^o, a]
* outparam: enote_ephemeral_pubkey_out - [address: r G] [subaddres: r_t K^{s,i}]
*/
void make_legacy_subaddress_spendkey(const rct::key &legacy_base_spend_pubkey,
    const crypto::secret_key &legacy_view_privkey,
    const cryptonote::subaddress_index &subaddress_index,
    rct::key &subaddress_spendkey_out);
void make_legacy_key_image(const crypto::secret_key &enote_view_privkey,
    const crypto::secret_key &legacy_spend_privkey,
    const rct::key &onetime_address,
    crypto::key_image &key_image_out);
void make_legacy_enote_view_privkey(const std::uint64_t tx_output_index,
    const crypto::key_derivation &sender_receiver_DH_derivation,
    const crypto::secret_key &legacy_view_privkey,
    const boost::optional<cryptonote::subaddress_index> &subaddress_index,
    crypto::secret_key &enote_view_privkey_out);
void make_legacy_amount_mask_v2(const crypto::secret_key &sender_receiver_secret,
    crypto::secret_key &amount_blinding_factor_out);
void make_legacy_amount_encoding_factor_v2(const crypto::secret_key &sender_receiver_secret,
    rct::key &amount_encoding_factor);
void make_legacy_encoded_amount_factor(const crypto::secret_key &sender_receiver_secret,
    rct::key &encoded_amount_factor_out);
rct::xmr_amount legacy_xor_encoded_amount(const rct::xmr_amount encoded_amount, const rct::key &encoding_factor);
rct::xmr_amount legacy_xor_amount(const rct::xmr_amount amount, const rct::key &encoding_factor);
void make_legacy_sender_receiver_secret(const rct::key &destination_viewkey,
    const std::uint64_t output_index,
    const crypto::secret_key &enote_ephemeral_privkey,
    crypto::secret_key &legacy_sender_receiver_secret_out);
void make_legacy_onetime_address(const rct::key &destination_spendkey,
    const rct::key &destination_viewkey,
    const std::uint64_t output_index,
    const crypto::secret_key &enote_ephemeral_privkey,
    rct::key &onetime_address_out);
void make_legacy_encoded_amount_v1(const rct::key &destination_viewkey,
    const std::uint64_t output_index,
    const crypto::secret_key &enote_ephemeral_privkey,
    const crypto::secret_key &amount_mask,
    const rct::xmr_amount amount,
    rct::key &encoded_amount_mask_out,
    rct::key &encoded_amount_out);
void make_legacy_encoded_amount_v2(const rct::key &destination_viewkey,
    const std::uint64_t output_index,
    const crypto::secret_key &enote_ephemeral_privkey,
    const rct::xmr_amount amount,
    rct::xmr_amount &encoded_amount_out);
void make_legacy_amount_mask_v2(const rct::key &destination_viewkey,
    const std::uint64_t output_index,
    const crypto::secret_key &enote_ephemeral_privkey,
    rct::key &amount_mask_out);
void make_legacy_view_tag(const rct::key &destination_viewkey,
    const std::uint64_t output_index,
    const crypto::secret_key &enote_ephemeral_privkey,
    crypto::view_tag &view_tag_out);


} //namespace sp
