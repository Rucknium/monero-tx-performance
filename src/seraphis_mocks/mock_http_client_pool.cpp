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

//local headers
#include "mock_http_client_pool.h"

//third party headers

//standard headers

//forward declarations


namespace sp
{
namespace mocks
{
//-------------------------------------------------------------------------------------------------------------------
std::size_t ClientConnectionPool::acquire_unused_http_client()
{
    std::lock_guard<std::mutex> lock{m_http_client_pool_mutex};

    // TODO: if m_http_client_pool.size() >= m_max_connections, wait until unused

    // Check if we have any clients available for use
    for (std::size_t i = 0; i < m_http_client_pool.size(); ++i)
    {
        if (m_http_client_pool[i].in_use)
            continue;

        m_http_client_pool[i].in_use = true;
        return i;
    }

    // If we don't find an unused client, add a new one to the pool
    m_http_client_pool.emplace_back(pool_http_client_t{
        .in_use = true,
        .http_client = std::unique_ptr<epee::net_utils::http::abstract_http_client>(new net::http::client())
    });

    if (!m_proxy.empty())
        m_http_client_pool.back().http_client->set_proxy(m_proxy);
    m_http_client_pool.back().http_client->set_server(m_daemon_address, m_daemon_login, m_ssl_support);

    return m_http_client_pool.size() - 1;
}
//-------------------------------------------------------------------------------------------------------------------
void ClientConnectionPool::release_http_client(size_t http_client_index)
{
    // Make the connection available for use again
    std::lock_guard<std::mutex> lock{m_http_client_pool_mutex};
    CHECK_AND_ASSERT_THROW_MES(m_http_client_pool.size() > http_client_index,
        "http client connection is unknown to the pool");
    m_http_client_pool[http_client_index].in_use = false;
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace mocks
} //namespace sp
