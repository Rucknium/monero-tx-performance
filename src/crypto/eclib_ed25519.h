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
// 

#pragma once

extern "C"
{
#include "crypto-ops.h"
}
#include "crypto.h"
#include "eclib_utils.h"

namespace crypto
{

struct eclib_ed25519 final
{
/// crypto utils
using utils = eclib_utils<eclib_ed25519>;

/// core group element types
using ge_deserialized  = ge_p3;
using ge_intermediate1 = ge_p2;
using ge_intermediate2 = ge_p1p1;
using ge_precomp       = ge_precomp;
using ge_cached        = ge_cached;

/// crypto types
using scalar         = crypto::ec_scalar;
using secret_key     = crypto::secret_key;
using public_key     = crypto::public_key;
using key_image      = crypto::key_image;
using key_derivation = crypto::key_derivation;

/// byte access
static inline unsigned char* to_bytes(scalar &sc)                 { return ::to_bytes(sc); }
static inline const unsigned char* to_bytes(const scalar &sc)     { return ::to_bytes(sc); }
static inline unsigned char* to_bytes(public_key &pk)             { return ::to_bytes(pk); }
static inline const unsigned char* to_bytes(const public_key &pk) { return ::to_bytes(pk); }

/// crypto ops

static void test_func(const secret_key &k, secret_key &key_out);

}; //eclib_ed25519

} //namespace crypto
