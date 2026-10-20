// Copyright (c) 2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_PRIVACY_BURN_H
#define RAVEN_PRIVACY_BURN_H

#include "amount.h"

#include <string>

// Privacy-fee burn.
//
// Moving RVN into the confidential (privacy) pool costs a fee that is BURNED
// rather than paid to a miner, so privacy demand itself permanently removes
// supply. The rate is one percent of the amount made private:
//
//     33,000,000 RVN private  ->  330,000 RVN burned   (1%)
//              10 RVN private  ->       0.1 RVN burned   (1%)
//
// The fee is computed in satoshis with integer math and always rounded UP, so
// the burn can never be less than one percent. A privacy transfer must move at
// least MIN_PRIVACY_AMOUNT so the one-percent rule stays meaningful at the low
// end (one percent of 10 RVN is 0.1 RVN).

//! One percent, expressed as a divisor (amount / 100).
static const int64_t PRIVACY_BURN_DIVISOR = 100;

//! Smallest amount that may be moved into the privacy pool: 10 RVN.
static const CAmount MIN_PRIVACY_AMOUNT = 10 * COIN;

/**
 * Return the amount that must be burned to make nPrivatizedAmount private.
 * This is one percent of the amount, rounded up to the next satoshi so the
 * burn is never short. Returns 0 for a non-positive input.
 */
CAmount GetPrivacyBurnAmount(const CAmount& nPrivatizedAmount);

/**
 * True if nPrivatizedAmount is a valid amount to move into the privacy pool:
 * within the money range and at least MIN_PRIVACY_AMOUNT.
 */
bool IsValidPrivacyAmount(const CAmount& nPrivatizedAmount);

/**
 * Consensus check for a confidential-send transaction: the burn actually
 * provided (the value sent to the burn address) must exactly equal the
 * required one-percent burn for the amount being made private, and the amount
 * itself must be valid. strError is set on failure.
 */
bool CheckPrivacyBurn(const CAmount& nPrivatizedAmount, const CAmount& nBurnProvided, std::string& strError);

#endif // RAVEN_PRIVACY_BURN_H
