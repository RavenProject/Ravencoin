// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include "base58.h"
#include "chainparams.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "policy/policy.h"
#include "crypto/mldsa.h"
#include "script/standard.h"
#include "txmempool.h"
#include "util.h"
#include "validation.h"

#include "test/test_raven.h"

#include <boost/test/unit_test.hpp>
#include <list>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(mempool_tests, TestingSetup)

    BOOST_AUTO_TEST_CASE(mempool_clear_drops_compact_block_index)
    {
        CTxMemPool pool;
        TestMemPoolEntryHelper entry;
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout = COutPoint(uint256S("31"), 0);
        tx.vout.emplace_back(1000, CScript() << OP_TRUE);
        BOOST_REQUIRE(pool.addUnchecked(tx.GetHash(), entry.FromTx(tx)));
        BOOST_REQUIRE_EQUAL(pool.vTxHashes.size(), 1U);

        pool.clear();
        BOOST_CHECK_EQUAL(pool.size(), 0U);
        BOOST_REQUIRE(pool.vTxHashes.empty());

        tx.nLockTime = 1;
        BOOST_REQUIRE(pool.addUnchecked(tx.GetHash(), entry.FromTx(tx)));
        BOOST_REQUIRE_EQUAL(pool.vTxHashes.size(), 1U);
        BOOST_CHECK(pool.vTxHashes[0].second->GetTx().GetHash() == tx.GetHash());
    }

    BOOST_AUTO_TEST_CASE(mempool_remove_test)
    {
        BOOST_TEST_MESSAGE("Running Mempool Remove Test");

        // Test CTxMemPool::remove functionality

        TestMemPoolEntryHelper entry;
        // Parent transaction with three children,
        // and three grand-children:
        CMutableTransaction txParent;
        txParent.vin.resize(1);
        txParent.vin[0].scriptSig = CScript() << OP_11;
        txParent.vout.resize(3);
        for (int i = 0; i < 3; i++)
        {
            txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            txParent.vout[i].nValue = 33000LL;
        }
        CMutableTransaction txChild[3];
        for (int i = 0; i < 3; i++)
        {
            txChild[i].vin.resize(1);
            txChild[i].vin[0].scriptSig = CScript() << OP_11;
            txChild[i].vin[0].prevout.hash = txParent.GetHash();
            txChild[i].vin[0].prevout.n = i;
            txChild[i].vout.resize(1);
            txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            txChild[i].vout[0].nValue = 11000LL;
        }
        CMutableTransaction txGrandChild[3];
        for (int i = 0; i < 3; i++)
        {
            txGrandChild[i].vin.resize(1);
            txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
            txGrandChild[i].vin[0].prevout.hash = txChild[i].GetHash();
            txGrandChild[i].vin[0].prevout.n = 0;
            txGrandChild[i].vout.resize(1);
            txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            txGrandChild[i].vout[0].nValue = 11000LL;
        }


        CTxMemPool testPool;

        // Nothing in pool, remove should do nothing:
        unsigned int poolSize = testPool.size();
        testPool.removeRecursive(txParent);
        BOOST_CHECK_EQUAL(testPool.size(), poolSize);

        // Just the parent:
        testPool.addUnchecked(txParent.GetHash(), entry.FromTx(txParent));
        poolSize = testPool.size();
        testPool.removeRecursive(txParent);
        BOOST_CHECK_EQUAL(testPool.size(), poolSize - 1);

        // Parent, children, grandchildren:
        testPool.addUnchecked(txParent.GetHash(), entry.FromTx(txParent));
        for (int i = 0; i < 3; i++)
        {
            testPool.addUnchecked(txChild[i].GetHash(), entry.FromTx(txChild[i]));
            testPool.addUnchecked(txGrandChild[i].GetHash(), entry.FromTx(txGrandChild[i]));
        }
        // Remove Child[0], GrandChild[0] should be removed:
        poolSize = testPool.size();
        testPool.removeRecursive(txChild[0]);
        BOOST_CHECK_EQUAL(testPool.size(), poolSize - 2);
        // ... make sure grandchild and child are gone:
        poolSize = testPool.size();
        testPool.removeRecursive(txGrandChild[0]);
        BOOST_CHECK_EQUAL(testPool.size(), poolSize);
        poolSize = testPool.size();
        testPool.removeRecursive(txChild[0]);
        BOOST_CHECK_EQUAL(testPool.size(), poolSize);
        // Remove parent, all children/grandchildren should go:
        poolSize = testPool.size();
        testPool.removeRecursive(txParent);
        BOOST_CHECK_EQUAL(testPool.size(), poolSize - 5);
        BOOST_CHECK_EQUAL(testPool.size(), (uint64_t)0);

        // Add children and grandchildren, but NOT the parent (simulate the parent being in a block)
        for (int i = 0; i < 3; i++)
        {
            testPool.addUnchecked(txChild[i].GetHash(), entry.FromTx(txChild[i]));
            testPool.addUnchecked(txGrandChild[i].GetHash(), entry.FromTx(txGrandChild[i]));
        }
        // Now remove the parent, as might happen if a block-re-org occurs but the parent cannot be
        // put into the mempool (maybe because it is non-standard):
        poolSize = testPool.size();
        testPool.removeRecursive(txParent);
        BOOST_CHECK_EQUAL(testPool.size(), poolSize - 6);
        BOOST_CHECK_EQUAL(testPool.size(), (uint64_t)0);
    }

    template<typename name>
    void CheckSort(CTxMemPool &pool, std::vector<std::string> &sortedOrder)
    {
        BOOST_CHECK_EQUAL(pool.size(), sortedOrder.size());
        typename CTxMemPool::indexed_transaction_set::index<name>::type::iterator it = pool.mapTx.get<name>().begin();
        int count = 0;
        for (; it != pool.mapTx.get<name>().end(); ++it, ++count)
        {
            BOOST_CHECK_EQUAL(it->GetTx().GetHash().ToString(), sortedOrder[count]);
        }
    }

    BOOST_AUTO_TEST_CASE(mempool_indexing_test)
    {
        BOOST_TEST_MESSAGE("Running Mempool Indexing Test");

        CTxMemPool pool;
        TestMemPoolEntryHelper entry;

        /* 3rd highest fee */
        CMutableTransaction tx1 = CMutableTransaction();
        tx1.vout.resize(1);
        tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx1.vout[0].nValue = 10 * COIN;
        pool.addUnchecked(tx1.GetHash(), entry.Fee(10000LL).FromTx(tx1));

        /* highest fee */
        CMutableTransaction tx2 = CMutableTransaction();
        tx2.vout.resize(1);
        tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx2.vout[0].nValue = 2 * COIN;
        pool.addUnchecked(tx2.GetHash(), entry.Fee(20000LL).FromTx(tx2));

        /* lowest fee */
        CMutableTransaction tx3 = CMutableTransaction();
        tx3.vout.resize(1);
        tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx3.vout[0].nValue = 5 * COIN;
        pool.addUnchecked(tx3.GetHash(), entry.Fee(0LL).FromTx(tx3));

        /* 2nd highest fee */
        CMutableTransaction tx4 = CMutableTransaction();
        tx4.vout.resize(1);
        tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx4.vout[0].nValue = 6 * COIN;
        pool.addUnchecked(tx4.GetHash(), entry.Fee(15000LL).FromTx(tx4));

        /* equal fee rate to tx1, but newer */
        CMutableTransaction tx5 = CMutableTransaction();
        tx5.vout.resize(1);
        tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx5.vout[0].nValue = 11 * COIN;
        entry.nTime = 1;
        pool.addUnchecked(tx5.GetHash(), entry.Fee(10000LL).FromTx(tx5));
        BOOST_CHECK_EQUAL(pool.size(), (uint64_t)5);

        std::vector<std::string> sortedOrder;
        sortedOrder.resize(5);
        sortedOrder[0] = tx3.GetHash().ToString(); // 0
        sortedOrder[1] = tx5.GetHash().ToString(); // 10000
        sortedOrder[2] = tx1.GetHash().ToString(); // 10000
        sortedOrder[3] = tx4.GetHash().ToString(); // 15000
        sortedOrder[4] = tx2.GetHash().ToString(); // 20000
        CheckSort<descendant_score>(pool, sortedOrder);

        /* low fee but with high fee child */
        /* tx6 -> tx7 -> tx8, tx9 -> tx10 */
        CMutableTransaction tx6 = CMutableTransaction();
        tx6.vout.resize(1);
        tx6.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx6.vout[0].nValue = 20 * COIN;
        pool.addUnchecked(tx6.GetHash(), entry.Fee(0LL).FromTx(tx6));
        BOOST_CHECK_EQUAL(pool.size(), (uint64_t)6);
        // Check that at this point, tx6 is sorted low
        sortedOrder.insert(sortedOrder.begin(), tx6.GetHash().ToString());
        CheckSort<descendant_score>(pool, sortedOrder);

        CTxMemPool::setEntries setAncestors;
        setAncestors.insert(pool.mapTx.find(tx6.GetHash()));
        CMutableTransaction tx7 = CMutableTransaction();
        tx7.vin.resize(1);
        tx7.vin[0].prevout = COutPoint(tx6.GetHash(), 0);
        tx7.vin[0].scriptSig = CScript() << OP_11;
        tx7.vout.resize(2);
        tx7.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx7.vout[0].nValue = 10 * COIN;
        tx7.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx7.vout[1].nValue = 1 * COIN;

        CTxMemPool::setEntries setAncestorsCalculated;
        std::string dummy;
        BOOST_CHECK_EQUAL(pool.CalculateMemPoolAncestors(entry.Fee(2000000LL).FromTx(tx7), setAncestorsCalculated, 100, 1000000, 1000, 1000000, dummy), true);
        BOOST_CHECK(setAncestorsCalculated == setAncestors);

        pool.addUnchecked(tx7.GetHash(), entry.FromTx(tx7), setAncestors);
        BOOST_CHECK_EQUAL(pool.size(), (uint64_t)7);

        // Now tx6 should be sorted higher (high fee child): tx7, tx6, tx2, ...
        sortedOrder.erase(sortedOrder.begin());
        sortedOrder.push_back(tx6.GetHash().ToString());
        sortedOrder.push_back(tx7.GetHash().ToString());
        CheckSort<descendant_score>(pool, sortedOrder);

        /* low fee child of tx7 */
        CMutableTransaction tx8 = CMutableTransaction();
        tx8.vin.resize(1);
        tx8.vin[0].prevout = COutPoint(tx7.GetHash(), 0);
        tx8.vin[0].scriptSig = CScript() << OP_11;
        tx8.vout.resize(1);
        tx8.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx8.vout[0].nValue = 10 * COIN;
        setAncestors.insert(pool.mapTx.find(tx7.GetHash()));
        pool.addUnchecked(tx8.GetHash(), entry.Fee(0LL).Time(2).FromTx(tx8), setAncestors);

        // Now tx8 should be sorted low, but tx6/tx both high
        sortedOrder.insert(sortedOrder.begin(), tx8.GetHash().ToString());
        CheckSort<descendant_score>(pool, sortedOrder);

        /* low fee child of tx7 */
        CMutableTransaction tx9 = CMutableTransaction();
        tx9.vin.resize(1);
        tx9.vin[0].prevout = COutPoint(tx7.GetHash(), 1);
        tx9.vin[0].scriptSig = CScript() << OP_11;
        tx9.vout.resize(1);
        tx9.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx9.vout[0].nValue = 1 * COIN;
        pool.addUnchecked(tx9.GetHash(), entry.Fee(0LL).Time(3).FromTx(tx9), setAncestors);

        // tx9 should be sorted low
        BOOST_CHECK_EQUAL(pool.size(), (uint64_t)9);
        sortedOrder.insert(sortedOrder.begin(), tx9.GetHash().ToString());
        CheckSort<descendant_score>(pool, sortedOrder);

        std::vector<std::string> snapshotOrder = sortedOrder;

        setAncestors.insert(pool.mapTx.find(tx8.GetHash()));
        setAncestors.insert(pool.mapTx.find(tx9.GetHash()));
        /* tx10 depends on tx8 and tx9 and has a high fee*/
        CMutableTransaction tx10 = CMutableTransaction();
        tx10.vin.resize(2);
        tx10.vin[0].prevout = COutPoint(tx8.GetHash(), 0);
        tx10.vin[0].scriptSig = CScript() << OP_11;
        tx10.vin[1].prevout = COutPoint(tx9.GetHash(), 0);
        tx10.vin[1].scriptSig = CScript() << OP_11;
        tx10.vout.resize(1);
        tx10.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx10.vout[0].nValue = 10 * COIN;

        setAncestorsCalculated.clear();
        BOOST_CHECK_EQUAL(pool.CalculateMemPoolAncestors(entry.Fee(200000LL).Time(4).FromTx(tx10), setAncestorsCalculated, 100, 1000000, 1000, 1000000, dummy), true);
        BOOST_CHECK(setAncestorsCalculated == setAncestors);

        pool.addUnchecked(tx10.GetHash(), entry.FromTx(tx10), setAncestors);

        /**
         *  tx8 and tx9 should both now be sorted higher
         *  Final order after tx10 is added:
         *
         *  tx3 = 0 (1)
         *  tx5 = 10000 (1)
         *  tx1 = 10000 (1)
         *  tx4 = 15000 (1)
         *  tx2 = 20000 (1)
         *  tx9 = 200k (2 txs)
         *  tx8 = 200k (2 txs)
         *  tx10 = 200k (1 tx)
         *  tx6 = 2.2M (5 txs)
         *  tx7 = 2.2M (4 txs)
         */
        sortedOrder.erase(sortedOrder.begin(), sortedOrder.begin() + 2); // take out tx9, tx8 from the beginning
        sortedOrder.insert(sortedOrder.begin() + 5, tx9.GetHash().ToString());
        sortedOrder.insert(sortedOrder.begin() + 6, tx8.GetHash().ToString());
        sortedOrder.insert(sortedOrder.begin() + 7, tx10.GetHash().ToString()); // tx10 is just before tx6
        CheckSort<descendant_score>(pool, sortedOrder);

        // there should be 10 transactions in the mempool
        BOOST_CHECK_EQUAL(pool.size(), (uint64_t)10);

        // Now try removing tx10 and verify the sort order returns to normal
        pool.removeRecursive(pool.mapTx.find(tx10.GetHash())->GetTx());
        CheckSort<descendant_score>(pool, snapshotOrder);

        pool.removeRecursive(pool.mapTx.find(tx9.GetHash())->GetTx());
        pool.removeRecursive(pool.mapTx.find(tx8.GetHash())->GetTx());
        /* Now check the sort on the mining score index.
         * Final order should be:
         *
         * tx7 (2M)
         * tx2 (20k)
         * tx4 (15000)
         * tx1/tx5 (10000)
         * tx3/6 (0)
         * (Ties resolved by hash)
         */
        sortedOrder.clear();
        sortedOrder.push_back(tx7.GetHash().ToString());
        sortedOrder.push_back(tx2.GetHash().ToString());
        sortedOrder.push_back(tx4.GetHash().ToString());
        if (tx1.GetHash() < tx5.GetHash())
        {
            sortedOrder.push_back(tx5.GetHash().ToString());
            sortedOrder.push_back(tx1.GetHash().ToString());
        } else
        {
            sortedOrder.push_back(tx1.GetHash().ToString());
            sortedOrder.push_back(tx5.GetHash().ToString());
        }
        if (tx3.GetHash() < tx6.GetHash())
        {
            sortedOrder.push_back(tx6.GetHash().ToString());
            sortedOrder.push_back(tx3.GetHash().ToString());
        } else
        {
            sortedOrder.push_back(tx3.GetHash().ToString());
            sortedOrder.push_back(tx6.GetHash().ToString());
        }
        CheckSort<mining_score>(pool, sortedOrder);
    }

    BOOST_AUTO_TEST_CASE(mempool_ancestor_indexing_test)
    {
        BOOST_TEST_MESSAGE("Running Mempool Ancestor Indexing Test");

        CTxMemPool pool;
        TestMemPoolEntryHelper entry;

        /* 3rd highest fee */
        CMutableTransaction tx1 = CMutableTransaction();
        tx1.vout.resize(1);
        tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx1.vout[0].nValue = 10 * COIN;
        pool.addUnchecked(tx1.GetHash(), entry.Fee(10000LL).FromTx(tx1));

        /* highest fee */
        CMutableTransaction tx2 = CMutableTransaction();
        tx2.vout.resize(1);
        tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx2.vout[0].nValue = 2 * COIN;
        pool.addUnchecked(tx2.GetHash(), entry.Fee(20000LL).FromTx(tx2));
        uint64_t tx2Size = GetVirtualTransactionSize(tx2);

        /* lowest fee */
        CMutableTransaction tx3 = CMutableTransaction();
        tx3.vout.resize(1);
        tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx3.vout[0].nValue = 5 * COIN;
        pool.addUnchecked(tx3.GetHash(), entry.Fee(0LL).FromTx(tx3));

        /* 2nd highest fee */
        CMutableTransaction tx4 = CMutableTransaction();
        tx4.vout.resize(1);
        tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx4.vout[0].nValue = 6 * COIN;
        pool.addUnchecked(tx4.GetHash(), entry.Fee(15000LL).FromTx(tx4));

        /* equal fee rate to tx1, but newer */
        CMutableTransaction tx5 = CMutableTransaction();
        tx5.vout.resize(1);
        tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx5.vout[0].nValue = 11 * COIN;
        pool.addUnchecked(tx5.GetHash(), entry.Fee(10000LL).FromTx(tx5));
        BOOST_CHECK_EQUAL(pool.size(), (uint64_t)5);

        std::vector<std::string> sortedOrder;
        sortedOrder.resize(5);
        sortedOrder[0] = tx2.GetHash().ToString(); // 20000
        sortedOrder[1] = tx4.GetHash().ToString(); // 15000
        // tx1 and tx5 are both 10000
        // Ties are broken by hash, not timestamp, so determine which
        // hash comes first.
        if (tx1.GetHash() < tx5.GetHash())
        {
            sortedOrder[2] = tx1.GetHash().ToString();
            sortedOrder[3] = tx5.GetHash().ToString();
        } else
        {
            sortedOrder[2] = tx5.GetHash().ToString();
            sortedOrder[3] = tx1.GetHash().ToString();
        }
        sortedOrder[4] = tx3.GetHash().ToString(); // 0

        CheckSort<ancestor_score>(pool, sortedOrder);

        /* low fee parent with high fee child */
        /* tx6 (0) -> tx7 (high) */
        CMutableTransaction tx6 = CMutableTransaction();
        tx6.vout.resize(1);
        tx6.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx6.vout[0].nValue = 20 * COIN;
        uint64_t tx6Size = GetVirtualTransactionSize(tx6);

        pool.addUnchecked(tx6.GetHash(), entry.Fee(0LL).FromTx(tx6));
        BOOST_CHECK_EQUAL(pool.size(), (uint64_t)6);
        // Ties are broken by hash
        if (tx3.GetHash() < tx6.GetHash())
            sortedOrder.push_back(tx6.GetHash().ToString());
        else
            sortedOrder.insert(sortedOrder.end() - 1, tx6.GetHash().ToString());

        CheckSort<ancestor_score>(pool, sortedOrder);

        CMutableTransaction tx7 = CMutableTransaction();
        tx7.vin.resize(1);
        tx7.vin[0].prevout = COutPoint(tx6.GetHash(), 0);
        tx7.vin[0].scriptSig = CScript() << OP_11;
        tx7.vout.resize(1);
        tx7.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx7.vout[0].nValue = 10 * COIN;
        uint64_t tx7Size = GetVirtualTransactionSize(tx7);

        /* set the fee to just below tx2's feerate when including ancestor */
        CAmount fee = (20000 / tx2Size) * (tx7Size + tx6Size) - 1;

        pool.addUnchecked(tx7.GetHash(), entry.Fee(fee).FromTx(tx7));
        BOOST_CHECK_EQUAL(pool.size(), (uint64_t)7);
        sortedOrder.insert(sortedOrder.begin() + 1, tx7.GetHash().ToString());
        CheckSort<ancestor_score>(pool, sortedOrder);

        /* after tx6 is mined, tx7 should move up in the sort */
        std::vector<CTransactionRef> vtx;
        vtx.push_back(MakeTransactionRef(tx6));
        pool.removeForBlock(vtx, 1);

        sortedOrder.erase(sortedOrder.begin() + 1);
        // Ties are broken by hash
        if (tx3.GetHash() < tx6.GetHash())
            sortedOrder.pop_back();
        else
            sortedOrder.erase(sortedOrder.end() - 2);
        sortedOrder.insert(sortedOrder.begin(), tx7.GetHash().ToString());
        CheckSort<ancestor_score>(pool, sortedOrder);
    }


    BOOST_AUTO_TEST_CASE(mempool_size_limit_test)
    {
        BOOST_TEST_MESSAGE("Running Mempool Size Limit Test");

        CTxMemPool pool;
        TestMemPoolEntryHelper entry;

        CMutableTransaction tx1 = CMutableTransaction();
        tx1.vin.resize(1);
        tx1.vin[0].scriptSig = CScript() << OP_1;
        tx1.vout.resize(1);
        tx1.vout[0].scriptPubKey = CScript() << OP_1 << OP_EQUAL;
        tx1.vout[0].nValue = 10 * COIN;
        pool.addUnchecked(tx1.GetHash(), entry.Fee(10000LL).FromTx(tx1));

        CMutableTransaction tx2 = CMutableTransaction();
        tx2.vin.resize(1);
        tx2.vin[0].scriptSig = CScript() << OP_2;
        tx2.vout.resize(1);
        tx2.vout[0].scriptPubKey = CScript() << OP_2 << OP_EQUAL;
        tx2.vout[0].nValue = 10 * COIN;
        pool.addUnchecked(tx2.GetHash(), entry.Fee(5000LL).FromTx(tx2));

        pool.TrimToSize(pool.DynamicMemoryUsage()); // should do nothing
        BOOST_CHECK(pool.exists(tx1.GetHash()));
        BOOST_CHECK(pool.exists(tx2.GetHash()));

        pool.TrimToSize(pool.DynamicMemoryUsage() * 3 / 4); // should remove the lower-feerate transaction
        BOOST_CHECK(pool.exists(tx1.GetHash()));
        BOOST_CHECK(!pool.exists(tx2.GetHash()));

        pool.addUnchecked(tx2.GetHash(), entry.FromTx(tx2));
        CMutableTransaction tx3 = CMutableTransaction();
        tx3.vin.resize(1);
        tx3.vin[0].prevout = COutPoint(tx2.GetHash(), 0);
        tx3.vin[0].scriptSig = CScript() << OP_2;
        tx3.vout.resize(1);
        tx3.vout[0].scriptPubKey = CScript() << OP_3 << OP_EQUAL;
        tx3.vout[0].nValue = 10 * COIN;
        pool.addUnchecked(tx3.GetHash(), entry.Fee(20000LL).FromTx(tx3));

        pool.TrimToSize(pool.DynamicMemoryUsage() * 3 / 4); // tx3 should pay for tx2 (CPFP)
        BOOST_CHECK(!pool.exists(tx1.GetHash()));
        BOOST_CHECK(pool.exists(tx2.GetHash()));
        BOOST_CHECK(pool.exists(tx3.GetHash()));

        pool.TrimToSize(GetVirtualTransactionSize(tx1)); // mempool is limited to tx1's size in memory usage, so nothing fits
        BOOST_CHECK(!pool.exists(tx1.GetHash()));
        BOOST_CHECK(!pool.exists(tx2.GetHash()));
        BOOST_CHECK(!pool.exists(tx3.GetHash()));

        CFeeRate maxFeeRateRemoved(25000, GetVirtualTransactionSize(tx3) + GetVirtualTransactionSize(tx2));
        BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), maxFeeRateRemoved.GetFeePerK() + 1000);

        CMutableTransaction tx4 = CMutableTransaction();
        tx4.vin.resize(2);
        tx4.vin[0].prevout.SetNull();
        tx4.vin[0].scriptSig = CScript() << OP_4;
        tx4.vin[1].prevout.SetNull();
        tx4.vin[1].scriptSig = CScript() << OP_4;
        tx4.vout.resize(2);
        tx4.vout[0].scriptPubKey = CScript() << OP_4 << OP_EQUAL;
        tx4.vout[0].nValue = 10 * COIN;
        tx4.vout[1].scriptPubKey = CScript() << OP_4 << OP_EQUAL;
        tx4.vout[1].nValue = 10 * COIN;

        CMutableTransaction tx5 = CMutableTransaction();
        tx5.vin.resize(2);
        tx5.vin[0].prevout = COutPoint(tx4.GetHash(), 0);
        tx5.vin[0].scriptSig = CScript() << OP_4;
        tx5.vin[1].prevout.SetNull();
        tx5.vin[1].scriptSig = CScript() << OP_5;
        tx5.vout.resize(2);
        tx5.vout[0].scriptPubKey = CScript() << OP_5 << OP_EQUAL;
        tx5.vout[0].nValue = 10 * COIN;
        tx5.vout[1].scriptPubKey = CScript() << OP_5 << OP_EQUAL;
        tx5.vout[1].nValue = 10 * COIN;

        CMutableTransaction tx6 = CMutableTransaction();
        tx6.vin.resize(2);
        tx6.vin[0].prevout = COutPoint(tx4.GetHash(), 1);
        tx6.vin[0].scriptSig = CScript() << OP_4;
        tx6.vin[1].prevout.SetNull();
        tx6.vin[1].scriptSig = CScript() << OP_6;
        tx6.vout.resize(2);
        tx6.vout[0].scriptPubKey = CScript() << OP_6 << OP_EQUAL;
        tx6.vout[0].nValue = 10 * COIN;
        tx6.vout[1].scriptPubKey = CScript() << OP_6 << OP_EQUAL;
        tx6.vout[1].nValue = 10 * COIN;

        CMutableTransaction tx7 = CMutableTransaction();
        tx7.vin.resize(2);
        tx7.vin[0].prevout = COutPoint(tx5.GetHash(), 0);
        tx7.vin[0].scriptSig = CScript() << OP_5;
        tx7.vin[1].prevout = COutPoint(tx6.GetHash(), 0);
        tx7.vin[1].scriptSig = CScript() << OP_6;
        tx7.vout.resize(2);
        tx7.vout[0].scriptPubKey = CScript() << OP_7 << OP_EQUAL;
        tx7.vout[0].nValue = 10 * COIN;
        tx7.vout[1].scriptPubKey = CScript() << OP_7 << OP_EQUAL;
        tx7.vout[1].nValue = 10 * COIN;

        pool.addUnchecked(tx4.GetHash(), entry.Fee(7000LL).FromTx(tx4));
        pool.addUnchecked(tx5.GetHash(), entry.Fee(1000LL).FromTx(tx5));
        pool.addUnchecked(tx6.GetHash(), entry.Fee(1100LL).FromTx(tx6));
        pool.addUnchecked(tx7.GetHash(), entry.Fee(9000LL).FromTx(tx7));

        // we only require this remove, at max, 2 txn, because its not clear what we're really optimizing for aside from that
        pool.TrimToSize(pool.DynamicMemoryUsage() - 1);
        BOOST_CHECK(pool.exists(tx4.GetHash()));
        BOOST_CHECK(pool.exists(tx6.GetHash()));
        BOOST_CHECK(!pool.exists(tx7.GetHash()));

        if (!pool.exists(tx5.GetHash()))
            pool.addUnchecked(tx5.GetHash(), entry.Fee(1000LL).FromTx(tx5));
        pool.addUnchecked(tx7.GetHash(), entry.Fee(9000LL).FromTx(tx7));

        pool.TrimToSize(pool.DynamicMemoryUsage() / 2); // should maximize mempool size by only removing 5/7
        BOOST_CHECK(pool.exists(tx4.GetHash()));
        BOOST_CHECK(!pool.exists(tx5.GetHash()));
        BOOST_CHECK(pool.exists(tx6.GetHash()));
        BOOST_CHECK(!pool.exists(tx7.GetHash()));

        pool.addUnchecked(tx5.GetHash(), entry.Fee(1000LL).FromTx(tx5));
        pool.addUnchecked(tx7.GetHash(), entry.Fee(9000LL).FromTx(tx7));

        std::vector<CTransactionRef> vtx;
        SetMockTime(42);
        SetMockTime(42 + CTxMemPool::ROLLING_FEE_HALFLIFE);
        BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), maxFeeRateRemoved.GetFeePerK() + 1000);
        // ... we should keep the same min fee until we get a block
        pool.removeForBlock(vtx, 1);
        SetMockTime(42 + 2 * CTxMemPool::ROLLING_FEE_HALFLIFE);
        BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), llround((maxFeeRateRemoved.GetFeePerK() + 1000) / 2.0));
        // ... then feerate should drop 1/2 each halflife

        SetMockTime(42 + 2 * CTxMemPool::ROLLING_FEE_HALFLIFE + CTxMemPool::ROLLING_FEE_HALFLIFE / 2);
        BOOST_CHECK_EQUAL(pool.GetMinFee(pool.DynamicMemoryUsage() * 5 / 2).GetFeePerK(), llround((maxFeeRateRemoved.GetFeePerK() + 1000) / 4.0));
        // ... with a 1/2 halflife when mempool is < 1/2 its target size

        SetMockTime(42 + 2 * CTxMemPool::ROLLING_FEE_HALFLIFE + CTxMemPool::ROLLING_FEE_HALFLIFE / 2 + CTxMemPool::ROLLING_FEE_HALFLIFE / 4);
        BOOST_CHECK_EQUAL(pool.GetMinFee(pool.DynamicMemoryUsage() * 9 / 2).GetFeePerK(), llround((maxFeeRateRemoved.GetFeePerK() + 1000) / 8.0));
        // ... with a 1/4 halflife when mempool is < 1/4 its target size

        SetMockTime(42 + 7 * CTxMemPool::ROLLING_FEE_HALFLIFE + CTxMemPool::ROLLING_FEE_HALFLIFE / 2 + CTxMemPool::ROLLING_FEE_HALFLIFE / 4);
        BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), 1000);
        // ... but feerate should never drop below 1000

        SetMockTime(42 + 8 * CTxMemPool::ROLLING_FEE_HALFLIFE + CTxMemPool::ROLLING_FEE_HALFLIFE / 2 + CTxMemPool::ROLLING_FEE_HALFLIFE / 4);
        BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), 0);
        // ... unless it has gone all the way to 0 (after getting past 1000/2)

        SetMockTime(0);
    }

    BOOST_AUTO_TEST_CASE(rip25_reorg_purges_preactivation_policy_transactions)
    {
        LOCK(cs_main);
        mempool.clear();

        const std::vector<unsigned char> program(32, 0x42);
        const CScript pqScript = CScript() << OP_2 << program;
        const CScript p2shPQScript = GetScriptForDestination(CScriptID(pqScript));
        const CScript ordinaryScript = CScript() << OP_TRUE;

        CTxIn helperInput;
        helperInput.scriptSig << std::vector<unsigned char>(pqScript.begin(), pqScript.end());
        BOOST_CHECK(SpendsPQWitnessV2Program(helperInput, pqScript));
        BOOST_CHECK(SpendsPQWitnessV2Program(helperInput, p2shPQScript));
        BOOST_CHECK(!SpendsPQWitnessV2Program(helperInput, ordinaryScript));

        CTxIn multiplePushInput;
        multiplePushInput.scriptSig << std::vector<unsigned char>(1, 0x01)
                                    << std::vector<unsigned char>(pqScript.begin(), pqScript.end());
        BOOST_CHECK(!SpendsPQWitnessV2Program(multiplePushInput, p2shPQScript));

        CTxIn malformedPushInput;
        malformedPushInput.scriptSig << OP_PUSHDATA1;
        BOOST_CHECK(!SpendsPQWitnessV2Program(malformedPushInput, p2shPQScript));

        const CScript wrongP2SH = GetScriptForDestination(CScriptID(ordinaryScript));
        BOOST_CHECK(!SpendsPQWitnessV2Program(helperInput, wrongP2SH));

        auto addFundingCoin = [&](const CScript& script) {
            const COutPoint outpoint(InsecureRand256(), 0);
            pcoinsTip->AddCoin(outpoint,
                               Coin(CTxOut(10 * COIN, script), chainActive.Height(), false),
                               false);
            return outpoint;
        };

        CMutableTransaction pqCreation;
        pqCreation.vin.resize(1);
        pqCreation.vin[0].prevout = addFundingCoin(ordinaryScript);
        pqCreation.vout.resize(1);
        pqCreation.vout[0] = CTxOut(9 * COIN, pqScript);

        CMutableTransaction descendant;
        descendant.vin.resize(1);
        descendant.vin[0].prevout = COutPoint(pqCreation.GetHash(), 0);
        descendant.vout.resize(1);
        descendant.vout[0] = CTxOut(8 * COIN, ordinaryScript);

        CMutableTransaction nativeSpend;
        nativeSpend.vin.resize(1);
        nativeSpend.vin[0].prevout = addFundingCoin(pqScript);
        nativeSpend.vin[0].scriptWitness.stack.emplace_back(mldsa::SIGNATURE_BYTES, 0x11);
        nativeSpend.vin[0].scriptWitness.stack.emplace_back(mldsa::PUBLICKEY_BYTES, 0x22);
        nativeSpend.vout.resize(1);
        nativeSpend.vout[0] = CTxOut(9 * COIN, ordinaryScript);

        CMutableTransaction wrappedSpend;
        wrappedSpend.vin.resize(1);
        wrappedSpend.vin[0].prevout = addFundingCoin(p2shPQScript);
        wrappedSpend.vin[0].scriptSig << std::vector<unsigned char>(pqScript.begin(), pqScript.end());
        wrappedSpend.vin[0].scriptWitness.stack.emplace_back(mldsa::SIGNATURE_BYTES, 0x33);
        wrappedSpend.vin[0].scriptWitness.stack.emplace_back(mldsa::PUBLICKEY_BYTES, 0x44);
        wrappedSpend.vout.resize(1);
        wrappedSpend.vout[0] = CTxOut(9 * COIN, ordinaryScript);

        CMutableTransaction wrappedParent;
        wrappedParent.vin.resize(1);
        wrappedParent.vin[0].prevout = addFundingCoin(ordinaryScript);
        wrappedParent.vout.resize(1);
        wrappedParent.vout[0] = CTxOut(9 * COIN, p2shPQScript);

        CMutableTransaction wrappedChild;
        wrappedChild.vin.resize(1);
        wrappedChild.vin[0].prevout = COutPoint(wrappedParent.GetHash(), 0);
        wrappedChild.vin[0].scriptSig << std::vector<unsigned char>(pqScript.begin(), pqScript.end());
        wrappedChild.vin[0].scriptWitness.stack.emplace_back(mldsa::SIGNATURE_BYTES, 0x55);
        wrappedChild.vin[0].scriptWitness.stack.emplace_back(mldsa::PUBLICKEY_BYTES, 0x66);
        wrappedChild.vout.resize(1);
        wrappedChild.vout[0] = CTxOut(8 * COIN, ordinaryScript);

        CMutableTransaction unrelated;
        unrelated.vin.resize(1);
        unrelated.vin[0].prevout = addFundingCoin(ordinaryScript);
        unrelated.vout.resize(1);
        unrelated.vout[0] = CTxOut(9 * COIN, ordinaryScript);

        TestMemPoolEntryHelper entry;
        mempool.addUnchecked(pqCreation.GetHash(), entry.FromTx(pqCreation));
        mempool.addUnchecked(descendant.GetHash(), entry.FromTx(descendant));
        mempool.addUnchecked(nativeSpend.GetHash(), entry.FromTx(nativeSpend));
        mempool.addUnchecked(wrappedSpend.GetHash(), entry.FromTx(wrappedSpend));
        mempool.addUnchecked(wrappedParent.GetHash(), entry.FromTx(wrappedParent));
        mempool.addUnchecked(wrappedChild.GetHash(), entry.FromTx(wrappedChild));
        mempool.addUnchecked(unrelated.GetHash(), entry.FromTx(unrelated));
        BOOST_REQUIRE_EQUAL(mempool.size(), 7U);

        const TxAssetDeploymentContext noAssets = {};
        mempool.removeForReorg(pcoinsTip, chainActive.Height() + 1,
                               STANDARD_LOCKTIME_VERIFY_FLAGS, true, noAssets);
        BOOST_REQUIRE_EQUAL(mempool.size(), 7U);

        mempool.removeForReorg(pcoinsTip, chainActive.Height() + 1,
                               STANDARD_LOCKTIME_VERIFY_FLAGS, false, noAssets);

        BOOST_CHECK(!mempool.exists(pqCreation.GetHash()));
        BOOST_CHECK(!mempool.exists(descendant.GetHash()));
        BOOST_CHECK(!mempool.exists(nativeSpend.GetHash()));
        BOOST_CHECK(!mempool.exists(wrappedSpend.GetHash()));
        BOOST_CHECK(mempool.exists(wrappedParent.GetHash()));
        BOOST_CHECK(!mempool.exists(wrappedChild.GetHash()));
        BOOST_CHECK(mempool.exists(unrelated.GetHash()));

        mempool.clear();
    }

    BOOST_AUTO_TEST_CASE(rip25_preactivation_policy_rejects_future_witness_spends)
    {
        LOCK(cs_main);
        mempool.clear();

        const Consensus::Params& consensus = GetParams().GetConsensus();
        BOOST_REQUIRE(GetParams().RequireStandard());
        BOOST_REQUIRE(!consensus.nPQHybridEnabled);
        BOOST_REQUIRE(!IsPQWitnessDiscountActive(chainActive.Tip(), consensus));

        const std::vector<unsigned char> program(32, 0x42);
        const CScript pqScript = CScript() << OP_2 << program;
        const CScript p2shPQScript = GetScriptForDestination(CScriptID(pqScript));
        const CScript ordinaryScript =
            GetScriptForDestination(CScriptID(CScript() << OP_TRUE));

        auto addFundingCoin = [&](const CScript& script) {
            const COutPoint outpoint(InsecureRand256(), 0);
            pcoinsTip->AddCoin(outpoint,
                               Coin(CTxOut(10 * COIN, script), chainActive.Height(), false),
                               false);
            return outpoint;
        };

        auto makeInvalidPQSpend = [&](const CScript& fundingScript, bool p2shWrapped) {
            CMutableTransaction spend;
            spend.vin.emplace_back(addFundingCoin(fundingScript));
            if (p2shWrapped) {
                spend.vin[0].scriptSig <<
                    std::vector<unsigned char>(pqScript.begin(), pqScript.end());
            }
            // The stack satisfies preactivation shape policy but is not bound
            // to the program and does not contain a valid ML-DSA signature.
            spend.vin[0].scriptWitness.stack.emplace_back(mldsa::SIGNATURE_BYTES, 0x11);
            spend.vin[0].scriptWitness.stack.emplace_back(mldsa::PUBLICKEY_BYTES, 0x22);
            spend.vout.emplace_back(9 * COIN, ordinaryScript);
            return MakeTransactionRef(std::move(spend));
        };

        const std::vector<CTransactionRef> candidates{
            makeInvalidPQSpend(pqScript, false),
            makeInvalidPQSpend(p2shPQScript, true),
        };

        for (const CTransactionRef& tx : candidates) {
            BOOST_REQUIRE(IsWitnessStandard(*tx, *pcoinsTip));

            // Legacy consensus deliberately treats witness-v2 as a future
            // witness program before activation.
            ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;
            BOOST_REQUIRE(VerifyScript(
                tx->vin[0].scriptSig,
                pcoinsTip->AccessCoin(tx->vin[0].prevout).out.scriptPubKey,
                &tx->vin[0].scriptWitness,
                SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS,
                TransactionSignatureChecker(tx.get(), 0, 10 * COIN), &error));
            BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

            // Default node policy nevertheless rejects every unknown witness
            // version. Therefore the invalid transaction cannot survive in
            // the mempool until the forward PQ activation transition.
            CValidationState state;
            BOOST_CHECK(!AcceptToMemoryPool(mempool, state, tx, nullptr, nullptr,
                                            false, 0));
            BOOST_CHECK_EQUAL(state.GetRejectCode(), REJECT_NONSTANDARD);
            BOOST_CHECK_EQUAL(
                state.GetRejectReason(),
                "non-mandatory-script-verify-flag (Witness version reserved for soft-fork upgrades)");
            BOOST_CHECK(!mempool.exists(tx->GetHash()));
        }

        BOOST_CHECK_EQUAL(mempool.size(), 0U);
    }

    BOOST_AUTO_TEST_CASE(transfer_overflow_activation_purges_invalid_graph)
    {
        LOCK(cs_main);
        mempool.clear();

        class ScopedAssetsDeployment
        {
        private:
            const bool previous;

        public:
            ScopedAssetsDeployment() : previous(AreAssetsDeployed())
            {
                SetAssetsDeployed(true);
            }
            ~ScopedAssetsDeployment()
            {
                SetAssetsDeployed(previous);
            }
        } assetsDeployment;

        const std::string assetName = "MEMPOOL_OVERFLOW";
        const CNewAsset metadata(assetName, 100, 8, 1, 0, "");
        BOOST_REQUIRE(passets->AddNewAsset(metadata, GetParams().GlobalBurnAddress(),
                                           chainActive.Height(), chainActive.Tip()->GetBlockHash()));

        auto makeAssetOutput = [&](CAmount amount) {
            CScript script = GetScriptForDestination(
                DecodeDestination(GetParams().GlobalBurnAddress()));
            CAssetTransfer(assetName, amount).ConstructTransaction(script);
            return CTxOut(0, script);
        };
        auto addCoin = [&](const CTxOut& output) {
            const COutPoint outpoint(InsecureRand256(), 0);
            pcoinsTip->AddCoin(outpoint,
                               Coin(output, chainActive.Height(), false), false);
            return outpoint;
        };

        constexpr uint64_t wrapPartA = 8173372036854775857ULL;
        constexpr uint64_t wrapPartB = 2100000000000000002ULL;
        static_assert(wrapPartA + wrapPartA + wrapPartB == 100ULL,
                      "overflow vector must equal 100 modulo 2^64");

        auto makeOverflowTransaction = [&]() {
            CMutableTransaction tx;
            tx.vin.emplace_back(addCoin(makeAssetOutput(100)));
            tx.vout.emplace_back(0, CScript() << OP_TRUE);
            tx.vout.emplace_back(makeAssetOutput(static_cast<CAmount>(wrapPartA)));
            tx.vout.emplace_back(makeAssetOutput(static_cast<CAmount>(wrapPartA)));
            tx.vout.emplace_back(makeAssetOutput(static_cast<CAmount>(wrapPartB)));
            return MakeTransactionRef(tx);
        };
        const CTransactionRef invalid = makeOverflowTransaction();

        std::vector<std::pair<std::string, uint256>> reissues;
        CValidationState preactivationState;
        const bool preactivationValid =
            Consensus::CheckTxAssets(*invalid, preactivationState, *pcoinsTip,
                                     passets, false, reissues, false);
        BOOST_REQUIRE_MESSAGE(preactivationValid, preactivationState.GetRejectReason());
        CValidationState activeState;
        BOOST_CHECK(!Consensus::CheckTxAssets(*invalid, activeState, *pcoinsTip,
                                              passets, false, reissues, true));
        BOOST_CHECK_EQUAL(activeState.GetRejectReason(),
                          "bad-txns-transfer-asset-amount-toolarge");

        CMutableTransaction childMutable;
        childMutable.vin.emplace_back(COutPoint(invalid->GetHash(), 0));
        childMutable.vout.emplace_back(0, CScript() << OP_TRUE);
        const CTransactionRef child = MakeTransactionRef(childMutable);

        CMutableTransaction validAssetMutable;
        validAssetMutable.vin.emplace_back(addCoin(makeAssetOutput(100)));
        validAssetMutable.vout.emplace_back(makeAssetOutput(100));
        const CTransactionRef validAsset = MakeTransactionRef(validAssetMutable);

        // The block that crosses the activation boundary is itself checked
        // under the previous (LOCKED_IN) rules. Simulate its already-flushed
        // output so an ACTIVE-valid child must survive after the parent is
        // removed with reason BLOCK.
        const CTransactionRef lastPreactivationBlockTx = makeOverflowTransaction();
        pcoinsTip->AddCoin(COutPoint(lastPreactivationBlockTx->GetHash(), 0),
                           Coin(lastPreactivationBlockTx->vout[0],
                                chainActive.Height() + 1, false), false);
        CMutableTransaction confirmedChildMutable;
        confirmedChildMutable.vin.emplace_back(
            COutPoint(lastPreactivationBlockTx->GetHash(), 0));
        confirmedChildMutable.vout.emplace_back(0, CScript() << OP_TRUE);
        const CTransactionRef confirmedChild = MakeTransactionRef(confirmedChildMutable);

        CMutableTransaction unrelatedMutable;
        unrelatedMutable.vin.emplace_back(
            addCoin(CTxOut(COIN, CScript() << OP_TRUE)));
        unrelatedMutable.vout.emplace_back(COIN, CScript() << OP_TRUE);
        const CTransactionRef unrelated = MakeTransactionRef(unrelatedMutable);

        TestMemPoolEntryHelper entry;
        BOOST_REQUIRE(mempool.addUnchecked(invalid->GetHash(), entry.FromTx(*invalid)));
        BOOST_REQUIRE(mempool.addUnchecked(child->GetHash(), entry.FromTx(*child)));
        BOOST_REQUIRE(mempool.addUnchecked(validAsset->GetHash(), entry.FromTx(*validAsset)));
        BOOST_REQUIRE(mempool.addUnchecked(lastPreactivationBlockTx->GetHash(),
                                           entry.FromTx(*lastPreactivationBlockTx)));
        BOOST_REQUIRE(mempool.addUnchecked(confirmedChild->GetHash(),
                                           entry.FromTx(*confirmedChild)));
        BOOST_REQUIRE(mempool.addUnchecked(unrelated->GetHash(), entry.FromTx(*unrelated)));
        BOOST_REQUIRE_EQUAL(mempool.size(), 6U);

        ConnectedBlockAssetData noAssetChanges;
        const std::vector<CTransactionRef> noBlockTransactions;
        mempool.removeForBlock(noBlockTransactions, chainActive.Height() + 1,
                               noAssetChanges, false, false);
        BOOST_REQUIRE_EQUAL(mempool.size(), 6U);

        const std::vector<CTransactionRef> activatingBlock{lastPreactivationBlockTx};
        mempool.removeForBlock(activatingBlock, chainActive.Height() + 1,
                               noAssetChanges, true, true);
        BOOST_CHECK(!mempool.exists(invalid->GetHash()));
        BOOST_CHECK(!mempool.exists(child->GetHash()));
        BOOST_CHECK(mempool.exists(validAsset->GetHash()));
        BOOST_CHECK(!mempool.exists(lastPreactivationBlockTx->GetHash()));
        BOOST_CHECK(mempool.exists(confirmedChild->GetHash()));
        BOOST_CHECK(mempool.exists(unrelated->GetHash()));
        BOOST_CHECK_EQUAL(mempool.size(), 3U);

        mempool.clear();
    }

BOOST_AUTO_TEST_SUITE_END()
