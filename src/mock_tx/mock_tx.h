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

// Mock tx interface
// NOT FOR PRODUCTION

#pragma once

//local headers

//third party headers
#include "ringct/rctTypes.h"

//standard headers
#include <memory>
#include <string>
#include <vector>

//forward declarations
namespace mock_tx
{
    class LedgerContext;
    class MockLedgerContext;
}


namespace mock_tx
{

////
// MockTxParamPack - parameter pack for mock tx
///
struct MockTxParamPack
{
    std::size_t max_rangeproof_splits;
    std::size_t ref_set_decomp_n;
    std::size_t ref_set_decomp_m;
};

////
// MockTx - mock transaction interface
///
class MockTx
{
public:
//constructors
    /// default constructor
    MockTx() = default;

//destructor: virtual for non-final type
    virtual ~MockTx() = default;

//member functions
    ////
    // validate the transaction
    // - if 'defer_batchable' is set, then batchable validation steps shouldn't be executed
    ///
    virtual bool validate(const std::shared_ptr<const LedgerContext> ledger_context,
        const bool defer_batchable = false) const;

    /// get size of tx
    virtual std::size_t get_size_bytes() const = 0;

    /// get a short description of the tx type
    virtual std::string get_descriptor() const = 0;

    /// get the tx version string: era | format | validation rules
    static void get_versioning_string(const unsigned char tx_era_version,
        const unsigned char tx_format_version,
        const unsigned char tx_validation_rules_version,
        std::string &version_string)
    {
        version_string += static_cast<char>(tx_era_version);
        version_string += static_cast<char>(tx_format_version);
        version_string += static_cast<char>(tx_validation_rules_version);
    }
    virtual void get_versioning_string(std::string &version_string) const final
    {
        get_versioning_string(m_tx_era_version, m_tx_format_version, m_tx_validation_rules_version, version_string);
    }

    //get_tx_byte_blob()

private:
    virtual bool validate_tx_semantics() const = 0;
    virtual bool validate_tx_linking_tags(const std::shared_ptr<const LedgerContext> ledger_context) const = 0;
    // e.g. sum(inputs) == sum(outputs), range proofs
    virtual bool validate_tx_amount_balance(const bool defer_batchable) const = 0;
    // e.g. membership, ownership, unspentness proofs
    virtual bool validate_tx_input_proofs(const std::shared_ptr<const LedgerContext> ledger_context,
        const bool defer_batchable) const = 0;
//member variables
protected:
    /// era of the tx (e.g. CryptoNote/RingCT/Seraphis)
    unsigned char m_tx_era_version;
    /// format version of the tx within its era
    unsigned char m_tx_format_version;
    /// a tx format's validation rules version
    unsigned char m_tx_validation_rules_version;
};

/**
* brief: make_mock_tx - make a mock transaction
* type: MockTxType - 
* param: params -
* param: in_amounts -
* param: out_amounts -
* return: the mock tx created
*/
template <typename MockTxType>
std::shared_ptr<MockTxType> make_mock_tx(const MockTxParamPack &params,
    const std::vector<rct::xmr_amount> &in_amounts,
    const std::vector<rct::xmr_amount> &out_amounts,
    std::shared_ptr<MockLedgerContext> ledger_context = nullptr);
/**
* brief: validate_mock_txs - validate a set of mock tx (use batching if possible)
* type: MockTxType - 
* param: txs_to_validate -
* return: true/false on verification result
*/
template <typename MockTxType>
bool validate_mock_txs(const std::vector<std::shared_ptr<MockTxType>> &txs_to_validate,
    const std::shared_ptr<const LedgerContext> ledger_context);

} //namespace mock_tx
