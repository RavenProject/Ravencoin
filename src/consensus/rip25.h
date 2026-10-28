// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_CONSENSUS_RIP25_H
#define RAVEN_CONSENSUS_RIP25_H

#include <array>
#include <cstddef>

namespace Consensus {

static const std::size_t PQ_SIGNATURE_CONTEXT_BYTES = 81;
using PQSignatureContext = std::array<unsigned char, PQ_SIGNATURE_CONTEXT_BYTES>;

inline bool IsValidPQSignatureContext(const PQSignatureContext& context)
{
    static const unsigned char prefix[] = "RVN/ML-DSA-44/v1/";
    static_assert(sizeof(prefix) - 1 == 17, "unexpected RIP-25 context prefix length");

    for (std::size_t i = 0; i < sizeof(prefix) - 1; ++i) {
        if (context[i] != prefix[i])
            return false;
    }
    for (std::size_t i = sizeof(prefix) - 1; i < context.size(); ++i) {
        const unsigned char ch = context[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return false;
    }
    return true;
}

inline const PQSignatureContext& NullPQSignatureContext()
{
    static const PQSignatureContext context{};
    return context;
}

} // namespace Consensus

#endif // RAVEN_CONSENSUS_RIP25_H
