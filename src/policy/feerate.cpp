// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "feerate.h"

#include "tinyformat.h"

#include <limits>

const std::string CURRENCY_UNIT = "RVN";

namespace {

// Compute amount * multiplier / divisor with truncation toward zero. The
// intermediate product can exceed 64 bits even when the quotient fits.
// Divide first, then calculate the remaining fractional product one bit at a
// time without overflowing uint64_t. Values outside CAmount saturate.
CAmount SaturatingMultiplyDivide(CAmount amount, uint64_t multiplier, uint64_t divisor)
{
    assert(divisor != 0);
    if (amount == 0 || multiplier == 0)
        return 0;

    const bool negative = amount < 0;
    const CAmount saturated = negative ? std::numeric_limits<CAmount>::min() : std::numeric_limits<CAmount>::max();
    const uint64_t limit = uint64_t(std::numeric_limits<CAmount>::max()) + uint64_t(negative);
    const uint64_t magnitude = negative ? uint64_t(-(amount + 1)) + 1 : uint64_t(amount);

    if (magnitude <= limit / multiplier) {
        const uint64_t result = magnitude * multiplier / divisor;
        if (!negative)
            return CAmount(result);
        return result == limit ? std::numeric_limits<CAmount>::min() : -CAmount(result);
    }

    const uint64_t whole = magnitude / divisor;
    if (whole > limit / multiplier)
        return saturated;
    const uint64_t integral = whole * multiplier;
    const uint64_t fractionalLimit = limit - integral;
    const uint64_t remainder = magnitude % divisor;

    uint64_t fractional = 0;
    uint64_t residual = 0;
    // After each bit, fractional * divisor + residual equals the product of
    // remainder and the multiplier prefix already processed.
    for (int bit = 63; bit >= 0; --bit) {
        if (fractional > fractionalLimit / 2)
            return saturated;
        fractional *= 2;

        unsigned int carry = 0;
        if (residual >= divisor - residual) {
            residual -= divisor - residual;
            ++carry;
        } else {
            residual += residual;
        }

        if ((multiplier >> bit) & 1) {
            if (residual >= divisor - remainder) {
                residual -= divisor - remainder;
                ++carry;
            } else {
                residual += remainder;
            }
        }

        if (carry > fractionalLimit - fractional)
            return saturated;
        fractional += carry;
    }

    const uint64_t result = integral + fractional;
    if (!negative)
        return CAmount(result);
    if (result == limit)
        return std::numeric_limits<CAmount>::min();
    return -CAmount(result);
}

} // namespace

CFeeRate::CFeeRate(const CAmount& nFeePaid, size_t nBytes_)
{
    static_assert(sizeof(size_t) <= sizeof(uint64_t), "Unsupported size_t width");
    if (nBytes_ > 0)
        nSatoshisPerK = SaturatingMultiplyDivide(nFeePaid, 1000, uint64_t(nBytes_));
    else
        nSatoshisPerK = 0;
}

CAmount CFeeRate::GetFee(size_t nBytes_) const
{
    static_assert(sizeof(size_t) <= sizeof(uint64_t), "Unsupported size_t width");
    CAmount nFee = SaturatingMultiplyDivide(nSatoshisPerK, uint64_t(nBytes_), 1000);

    if (nFee == 0 && nBytes_ != 0) {
        if (nSatoshisPerK > 0)
            nFee = CAmount(1);
        if (nSatoshisPerK < 0)
            nFee = CAmount(-1);
    }

    return nFee;
}

std::string CFeeRate::ToString() const
{
    return strprintf("%d.%08d %s/kB", nSatoshisPerK / COIN, nSatoshisPerK % COIN, CURRENCY_UNIT);
}
