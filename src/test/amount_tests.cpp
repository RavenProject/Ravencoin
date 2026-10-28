// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "policy/feerate.h"
#include "test/test_raven.h"

#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(amount_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(Money_Range_Test)
    {
        BOOST_TEST_MESSAGE("Running Money Range Test");

        BOOST_CHECK_EQUAL(MoneyRange(CAmount(-1)), false);
        BOOST_CHECK_EQUAL(MoneyRange(MAX_MONEY + CAmount(1)), false);
        BOOST_CHECK_EQUAL(MoneyRange(CAmount(1)), true);
    }

    BOOST_AUTO_TEST_CASE(Get_Fee_Test)
    {
        BOOST_TEST_MESSAGE("Running Get Fee Test");

        CFeeRate feeRate, altFeeRate;

        feeRate = CFeeRate(0);
        // Must always return 0
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1e5), 0);

        feeRate = CFeeRate(1000);
        // Must always just return the arg
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1), 1);
        BOOST_CHECK_EQUAL(feeRate.GetFee(121), 121);
        BOOST_CHECK_EQUAL(feeRate.GetFee(999), 999);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), 1e3);
        BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), 9e3);

        feeRate = CFeeRate(-1000);
        // Must always just return -1 * arg
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1), -1);
        BOOST_CHECK_EQUAL(feeRate.GetFee(121), -121);
        BOOST_CHECK_EQUAL(feeRate.GetFee(999), -999);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), -1e3);
        BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), -9e3);

        feeRate = CFeeRate(123);
        // Truncates the result, if not integer
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(8), 1); // Special case: returns 1 instead of 0
        BOOST_CHECK_EQUAL(feeRate.GetFee(9), 1);
        BOOST_CHECK_EQUAL(feeRate.GetFee(121), 14);
        BOOST_CHECK_EQUAL(feeRate.GetFee(122), 15);
        BOOST_CHECK_EQUAL(feeRate.GetFee(999), 122);
        BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), 123);
        BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), 1107);

        feeRate = CFeeRate(-123);
        // Truncates the result, if not integer
        BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
        BOOST_CHECK_EQUAL(feeRate.GetFee(8), -1); // Special case: returns -1 instead of 0
        BOOST_CHECK_EQUAL(feeRate.GetFee(9), -1);

        // check alternate constructor
        feeRate = CFeeRate(1000);
        altFeeRate = CFeeRate(feeRate);
        BOOST_CHECK_EQUAL(feeRate.GetFee(100), altFeeRate.GetFee(100));

        // Check full constructor
        // default value
        BOOST_CHECK(CFeeRate(CAmount(-1), 1000) == CFeeRate(-1));
        BOOST_CHECK(CFeeRate(CAmount(0), 1000) == CFeeRate(0));
        BOOST_CHECK(CFeeRate(CAmount(1), 1000) == CFeeRate(1));
        // lost precision (can only resolve satoshis per kB)
        BOOST_CHECK(CFeeRate(CAmount(1), 1001) == CFeeRate(0));
        BOOST_CHECK(CFeeRate(CAmount(2), 1001) == CFeeRate(1));
        // some more integer checks
        BOOST_CHECK(CFeeRate(CAmount(26), 789) == CFeeRate(32));
        BOOST_CHECK(CFeeRate(CAmount(27), 789) == CFeeRate(34));
        // The quotient is 227 sat/kB on 64-bit systems even though the product overflows int64_t.
        const size_t halfMaxSize = std::numeric_limits<size_t>::max() >> 1;
        if (sizeof(size_t) == 8) {
            BOOST_CHECK(CFeeRate(MAX_MONEY, halfMaxSize) == CFeeRate(227));
        } else {
            const CAmount expected = MAX_MONEY / halfMaxSize * 1000 + MAX_MONEY % halfMaxSize * 1000 / halfMaxSize;
            BOOST_CHECK(CFeeRate(MAX_MONEY, halfMaxSize) == CFeeRate(expected));
        }
    }

    BOOST_AUTO_TEST_CASE(Fee_Arithmetic_Boundaries_Test)
    {
        const CAmount maxAmount = std::numeric_limits<CAmount>::max();
        const CAmount minAmount = std::numeric_limits<CAmount>::min();
        const size_t maxSize = std::numeric_limits<size_t>::max();

        // These quotients fit, even though the original intermediate products do not.
        BOOST_CHECK(CFeeRate(maxAmount, 1000) == CFeeRate(maxAmount));
        BOOST_CHECK(CFeeRate(minAmount, 1000) == CFeeRate(minAmount));
        const uint64_t max1001 = uint64_t(maxAmount) / 1001 * 1000 + uint64_t(maxAmount) % 1001 * 1000 / 1001;
        const uint64_t minMagnitude = uint64_t(maxAmount) + 1;
        const uint64_t min1001 = minMagnitude / 1001 * 1000 + minMagnitude % 1001 * 1000 / 1001;
        BOOST_CHECK(CFeeRate(maxAmount, 1001) == CFeeRate(CAmount(max1001)));
        BOOST_CHECK(CFeeRate(minAmount, 1001) == CFeeRate(-CAmount(min1001)));
        const CAmount max999 = CAmount(uint64_t(maxAmount) / 1000 * 999 + uint64_t(maxAmount) % 1000 * 999 / 1000);
        BOOST_CHECK_EQUAL(CFeeRate(maxAmount).GetFee(999), max999);
        BOOST_CHECK_EQUAL(CFeeRate(1000).GetFee(maxSize),
                          maxSize <= static_cast<size_t>(maxAmount) ? static_cast<CAmount>(maxSize) : maxAmount);
        BOOST_CHECK_EQUAL(CFeeRate(-1000).GetFee(1000), -1000);

        // Results outside CAmount saturate, rather than wrapping or invoking UB.
        BOOST_CHECK(CFeeRate(maxAmount, 1) == CFeeRate(maxAmount));
        BOOST_CHECK(CFeeRate(minAmount, 1) == CFeeRate(minAmount));
        BOOST_CHECK_EQUAL(CFeeRate(maxAmount).GetFee(2000), maxAmount);
        BOOST_CHECK_EQUAL(CFeeRate(minAmount).GetFee(2000), minAmount);

        // Retain truncation toward zero for small, non-integral results.
        BOOST_CHECK(CFeeRate(CAmount(1), 3000) == CFeeRate(0));
        BOOST_CHECK(CFeeRate(CAmount(-1), 3000) == CFeeRate(0));
        BOOST_CHECK_EQUAL(CFeeRate(1).GetFee(999), 1);
        BOOST_CHECK_EQUAL(CFeeRate(-1).GetFee(999), -1);

        if (maxSize > static_cast<size_t>(maxAmount)) {
            // Full-width size_t values must not narrow to negative int64_t.
            BOOST_CHECK(CFeeRate(CAmount(1), maxSize) == CFeeRate(0));
            BOOST_CHECK(CFeeRate(maxAmount, maxSize) == CFeeRate(499));
            BOOST_CHECK_EQUAL(CFeeRate(1).GetFee(maxSize), static_cast<CAmount>(maxSize / 1000));
            BOOST_CHECK_EQUAL(CFeeRate(-1).GetFee(maxSize), -static_cast<CAmount>(maxSize / 1000));
        }
    }

    BOOST_AUTO_TEST_CASE(Binary_Operator_Test)
    {
        BOOST_TEST_MESSAGE("Running Binary Operator Test");

        CFeeRate a, b;
        a = CFeeRate(1);
        b = CFeeRate(2);
        BOOST_CHECK(a < b);
        BOOST_CHECK(b > a);
        BOOST_CHECK(a == a);
        BOOST_CHECK(a <= b);
        BOOST_CHECK(a <= a);
        BOOST_CHECK(b >= a);
        BOOST_CHECK(b >= b);
        // a should be 0.00000002 RVN/kB now
        a += a;
        BOOST_CHECK(a == b);
    }

    BOOST_AUTO_TEST_CASE(Fee_Rate_Addition_Boundaries_Test)
    {
        const CAmount maxAmount = std::numeric_limits<CAmount>::max();
        const CAmount minAmount = std::numeric_limits<CAmount>::min();

        CFeeRate positive(maxAmount - 1);
        positive += CFeeRate(1);
        BOOST_CHECK(positive == CFeeRate(maxAmount));
        positive += CFeeRate(1);
        BOOST_CHECK(positive == CFeeRate(maxAmount));

        CFeeRate negative(minAmount + 1);
        negative += CFeeRate(-1);
        BOOST_CHECK(negative == CFeeRate(minAmount));
        negative += CFeeRate(-1);
        BOOST_CHECK(negative == CFeeRate(minAmount));

        CFeeRate positiveSelf(maxAmount / 2 + 1);
        positiveSelf += positiveSelf;
        BOOST_CHECK(positiveSelf == CFeeRate(maxAmount));
        CFeeRate negativeSelf(minAmount / 2 - 1);
        negativeSelf += negativeSelf;
        BOOST_CHECK(negativeSelf == CFeeRate(minAmount));

        CFeeRate opposite(maxAmount);
        opposite += CFeeRate(minAmount);
        BOOST_CHECK(opposite == CFeeRate(-1));
    }

    BOOST_AUTO_TEST_CASE(ToString_Test)
    {
        BOOST_TEST_MESSAGE("Running ToString Test");

        CFeeRate feeRate;
        feeRate = CFeeRate(1);
        BOOST_CHECK_EQUAL(feeRate.ToString(), "0.00000001 RVN/kB");
    }

BOOST_AUTO_TEST_SUITE_END()
