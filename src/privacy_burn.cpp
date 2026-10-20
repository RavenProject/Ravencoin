// Copyright (c) 2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "privacy_burn.h"

#include "tinyformat.h"
#include "utilmoneystr.h"

CAmount GetPrivacyBurnAmount(const CAmount& nPrivatizedAmount)
{
    if (nPrivatizedAmount <= 0)
        return 0;

    // One percent, rounded up: ceil(amount / 100) in satoshis. Rounding up
    // guarantees the burn is never less than one percent. Computed without an
    // addition so it cannot overflow for any positive int64, even if a caller
    // passes an amount above MAX_MONEY before it has been range-checked.
    return nPrivatizedAmount / PRIVACY_BURN_DIVISOR
           + ((nPrivatizedAmount % PRIVACY_BURN_DIVISOR) ? 1 : 0);
}

bool IsValidPrivacyAmount(const CAmount& nPrivatizedAmount)
{
    if (!MoneyRange(nPrivatizedAmount))
        return false;
    return nPrivatizedAmount >= MIN_PRIVACY_AMOUNT;
}

bool CheckPrivacyBurn(const CAmount& nPrivatizedAmount, const CAmount& nBurnProvided, std::string& strError)
{
    if (!IsValidPrivacyAmount(nPrivatizedAmount)) {
        strError = strprintf("Privacy amount %s is invalid: must be at least %s and within the money range",
                             FormatMoney(nPrivatizedAmount), FormatMoney(MIN_PRIVACY_AMOUNT));
        return false;
    }

    const CAmount nRequiredBurn = GetPrivacyBurnAmount(nPrivatizedAmount);

    if (!MoneyRange(nBurnProvided)) {
        strError = strprintf("Privacy burn %s is outside the money range", FormatMoney(nBurnProvided));
        return false;
    }

    if (nBurnProvided != nRequiredBurn) {
        strError = strprintf("Privacy burn mismatch: provided %s but %s (1%%) is required to make %s private",
                             FormatMoney(nBurnProvided), FormatMoney(nRequiredBurn), FormatMoney(nPrivatizedAmount));
        return false;
    }

    return true;
}
