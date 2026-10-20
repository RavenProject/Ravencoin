// Copyright (c) 2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "privacy_burn.h"
#include "amount.h"
#include "test/test_raven.h"

#include <string>
#include <limits>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(privacy_burn_tests, BasicTestingSetup)

// The headline example: 33,000,000 RVN private burns exactly 330,000 RVN.
BOOST_AUTO_TEST_CASE(headline_one_percent)
{
    const CAmount amount = 33000000 * COIN;
    BOOST_CHECK_EQUAL(GetPrivacyBurnAmount(amount), 330000 * COIN);
}

// The minimum: 10 RVN private burns 0.1 RVN (1%).
BOOST_AUTO_TEST_CASE(minimum_ten_coins)
{
    const CAmount amount = 10 * COIN;
    BOOST_CHECK_EQUAL(GetPrivacyBurnAmount(amount), COIN / 10); // 0.1 RVN
    BOOST_CHECK(IsValidPrivacyAmount(amount));
}

// Below the minimum is rejected.
BOOST_AUTO_TEST_CASE(below_minimum_rejected)
{
    BOOST_CHECK(!IsValidPrivacyAmount(9 * COIN));
    BOOST_CHECK(!IsValidPrivacyAmount(0));
    BOOST_CHECK(!IsValidPrivacyAmount(-1));
    BOOST_CHECK(IsValidPrivacyAmount(MIN_PRIVACY_AMOUNT));
}

// The burn always rounds up, so it is never less than one percent.
BOOST_AUTO_TEST_CASE(rounds_up_never_short)
{
    // 10 RVN + 1 satoshi: 1% is 0.10000000.01 -> rounds up by one satoshi.
    const CAmount amount = 10 * COIN + 1;
    const CAmount burn = GetPrivacyBurnAmount(amount);
    BOOST_CHECK_EQUAL(burn, (COIN / 10) + 1);
    // Burn * 100 must be >= amount (i.e. at least one percent).
    BOOST_CHECK(burn * PRIVACY_BURN_DIVISOR >= amount);
}

// A spread of amounts: burn is always ceil(amount/100) and >= 1%.
BOOST_AUTO_TEST_CASE(invariant_across_amounts)
{
    const CAmount samples[] = {
        10 * COIN, 11 * COIN, 100 * COIN, 12345 * COIN,
        1000000 * COIN, 33000000 * COIN, MAX_MONEY
    };
    for (const CAmount a : samples) {
        const CAmount burn = GetPrivacyBurnAmount(a);
        BOOST_CHECK_EQUAL(burn, (a + 99) / 100);
        BOOST_CHECK(burn * PRIVACY_BURN_DIVISOR >= a);   // never under one percent
        BOOST_CHECK((burn - 1) * PRIVACY_BURN_DIVISOR < a); // but not over by a whole satoshi step
    }
}

// The consensus check accepts the exact burn and rejects anything else.
BOOST_AUTO_TEST_CASE(check_privacy_burn)
{
    std::string err;
    const CAmount amount = 33000000 * COIN;
    const CAmount correct = 330000 * COIN;

    BOOST_CHECK(CheckPrivacyBurn(amount, correct, err));

    BOOST_CHECK(!CheckPrivacyBurn(amount, correct - 1, err));       // too little
    BOOST_CHECK(!CheckPrivacyBurn(amount, correct + 1, err));       // too much
    BOOST_CHECK(!CheckPrivacyBurn(5 * COIN, GetPrivacyBurnAmount(5 * COIN), err)); // below min amount
}

// A value far above the money range must not overflow (no signed-overflow UB)
// and must still round up correctly.
BOOST_AUTO_TEST_CASE(overflow_safe)
{
    const CAmount big = std::numeric_limits<int64_t>::max();
    const CAmount burn = GetPrivacyBurnAmount(big);
    BOOST_CHECK_EQUAL(burn, big / 100 + 1); // max % 100 == 7, so rounds up
    BOOST_CHECK(burn > 0);                   // sane, no wraparound to negative
}

BOOST_AUTO_TEST_SUITE_END()
