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

#include "eclib_ed25519.h"
#include "eclib_utils.h"
#include "eclib_utils.inl"  //should be included in ONLY this file

namespace crypto
{
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
template <typename LIBT>
static void eclib_interface()
{
    // dummy variables
    unsigned char* uchar_ptr = nullptr;
    const unsigned char* const_uchar_ptr = nullptr;
    bool boolean = false;

    // core group element types
          typename LIBT::ge_deserialized       GE_DESERIALIZED{};
    const typename LIBT::ge_deserialized CONST_GE_DESERIALIZED{};

          typename LIBT::ge_intermediate1       GE_INTERMEDIATE1{};
    const typename LIBT::ge_intermediate1 CONST_GE_INTERMEDIATE1{};

          typename LIBT::ge_intermediate2       GE_INTERMEDIATE2{};
    const typename LIBT::ge_intermediate2 CONST_GE_INTERMEDIATE2{};

          typename LIBT::ge_precomp       GE_PRECOMP{};
    const typename LIBT::ge_precomp CONST_GE_PRECOMP{};

          typename LIBT::ge_cached       GE_CACHED{};
    const typename LIBT::ge_cached CONST_GE_CACHED{};

    // eclib types
          typename LIBT::scalar       SCALAR{};
    const typename LIBT::scalar CONST_SCALAR{};

          typename LIBT::secret_key       SECRET_KEY{};
    const typename LIBT::secret_key CONST_SECRET_KEY{};

          typename LIBT::public_key       PUBLIC_KEY{};
    const typename LIBT::public_key CONST_PUBLIC_KEY{};

          typename LIBT::key_image       KEY_IMAGE{};
    const typename LIBT::key_image CONST_KEY_IMAGE{};

          typename LIBT::key_derivation       KEY_DERIVATION{};
    const typename LIBT::key_derivation CONST_KEY_DERIVATION{};

    // operators that should be implemented
    boolean = PUBLIC_KEY < PUBLIC_KEY;
    boolean = PUBLIC_KEY > PUBLIC_KEY;
    boolean = KEY_IMAGE < KEY_IMAGE;
    boolean = KEY_IMAGE > KEY_IMAGE;

    // byte access
    uchar_ptr       = LIBT::to_bytes(SCALAR);
    const_uchar_ptr = LIBT::to_bytes(CONST_SCALAR);
    uchar_ptr       = LIBT::to_bytes(SECRET_KEY);
    const_uchar_ptr = LIBT::to_bytes(CONST_SECRET_KEY);
    uchar_ptr       = LIBT::to_bytes(PUBLIC_KEY);
    const_uchar_ptr = LIBT::to_bytes(CONST_PUBLIC_KEY);

    // eclib functions
    LIBT::test_func(CONST_SECRET_KEY, SECRET_KEY);

    // eclib::utils functions
    LIBT::utils::util_test_func(CONST_SECRET_KEY, SECRET_KEY);
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------------------------
// instantiate the utils for each eclib type
//-------------------------------------------------------------------------------------------------------------------
template struct eclib_utils<eclib_ed25519>;
//-------------------------------------------------------------------------------------------------------------------
// expect the interface to compile for each eclib type
//-------------------------------------------------------------------------------------------------------------------
void eclib_interfaces_impl()
{
    eclib_interface<eclib_ed25519>();
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace crypto
