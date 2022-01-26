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
#include "jamtis_hash_functions.h"

//local headers
#include "crypto/hash.h"
#include "ringct/rctTypes.h"

//third party headers

//standard headers

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "seraphis"

namespace sp
{
namespace jamtis
{

using jamtis_hash_result = unsigned char[32];
constexpr std::size_t KECCAK_256_BITRATE_BYTES{136};

//-------------------------------------------------------------------------------------------------------------------
// data_out = data_in || [input] || 'domain-sep'
//-------------------------------------------------------------------------------------------------------------------
static void jamtis_hash_fill_data(const std::string &domain_separator,
    const unsigned char *input,
    const std::size_t input_length,
    std::string &data_inout)
{
    data_inout.reserve(data_out.size() + domain_separator.size() + input_length);
    data_inout.append(input, input_length);
    data_inout.append(domain_separator);
}
//-------------------------------------------------------------------------------------------------------------------
// Pad136(k) = k || 104*(0x00)
//-------------------------------------------------------------------------------------------------------------------
static void jamtis_pad_key136(const rct::key &key, std::string &padded_key_out)
{
    static const std::string padding{KECCAK_256_BITRATE_BYTES - sizeof(rct::key), '0'};
    padded_key_out.reserve(KECCAK_256_BITRATE_BYTES);
    padded_key_out.clear();
    padded_key_out.append((const char *)&key, sizeof(rct::key));
    padded_key_out += padding;
}
//-------------------------------------------------------------------------------------------------------------------
// H_32(data)
//-------------------------------------------------------------------------------------------------------------------
static void jamtis_hash_base(const std::string &data, jamtis_hash_result &hash_result_out)
{
    cn_fast_hash(data.data(), data.size(), reinterpret_cast<char *>(hash_result_out));
}
//-------------------------------------------------------------------------------------------------------------------
// H_32([input] || 'domain-sep')
//-------------------------------------------------------------------------------------------------------------------
static void jamtis_hash_simple(const std::string &domain_separator,
    const unsigned char *input,
    const std::size_t input_length,
    jamtis_hash_result &hash_result_out)
{
    std::string hash_data;
    jamtis_hash_fill_data(domain_separator, input, input_length, hash_data);
    jamtis_hash_base(hash_data, hash_result_out);
}
//-------------------------------------------------------------------------------------------------------------------
// H_32(Pad136(k) || [input] || 'domain-sep')
//-------------------------------------------------------------------------------------------------------------------
static void jamtis_hash_padded(const std::string &domain_separator,
    const rct::key &derivation_key,
    const unsigned char *input,
    const std::size_t input_length,
    jamtis_hash_result &hash_result_out)
{
    std::string hash_data;
    hash_data.reserve(KECCAK_256_BITRATE_BYTES + domain_separator.size() + input_length);
    jamtis_pad_key136(derivation_key, hash_data);
    jamtis_hash_fill_data(domain_separator, input, input_length, hash_data);
    jamtis_hash_base(hash_data, hash_result_out);
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
void jamtis_hash1(const std::string &domain_separator,
    const unsigned char *input,
    const std::size_t input_length,
    unsigned char *hash_out)
{
    // H_1(x): 1-byte output
    jamtis_hash_result hash_result_32{};
    jamtis_hash_simple(domain_separator, input, input_length, hash_result_32);
    memcpy(hash_out, hash_result_32, 1);
}
//-------------------------------------------------------------------------------------------------------------------
void jamtis_hash8(const std::string &domain_separator,
    const unsigned char *input,
    const std::size_t input_length,
    unsigned char *hash_out)
{
    // H_8(x): 8-byte output
    jamtis_hash_result hash_result_32{};
    jamtis_hash_simple(domain_separator, input, input_length, hash_result_32);
    memcpy(hash_out, hash_result_32, 8);
}
//-------------------------------------------------------------------------------------------------------------------
void jamtis_hash16(const std::string &domain_separator,
    const unsigned char *input,
    const std::size_t input_length,
    unsigned char *hash_out)
{
    // H_16(x): 16-byte output
    jamtis_hash_result hash_result_32{};
    jamtis_hash_simple(domain_separator, input, input_length, hash_result_32);
    memcpy(hash_out, hash_result_32, 16);
}
//-------------------------------------------------------------------------------------------------------------------
void jamtis_hash_scalar(const std::string &domain_separator,
    const unsigned char *input,
    const std::size_t input_length,
    unsigned char *hash_out)
{
    // H_n(x): Ed25519 group scalar output (32 bytes)
    jamtis_hash_result hash_result_32{};
    jamtis_hash_simple(domain_separator, input, input_length, hash_result_32);
    sc_reduce32(hash_result_32);  //mod l
    memcpy(hash_out, hash_result_32, 32);
}
//-------------------------------------------------------------------------------------------------------------------
void jamtis_key_derive(const std::string &domain_separator,
    const rct::key &derivation_key,
    const unsigned char *input,
    const std::size_t input_length,
    unsigned char *hash_out)
{
    // H_n(Pad_136(k), x): Ed25519 group scalar output (32 bytes)
    jamtis_hash_result hash_result_32{};
    jamtis_hash_padded(domain_separator, derivation_key, input, input_length, hash_result_32);
    sc_reduce32(hash_result_32);  //mod l
    memcpy(hash_out, hash_result_32, 32);
}
//-------------------------------------------------------------------------------------------------------------------
void jamtis_secret_derive(const std::string &domain_separator,
    const rct::key &derivation_key,
    const unsigned char *input,
    const std::size_t input_length,
    unsigned char *hash_out)
{
    // H_32(Pad_136(k), x): 32-byte output
    jamtis_hash_result hash_result_32{};
    jamtis_hash_padded(domain_separator, derivation_key, input, input_length, hash_result_32);
    memcpy(hash_out, hash_result_32, 32);
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace jamtis
} //namespace sp
