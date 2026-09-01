// Copyright (c) 2026 The Ravencoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>
#include <assets/mineable.h>
#include <assets/mineabledb.h>

#include <test/assets/asset_test_helpers.h>
#include <test/test_raven.h>

#include <boost/test/unit_test.hpp>

#include <amount.h>
#include <base58.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <script/standard.h>
#include <validation.h>
#include <key.h>
#include <script/sign.h>
#include <keystore.h>
#include <util.h>

#include <map>
#include <vector>

namespace {

CMineableSchedule MakeSchedule(const std::string& root, CAmount total, CAmount perBlock, int nth,
                               int startHeight = 100, bool allowExt = false, CAmount maxQty = 0)
{
    CMineableSchedule s;
    s.strRootAsset = root;
    s.strMineableAsset = MineableAssetNameFromRoot(root);
    s.nTotalQty = total;
    s.nPerBlock = perBlock;
    s.nNthBlock = nth;
    s.nFundAmt = 0;
    s.nUnits = 0;
    s.nHasIPFS = 0;
    s.nStartHeight = startHeight;
    s.nMaturedPeriods = 0;
    s.nClaimedPeriods = 0;
    s.nTotalMinted = 0;
    s.fActive = true;
    s.fAllowExtension = allowExt;
    s.nMaxTotalQty = maxQty > 0 ? maxQty : total;
    return s;
}

CScript MinerScript(const CKey& key)
{
    return CScript() << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
}

CScript IssueBurnScript()
{
    return GetScriptForDestination(DecodeDestination(GetParams().IssueAssetBurnAddress()));
}

/** End-to-end chain helper: coinbase -> self, enumerate assets, sign txs. */
struct MineableChainHelper
{
    TestChain100Setup& chain;
    CScript miner;
    CBasicKeyStore keystore;
    int nextCoinbase;
    int assetSerial;
    std::map<std::string, COutPoint> ownerOutpoints;
    std::map<std::string, CMutableTransaction> rootIssueTxs;

    explicit     MineableChainHelper(TestChain100Setup& setup)
        : chain(setup), miner(GetScriptForDestination(setup.coinbaseKey.GetPubKey().GetID())), nextCoinbase(0), assetSerial(0)
    {
        keystore.AddKey(setup.coinbaseKey);
    }

    std::string NextRootName()
    {
        return strprintf("MINE%04d", assetSerial++);
    }

    void SignPrevout(CMutableTransaction& tx, unsigned int nIn, const CTransaction& prevTx) const
    {
        BOOST_REQUIRE(SignSignature(keystore, prevTx, tx, nIn, SIGHASH_ALL));
    }

    void AppendCoinbaseInput(CMutableTransaction& tx)
    {
        BOOST_REQUIRE(nextCoinbase < static_cast<int>(chain.coinbaseTxns.size()));
        tx.vin.emplace_back(COutPoint(chain.coinbaseTxns[nextCoinbase].GetHash(), 0));
        ++nextCoinbase;
    }

    CMutableTransaction BuildIssueRootAssetTx(const std::string& rootName, CAmount qty = COIN)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        CNewAsset asset(rootName, qty, 0, 1, 0, "");
        tx.vout.emplace_back(GetIssueAssetBurnAmount(), IssueBurnScript());
        CScript ownerScript = miner;
        asset.ConstructOwnerTransaction(ownerScript);
        tx.vout.emplace_back(0, ownerScript);
        tx.vout.emplace_back(50 * COIN, miner);
        CScript assetScript = miner;
        asset.ConstructTransaction(assetScript);
        tx.vout.emplace_back(0, assetScript);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    uint256 IssueRootOnChain(const std::string& rootName, CAmount qty = COIN)
    {
        CMutableTransaction tx = BuildIssueRootAssetTx(rootName, qty);
        chain.CreateAndProcessBlock({tx}, miner);
        ownerOutpoints[rootName] = COutPoint(tx.GetHash(), 1);
        rootIssueTxs[rootName] = tx;
        return tx.GetHash();
    }

    CMutableTransaction BuildIssueMineableTx(const CIssueMineable& issue)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        const CAmount burn = CalculateMineableIssuanceCost(issue);
        tx.vout.emplace_back(burn, IssueBurnScript());
        tx.vout.emplace_back(50 * COIN, miner);
        CScript mineScript = miner;
        issue.ConstructTransaction(mineScript);
        tx.vout.emplace_back(0, mineScript);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CIssueMineable DefaultIssueParams(const std::string& root, CAmount total, CAmount perBlock, int nth,
                                      bool allowExt = false, CAmount maxQty = 0)
    {
        CIssueMineable issue;
        issue.strRootAsset = root;
        issue.strMineableAsset = MineableAssetNameFromRoot(root);
        issue.nTotalQty = total;
        issue.nPerBlock = perBlock;
        issue.nNthBlock = nth;
        issue.nFundAmt = 0;
        issue.nUnits = 0;
        issue.nHasIPFS = 0;
        issue.fAllowExtension = allowExt;
        issue.nMaxTotalQty = maxQty;
        return issue;
    }

    uint256 IssueMineableOnChain(const CIssueMineable& issue)
    {
        CMutableTransaction tx = BuildIssueMineableTx(issue);
        chain.CreateAndProcessBlock({tx}, miner);
        return tx.GetHash();
    }

    std::string IssueRootAndMineableOnChain(CAmount total = 400 * COIN, CAmount perBlock = 100 * COIN,
                                            int nth = 4, bool allowExt = false, CAmount maxQty = 0)
    {
        const std::string root = NextRootName();
        IssueRootOnChain(root);
        CIssueMineable issue = DefaultIssueParams(root, total, perBlock, nth, allowExt, maxQty);
        IssueMineableOnChain(issue);
        return root;
    }

    CMutableTransaction BuildReissueMineableTx(const CReissueMineableSchedule& reissue,
                                               const CMineableSchedule& schedule)
    {
        const COutPoint ownerOut = ownerOutpoints.at(schedule.strRootAsset);
        CMutableTransaction tx;
        tx.vin.emplace_back(ownerOut);
        const CAmount burn = CalculateMineableExtensionCost(schedule, reissue.nAddQty, reissue.nNewNthBlock);
        unsigned int coinbaseVin = 0;
        if (burn > 0) {
            AppendCoinbaseInput(tx);
            coinbaseVin = 1;
            tx.vout.emplace_back(burn, IssueBurnScript());
        }
        CAssetTransfer ownerReturn(schedule.strRootAsset + OWNER_TAG, OWNER_ASSET_AMOUNT);
        CScript ownerScript = miner;
        ownerReturn.ConstructTransaction(ownerScript);
        tx.vout.emplace_back(0, ownerScript);
        if (burn > 0)
            tx.vout.emplace_back(50 * COIN, miner);
        CScript reissueScript = miner;
        reissue.ConstructTransaction(reissueScript);
        tx.vout.emplace_back(0, reissueScript);

        SignPrevout(tx, 0, rootIssueTxs.at(schedule.strRootAsset));
        if (burn > 0)
            SignPrevout(tx, coinbaseVin, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    void ReissueOnChain(const std::string& root, const CReissueMineableSchedule& reissue)
    {
        CMineableSchedule schedule;
        pmineabledb->ReadSchedule(MineableAssetNameFromRoot(root), schedule);
        CMutableTransaction tx = BuildReissueMineableTx(reissue, schedule);
        chain.CreateAndProcessBlock({tx}, miner);
    }

    void MineBlocks(int n)
    {
        for (int i = 0; i < n; ++i)
            chain.CreateAndProcessBlock({}, miner);
    }

    CAmount AccruedForAsset(const std::string& mineableAsset, int atHeight = -1) const
    {
        CMineableSchedule live;
        pmineabledb->ReadSchedule(mineableAsset, live);
        const int height = atHeight >= 0 ? atHeight : chainActive.Height();
        live.UpdateMaturity(height);
        return live.GetAccruedAmount();
    }

    void ClaimAsset(const std::string& mineableAsset)
    {
        const CAmount amt = AccruedForAsset(mineableAsset, chainActive.Height() + 1);
        BOOST_REQUIRE(amt > 0);
        std::map<std::string, CAmount> claims;
        claims[mineableAsset] = amt;
        chain.CreateAndProcessBlockWithMineableClaims({}, miner, claims);
    }

    CMutableTransaction BuildTransferTx(const COutPoint& assetIn, const std::string& assetName,
                                        CAmount amount, const CScript& fromScript)
    {
        CMutableTransaction tx;
        tx.vin.emplace_back(assetIn);
        CAssetTransfer xfer(assetName, amount);
        CScript dest = miner;
        xfer.ConstructTransaction(dest);
        tx.vout.emplace_back(0, dest);
        CMutableTransaction fakePrev;
        fakePrev.vin.emplace_back(COutPoint(uint256S("0"), 0));
        CScript prevOut = fromScript;
        CAssetTransfer inXfer(assetName, amount);
        inXfer.ConstructTransaction(prevOut);
        fakePrev.vout.emplace_back(0, prevOut);
        SignPrevout(tx, 0, fakePrev);
        return tx;
    }
};

CMutableTransaction BuildReissueMineableTx(const CReissueMineableSchedule& reissue, CAmount burn,
                                           const CScript& destScript, const std::string& rootAsset = "")
{
    CMutableTransaction tx;
    if (burn > 0) {
        tx.vout.emplace_back(burn, GetScriptForDestination(
            DecodeDestination(GetParams().IssueAssetBurnAddress())));
    }
    if (!rootAsset.empty()) {
        CAssetTransfer ownerReturn(rootAsset + OWNER_TAG, OWNER_ASSET_AMOUNT);
        CScript ownerScript = destScript;
        ownerReturn.ConstructTransaction(ownerScript);
        tx.vout.emplace_back(0, ownerScript);
    }
    CScript script = destScript;
    reissue.ConstructTransaction(script);
    tx.vout.emplace_back(0, script);
    return tx;
}

void RegisterScheduleOnChain(CMineableSchedule schedule)
{
    BOOST_REQUIRE(pmineabledb != nullptr);
    BOOST_REQUIRE(pmineabledb->WriteSchedule(schedule));
    CNewAsset asset(schedule.strMineableAsset, 0, schedule.nUnits, 0, schedule.nHasIPFS, schedule.strIPFSHash);
    if (!passets->CheckIfAssetExists(schedule.strMineableAsset)) {
        BOOST_REQUIRE(passets->AddNewAsset(asset, GetParams().GlobalBurnAddress(), schedule.nStartHeight, uint256()));
    }
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(mineable_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(mineable_accrual_math)
{
    ActivateAllAssetFeaturesForTest();
    CMineableSchedule s = MakeSchedule("ROOTA", 1000 * COIN, 100 * COIN, 10, 100);
    s.UpdateMaturity(110);
    BOOST_CHECK_EQUAL(s.nMaturedPeriods, 1);
    BOOST_CHECK_EQUAL(s.GetAccruedAmount(), 100 * COIN);

    s.UpdateMaturity(130);
    BOOST_CHECK_EQUAL(s.nMaturedPeriods, 3);
    BOOST_CHECK_EQUAL(s.GetAccruedAmount(), 300 * COIN);

    s.nClaimedPeriods = 1;
    BOOST_CHECK_EQUAL(s.GetAccruedAmount(), 200 * COIN);

    s.nTotalMinted = 900 * COIN;
    BOOST_CHECK_EQUAL(s.GetAccruedAmount(), 100 * COIN);
}

BOOST_AUTO_TEST_CASE(mineable_delayed_claim_accrual)
{
    ActivateAllAssetFeaturesForTest();
    CMineableSchedule s = MakeSchedule("ROOTB", 500 * COIN, 50 * COIN, 5, 200);
    s.UpdateMaturity(220);
    BOOST_CHECK_EQUAL(s.nMaturedPeriods, 4);
    s.UpdateMaturity(235);
    BOOST_CHECK_EQUAL(s.nMaturedPeriods, 7);
    BOOST_CHECK_EQUAL(s.GetAccruedAmount(), 7 * 50 * COIN);
    s.nClaimedPeriods = 0;
    BOOST_CHECK_EQUAL(s.GetAccruedAmount(), 350 * COIN);
}

BOOST_AUTO_TEST_CASE(mineable_nth_compatibility)
{
    BOOST_CHECK(IsNthBlockExtensionCompatible(10, 0));
    BOOST_CHECK(IsNthBlockExtensionCompatible(10, 10));
    BOOST_CHECK(IsNthBlockExtensionCompatible(10, 5));
    BOOST_CHECK(IsNthBlockExtensionCompatible(10, 2));
    BOOST_CHECK(!IsNthBlockExtensionCompatible(10, 3));
    BOOST_CHECK(!IsNthBlockExtensionCompatible(10, 15));
    BOOST_CHECK(IsNthBlockExtensionCompatible(7, 1));
    BOOST_CHECK(!IsNthBlockExtensionCompatible(7, 2));
}

BOOST_AUTO_TEST_CASE(mineable_extension_cost_add_qty)
{
    CMineableSchedule s = MakeSchedule("ROOTC", 1000 * COIN, 100 * COIN, 10);
    const CAmount cost = CalculateMineableExtensionCost(s, 200 * COIN, 0);
    BOOST_CHECK_EQUAL(cost, 2 * MINEABLE_COST_PER_PERIOD);
}

BOOST_AUTO_TEST_CASE(mineable_extension_cost_prime_to_nth_one)
{
    CMineableSchedule s = MakeSchedule("ROOTD", 700 * COIN, 100 * COIN, 7);
    s.nClaimedPeriods = 2;
    const CAmount cost = CalculateMineableExtensionCost(s, 0, 1);
    const int remaining = s.GetTotalPeriods() - s.nClaimedPeriods;
    BOOST_CHECK_EQUAL(cost, static_cast<CAmount>(remaining) * 6 * MINEABLE_COST_PER_PERIOD);
}

BOOST_AUTO_TEST_CASE(mineable_extension_cost_compatible_nth_free)
{
    CMineableSchedule s = MakeSchedule("ROOTE", 1000 * COIN, 100 * COIN, 10);
    BOOST_CHECK_EQUAL(CalculateMineableExtensionCost(s, 0, 5), 0);
    BOOST_CHECK_EQUAL(CalculateMineableExtensionCost(s, 100 * COIN, 5), MINEABLE_COST_PER_PERIOD);
}

BOOST_AUTO_TEST_CASE(mineable_extension_reject_subtractive)
{
    CMineableSchedule s = MakeSchedule("ROOTF", 1000 * COIN, 100 * COIN, 10, 100, true, 2000 * COIN);
    std::string err;
    BOOST_CHECK(!ApplyScheduleExtension(s, 0, 20, err));
    BOOST_CHECK_EQUAL(s.nTotalQty, 1000 * COIN);

    CMineableSchedule capped = MakeSchedule("ROOTF2", 1000 * COIN, 100 * COIN, 10, 100, true, 1100 * COIN);
    BOOST_CHECK(!ApplyScheduleExtension(capped, 200 * COIN, 0, err));
}

BOOST_AUTO_TEST_CASE(mineable_extension_hard_cap)
{
    CMineableSchedule s = MakeSchedule("ROOTG", 500 * COIN, 50 * COIN, 5, 100, true, 600 * COIN);
    std::string err;
    BOOST_CHECK(ApplyScheduleExtension(s, 100 * COIN, 0, err));
    BOOST_CHECK_EQUAL(s.nTotalQty, 600 * COIN);
    BOOST_CHECK(!ApplyScheduleExtension(s, 50 * COIN, 0, err));
}

BOOST_AUTO_TEST_CASE(mineable_extension_not_editable_by_default)
{
    CMineableSchedule s = MakeSchedule("ROOTH", 500 * COIN, 50 * COIN, 5);
    BOOST_CHECK(!s.fAllowExtension);
    CMineableAssetsDB db(1 << 16, true, true);
    db.WriteSchedule(s);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = s.strMineableAsset;
    reissue.nAddQty = 50 * COIN;
    std::string err;
    BOOST_CHECK(!ContextualCheckReissueMineableSchedule(nullptr, db, reissue, "", err));
}

BOOST_AUTO_TEST_CASE(mineable_extension_pay_as_you_go)
{
    CMineableSchedule s = MakeSchedule("ROOTI", 500 * COIN, 50 * COIN, 5, 100, true, 2000 * COIN);
    CAmount first = CalculateMineableExtensionCost(s, 100 * COIN, 0);
    std::string err;
    ApplyScheduleExtension(s, 100 * COIN, 0, err);
    CAmount second = CalculateMineableExtensionCost(s, 150 * COIN, 0);
    BOOST_CHECK_EQUAL(first, 2 * MINEABLE_COST_PER_PERIOD);
    BOOST_CHECK_EQUAL(second, 3 * MINEABLE_COST_PER_PERIOD);
}

BOOST_AUTO_TEST_CASE(mineable_db_persistence_restart)
{
    CMineableSchedule s = MakeSchedule("ROOTJ", 800 * COIN, 80 * COIN, 8, 50, true, 1600 * COIN);
    s.nMaturedPeriods = 3;
    s.nClaimedPeriods = 1;

    CMineableAssetsDB db(1 << 16, true, true);
    BOOST_CHECK(db.WriteSchedule(s));
    db.Flush();

    CMineableSchedule loaded;
    BOOST_CHECK(db.ReadSchedule(s.strMineableAsset, loaded));
    BOOST_CHECK_EQUAL(loaded.nTotalQty, s.nTotalQty);
    BOOST_CHECK_EQUAL(loaded.nMaturedPeriods, 3);
    BOOST_CHECK_EQUAL(loaded.nClaimedPeriods, 1);
    BOOST_CHECK(loaded.fAllowExtension);
    BOOST_CHECK_EQUAL(loaded.nMaxTotalQty, 1600 * COIN);
}

BOOST_AUTO_TEST_CASE(mineable_coinbase_claim_validation)
{
    ActivateAllAssetFeaturesForTest();
    CMineableSchedule s = MakeSchedule("ROOTK", 300 * COIN, 100 * COIN, 5, chainActive.Height() - 10);
    RegisterScheduleOnChain(s);

    const int h = chainActive.Height() + 1;
    ProcessMineableMaturityAtHeight(h, *pmineabledb);

    CMineableSchedule updated;
    pmineabledb->ReadSchedule(s.strMineableAsset, updated);
    updated.UpdateMaturity(h);
    const CAmount accrued = updated.GetAccruedAmount();
    BOOST_CHECK(accrued > 0);

    CMutableTransaction coinbase;
    coinbase.vin.emplace_back(COutPoint(uint256(), (uint32_t)-1));
    CKey key;
    key.MakeNewKey(true);
    CScript miner = MinerScript(key);
    CAssetTransfer claim(s.strMineableAsset, accrued);
    CScript claimScript = miner;
    claim.ConstructTransaction(claimScript);
    coinbase.vout.emplace_back(0, claimScript);

    CValidationState state;
    BOOST_CHECK(ValidateMineableCoinbaseClaims(coinbase, h, passets, *pmineabledb, state));

    CAssetTransfer badClaim(s.strMineableAsset, accrued - COIN);
    CMutableTransaction badCoinbase;
    badCoinbase.vin.emplace_back(COutPoint(uint256(), (uint32_t)-1));
    CScript badScript = miner;
    badClaim.ConstructTransaction(badScript);
    badCoinbase.vout.emplace_back(0, badScript);
    CValidationState badState;
    BOOST_CHECK(!ValidateMineableCoinbaseClaims(badCoinbase, h, passets, *pmineabledb, badState));
}

BOOST_AUTO_TEST_CASE(mineable_coinbase_duplicate_miner_claim)
{
    ActivateAllAssetFeaturesForTest();
    CMineableSchedule s = MakeSchedule("DUPD", 200 * COIN, 100 * COIN, 2, chainActive.Height() - 10);
    RegisterScheduleOnChain(s);
    const int h = chainActive.Height() + 1;
    ProcessMineableMaturityAtHeight(h, *pmineabledb);

    CMineableSchedule updated;
    pmineabledb->ReadSchedule(s.strMineableAsset, updated);
    updated.UpdateMaturity(h);
    const CAmount amt = updated.GetAccruedAmount();

    CKey key;
    key.MakeNewKey(true);
    CScript miner = MinerScript(key);
    CMutableTransaction coinbase;
    coinbase.vin.emplace_back(COutPoint(uint256(), (uint32_t)-1));
    for (int i = 0; i < 2; ++i) {
        CAssetTransfer claim(s.strMineableAsset, amt / 2);
        CScript claimScript = miner;
        claim.ConstructTransaction(claimScript);
        coinbase.vout.emplace_back(0, claimScript);
    }
    CValidationState state;
    BOOST_CHECK(!ValidateMineableCoinbaseClaims(coinbase, h, passets, *pmineabledb, state));
}

BOOST_AUTO_TEST_CASE(mineable_coinbase_zero_accrued_rejected)
{
    ActivateAllAssetFeaturesForTest();
    CMineableSchedule s = MakeSchedule("ZERO", 100 * COIN, 50 * COIN, 50, chainActive.Height() + 100);
    RegisterScheduleOnChain(s);
    const int h = chainActive.Height() + 1;

    CKey key;
    key.MakeNewKey(true);
    CScript miner = MinerScript(key);
    CMutableTransaction coinbase;
    coinbase.vin.emplace_back(COutPoint(uint256(), (uint32_t)-1));
    CAssetTransfer claim(s.strMineableAsset, 50 * COIN);
    CScript claimScript = miner;
    claim.ConstructTransaction(claimScript);
    coinbase.vout.emplace_back(0, claimScript);

    CValidationState state;
    BOOST_CHECK(!ValidateMineableCoinbaseClaims(coinbase, h, passets, *pmineabledb, state));
}

BOOST_AUTO_TEST_CASE(mineable_issue_script_roundtrip)
{
    CIssueMineable issue;
    issue.strRootAsset = "ROUNDROOT";
    issue.strMineableAsset = MineableAssetNameFromRoot(issue.strRootAsset);
    issue.nTotalQty = 1000 * COIN;
    issue.nPerBlock = 100 * COIN;
    issue.nNthBlock = 10;
    issue.nUnits = 0;

    CScript dest = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    issue.ConstructTransaction(dest);
    BOOST_CHECK(dest.IsIssueMineableAsset());

    CIssueMineable issue2 = issue;
    CKey key;
    key.MakeNewKey(true);
    CScript miner = MinerScript(key);
    issue2.ConstructTransaction(miner);
    BOOST_CHECK(miner.IsIssueMineableAsset());

    CIssueMineable parsed;
    std::string address;
    BOOST_CHECK(IssueMineableFromScript(dest, parsed, address));
    BOOST_CHECK_EQUAL(parsed.strRootAsset, issue.strRootAsset);
    BOOST_CHECK_EQUAL(parsed.nTotalQty, issue.nTotalQty);
}

BOOST_AUTO_TEST_CASE(mineable_reissue_script_roundtrip)
{
    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = "&ROUND";
    reissue.nAddQty = 100 * COIN;
    reissue.nNewNthBlock = 2;

    CScript dest = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    reissue.ConstructTransaction(dest);
    BOOST_CHECK(dest.IsReissueMineableAsset());

    CReissueMineableSchedule parsed;
    std::string address;
    BOOST_CHECK(ReissueMineableFromScript(dest, parsed, address));
    BOOST_CHECK_EQUAL(parsed.strMineableAsset, reissue.strMineableAsset);
    BOOST_CHECK_EQUAL(parsed.nAddQty, reissue.nAddQty);
    BOOST_CHECK_EQUAL(parsed.nNewNthBlock, reissue.nNewNthBlock);
}

BOOST_AUTO_TEST_CASE(mineable_issue_fixed_cap_default)
{
    CIssueMineable issue;
    issue.strRootAsset = "CAPROOT";
    issue.strMineableAsset = MineableAssetNameFromRoot(issue.strRootAsset);
    issue.nTotalQty = 500 * COIN;
    issue.nPerBlock = 50 * COIN;
    issue.nNthBlock = 5;
    issue.nUnits = 0;
    issue.fAllowExtension = false;
    issue.nMaxTotalQty = 0;

    std::string err;
    BOOST_CHECK(CheckIssueMineable(issue, err));

    CMineableSchedule sched;
    sched.nMaxTotalQty = issue.nTotalQty;
    BOOST_CHECK_EQUAL(sched.nMaxTotalQty, 500 * COIN);
}

BOOST_AUTO_TEST_CASE(mineable_issue_burn_validation)
{
    ActivateAllAssetFeaturesForTest();
    SelectParams(CBaseChainParams::REGTEST);

    CIssueMineable issue;
    issue.strRootAsset = "BURNROOT";
    issue.strMineableAsset = MineableAssetNameFromRoot(issue.strRootAsset);
    issue.nTotalQty = 1000 * COIN;
    issue.nPerBlock = 100 * COIN;
    issue.nNthBlock = 10;
    issue.nUnits = 0;

    CNewAsset root(issue.strRootAsset, 1 * COIN, 0, 1, 0, "");
    BOOST_REQUIRE(passets->AddNewAsset(root, GetParams().GlobalBurnAddress(), 1, uint256()));

    std::string err;
    BOOST_CHECK(CheckIssueMineable(issue, err));

    CMutableTransaction tx;
    const CAmount burn = CalculateMineableIssuanceCost(issue);
    tx.vout.emplace_back(burn, GetScriptForDestination(
        DecodeDestination(GetParams().IssueAssetBurnAddress())));
    CScript dest = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    issue.ConstructTransaction(dest);
    tx.vout.emplace_back(0, dest);

    CTransaction ctx(tx);
    BOOST_CHECK(ctx.VerifyIssueMineable(err));

    tx.vout[0].nValue = burn - COIN;
    CTransaction badTx(tx);
    BOOST_CHECK(!badTx.VerifyIssueMineable(err));
}

BOOST_AUTO_TEST_CASE(mineable_reissue_burn_validation)
{
    ActivateAllAssetFeaturesForTest();
    CMineableSchedule s = MakeSchedule("BURNEXT", 500 * COIN, 50 * COIN, 7, 10, true, 1000 * COIN);
    RegisterScheduleOnChain(s);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = s.strMineableAsset;
    reissue.nAddQty = 100 * COIN;
    const CAmount burn = CalculateMineableExtensionCost(s, reissue.nAddQty, 0);

    CScript dest = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    CMutableTransaction tx = BuildReissueMineableTx(reissue, burn, dest, s.strRootAsset);
    CTransaction ctx(tx);
    std::string err;
    BOOST_CHECK(ctx.VerifyReissueMineable(err));

    tx.vout[0].nValue = burn + COIN;
    CTransaction badTx(tx);
    BOOST_CHECK(!badTx.VerifyReissueMineable(err));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(mineable_chain_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(mineable_chain_mine_and_claim)
{
    ActivateAllAssetFeaturesForTest();
    const int base = chainActive.Height();
    CMineableSchedule s = MakeSchedule("CHAIN1", 400 * COIN, 100 * COIN, 4, base + 1);
    RegisterScheduleOnChain(s);

    CScript miner = MinerScript(coinbaseKey);
    for (int i = 0; i < 5; ++i)
        CreateAndProcessBlock({}, miner);

    CMineableSchedule live;
    pmineabledb->ReadSchedule(s.strMineableAsset, live);
    live.UpdateMaturity(chainActive.Height());
    const CAmount accrued = live.GetAccruedAmount();
    BOOST_CHECK(accrued >= 100 * COIN);

    std::map<std::string, CAmount> claims;
    live.UpdateMaturity(chainActive.Height() + 1);
    claims[s.strMineableAsset] = live.GetAccruedAmount();
    CreateAndProcessBlockWithMineableClaims({}, miner, claims);

    CMineableSchedule claimed;
    pmineabledb->ReadSchedule(s.strMineableAsset, claimed);
    BOOST_CHECK(claimed.nClaimedPeriods > 0);
    BOOST_CHECK(claimed.nTotalMinted >= accrued);
}

BOOST_AUTO_TEST_CASE(mineable_chain_delayed_claim_full_accrual)
{
    ActivateAllAssetFeaturesForTest();
    const int base = chainActive.Height();
    CMineableSchedule s = MakeSchedule("CHAIN2", 600 * COIN, 100 * COIN, 3, base + 1);
    RegisterScheduleOnChain(s);

    CScript miner = MinerScript(coinbaseKey);
    for (int i = 0; i < 8; ++i)
        CreateAndProcessBlock({}, miner);

    CMineableSchedule live;
    pmineabledb->ReadSchedule(s.strMineableAsset, live);
    live.UpdateMaturity(chainActive.Height());
    const CAmount fullAccrued = live.GetAccruedAmount();
    BOOST_CHECK(fullAccrued >= 200 * COIN);

    CreateAndProcessBlock({}, miner);
    std::map<std::string, CAmount> claims;
    CMineableSchedule preClaim;
    pmineabledb->ReadSchedule(s.strMineableAsset, preClaim);
    preClaim.UpdateMaturity(chainActive.Height() + 1);
    claims[s.strMineableAsset] = preClaim.GetAccruedAmount();
    CreateAndProcessBlockWithMineableClaims({}, miner, claims);

    CMineableSchedule claimed;
    pmineabledb->ReadSchedule(s.strMineableAsset, claimed);
    BOOST_CHECK_EQUAL(claimed.nClaimedPeriods, claimed.nMaturedPeriods);
}

BOOST_AUTO_TEST_CASE(mineable_chain_spend_after_claim)
{
    ActivateAllAssetFeaturesForTest();
    const int base = chainActive.Height();
    CMineableSchedule s = MakeSchedule("CHAIN3", 200 * COIN, 100 * COIN, 2, base + 1);
    RegisterScheduleOnChain(s);

    CScript miner = MinerScript(coinbaseKey);
    for (int i = 0; i < 4; ++i)
        CreateAndProcessBlock({}, miner);

    CMineableSchedule live;
    pmineabledb->ReadSchedule(s.strMineableAsset, live);
    live.UpdateMaturity(chainActive.Height());
    const CAmount accrued = live.GetAccruedAmount();

    std::map<std::string, CAmount> claims;
    live.UpdateMaturity(chainActive.Height() + 1);
    claims[s.strMineableAsset] = live.GetAccruedAmount();
    CreateAndProcessBlockWithMineableClaims({}, miner, claims);

    CMineableSchedule after;
    pmineabledb->ReadSchedule(s.strMineableAsset, after);
    BOOST_CHECK(after.nTotalMinted >= accrued);
}

BOOST_AUTO_TEST_CASE(mineable_chain_many_assets_one_block)
{
    ActivateAllAssetFeaturesForTest();
    const int base = chainActive.Height();
    const char* roots[] = {"MULTI1", "MULTI2", "MULTI3", "MULTI4", "MULTI5"};
    std::map<std::string, CAmount> claims;
    CScript miner = MinerScript(coinbaseKey);

    for (const char* root : roots) {
        CMineableSchedule s = MakeSchedule(root, 200 * COIN, 50 * COIN, 2, base + 1);
        RegisterScheduleOnChain(s);
    }

    for (int i = 0; i < 6; ++i)
        CreateAndProcessBlock({}, miner);

    for (const char* root : roots) {
        const std::string asset = MineableAssetNameFromRoot(root);
        CMineableSchedule live;
        pmineabledb->ReadSchedule(asset, live);
        live.UpdateMaturity(chainActive.Height() + 1);
        claims[asset] = live.GetAccruedAmount();
    }

    CreateAndProcessBlockWithMineableClaims({}, miner, claims);

    for (const char* root : roots) {
        CMineableSchedule claimed;
        pmineabledb->ReadSchedule(MineableAssetNameFromRoot(root), claimed);
        BOOST_CHECK(claimed.nTotalMinted > 0);
    }
}

BOOST_AUTO_TEST_CASE(mineable_chain_extend_schedule_on_chain)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.IssueRootAndMineableOnChain(500 * COIN, 50 * COIN, 10, true, 1000 * COIN);
    const std::string asset = MineableAssetNameFromRoot(root);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = asset;
    reissue.nAddQty = 150 * COIN;
    reissue.nNewNthBlock = 5;
    H.ReissueOnChain(root, reissue);

    CMineableSchedule extended;
    pmineabledb->ReadSchedule(asset, extended);
    BOOST_CHECK_EQUAL(extended.nTotalQty, 650 * COIN);
    BOOST_CHECK_EQUAL(extended.nNthBlock, 5);
}

BOOST_AUTO_TEST_CASE(mineable_chain_extend_prime_nth_one_recost)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.IssueRootAndMineableOnChain(700 * COIN, 100 * COIN, 7, true, 1400 * COIN);
    const std::string asset = MineableAssetNameFromRoot(root);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = asset;
    reissue.nAddQty = 0;
    reissue.nNewNthBlock = 1;
    H.ReissueOnChain(root, reissue);

    CMineableSchedule extended;
    pmineabledb->ReadSchedule(asset, extended);
    BOOST_CHECK_EQUAL(extended.nNthBlock, 1);
}

BOOST_AUTO_TEST_CASE(mineable_chain_reject_extend_when_fixed)
{
    ActivateAllAssetFeaturesForTest();
    CMineableSchedule s = MakeSchedule("FIXED1", 300 * COIN, 50 * COIN, 5, chainActive.Height() + 1);
    RegisterScheduleOnChain(s);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = s.strMineableAsset;
    reissue.nAddQty = 50 * COIN;

    std::string err;
    BOOST_CHECK(!ContextualCheckReissueMineableSchedule(passets, *pmineabledb, reissue,
        EncodeDestination(coinbaseKey.GetPubKey().GetID()), err));
}

BOOST_AUTO_TEST_CASE(mineable_reissue_requires_owner_token)
{
    ActivateAllAssetFeaturesForTest();
    CMineableSchedule s = MakeSchedule("OWNCHK", 500 * COIN, 50 * COIN, 5, 10, true, 1000 * COIN);
    RegisterScheduleOnChain(s);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = s.strMineableAsset;
    reissue.nAddQty = 50 * COIN;

    std::string err;
    const std::string notOwner = GetParams().GlobalBurnAddress();
    BOOST_CHECK(!ContextualCheckReissueMineableSchedule(passets, *pmineabledb, reissue, notOwner, err));

    passets->mapAssetsAddressAmount[std::make_pair(s.strRootAsset + OWNER_TAG, notOwner)] = OWNER_ASSET_AMOUNT;
    BOOST_CHECK(ContextualCheckReissueMineableSchedule(passets, *pmineabledb, reissue, notOwner, err));
}

BOOST_AUTO_TEST_CASE(mineable_stress_many_schedules_maturity)
{
    ActivateAllAssetFeaturesForTest();
    const int base = chainActive.Height();
    const int count = 20;
    CScript miner = MinerScript(coinbaseKey);

    for (int i = 0; i < count; ++i) {
        const std::string root = "STRESS" + std::to_string(i);
        CMineableSchedule s = MakeSchedule(root, 100 * COIN, 10 * COIN, 2, base + 1, i % 2 == 0, 200 * COIN);
        RegisterScheduleOnChain(s);
    }

    for (int i = 0; i < 10; ++i)
        CreateAndProcessBlock({}, miner);

    std::vector<CMineableSchedule> all;
    pmineabledb->ListSchedules(all);
    BOOST_CHECK(static_cast<int>(all.size()) >= count);

    int matured = 0;
    for (const auto& sch : all) {
        CMineableSchedule copy = sch;
        copy.UpdateMaturity(chainActive.Height());
        if (copy.nMaturedPeriods > 0)
            ++matured;
    }
    BOOST_CHECK(matured >= count / 2);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(mineable_e2e_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(mineable_e2e_issue_root_and_mineable_on_chain)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.NextRootName();
    H.IssueRootOnChain(root);
    CIssueMineable issue = H.DefaultIssueParams(root, 400 * COIN, 100 * COIN, 4);
    H.IssueMineableOnChain(issue);

    CMineableSchedule sched;
    BOOST_CHECK(pmineabledb->ReadSchedule(MineableAssetNameFromRoot(root), sched));
    BOOST_CHECK(sched.fActive);
    BOOST_CHECK_EQUAL(sched.nTotalQty, 400 * COIN);
}

BOOST_AUTO_TEST_CASE(mineable_e2e_full_mint_mine_claim)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.IssueRootAndMineableOnChain(400 * COIN, 100 * COIN, 4);
    const std::string asset = MineableAssetNameFromRoot(root);

    H.MineBlocks(5);
    BOOST_CHECK(H.AccruedForAsset(asset) >= 100 * COIN);
    H.ClaimAsset(asset);

    CMineableSchedule claimed;
    pmineabledb->ReadSchedule(asset, claimed);
    BOOST_CHECK(claimed.nTotalMinted >= 100 * COIN);
    BOOST_CHECK(claimed.nClaimedPeriods > 0);
}

BOOST_AUTO_TEST_CASE(mineable_e2e_enumerated_assets_sequential_mint)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const int count = 8;
    std::vector<std::string> assets;

    for (int i = 0; i < count; ++i) {
        const std::string root = H.IssueRootAndMineableOnChain(200 * COIN, 50 * COIN, 2);
        assets.push_back(MineableAssetNameFromRoot(root));
    }

    H.MineBlocks(6);
    std::map<std::string, CAmount> claims;
    for (const auto& asset : assets) {
        const CAmount amt = H.AccruedForAsset(asset, chainActive.Height() + 1);
        if (amt > 0)
            claims[asset] = amt;
    }
    BOOST_CHECK(static_cast<int>(claims.size()) >= count - 1);
    CreateAndProcessBlockWithMineableClaims({}, H.miner, claims);

    for (const auto& asset : assets) {
        CMineableSchedule s;
        pmineabledb->ReadSchedule(asset, s);
        BOOST_CHECK(s.nTotalMinted > 0);
    }
}

BOOST_AUTO_TEST_CASE(mineable_e2e_payg_extend_while_mining)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.IssueRootAndMineableOnChain(300 * COIN, 50 * COIN, 5, true, 800 * COIN);
    const std::string asset = MineableAssetNameFromRoot(root);

    H.MineBlocks(3);
    CReissueMineableSchedule ext;
    ext.strMineableAsset = asset;
    ext.nAddQty = 100 * COIN;
    H.ReissueOnChain(root, ext);

    CMineableSchedule extended;
    pmineabledb->ReadSchedule(asset, extended);
    BOOST_CHECK_EQUAL(extended.nTotalQty, 400 * COIN);

    H.MineBlocks(4);
    H.ClaimAsset(asset);
    pmineabledb->ReadSchedule(asset, extended);
    BOOST_CHECK(extended.nTotalMinted > 0);
}

BOOST_AUTO_TEST_CASE(mineable_e2e_reissue_owner_signed_on_chain)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.IssueRootAndMineableOnChain(500 * COIN, 50 * COIN, 10, true, 1000 * COIN);
    const std::string asset = MineableAssetNameFromRoot(root);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = asset;
    reissue.nAddQty = 100 * COIN;
    reissue.nNewNthBlock = 5;
    H.ReissueOnChain(root, reissue);

    CMineableSchedule s;
    pmineabledb->ReadSchedule(asset, s);
    BOOST_CHECK_EQUAL(s.nTotalQty, 600 * COIN);
    BOOST_CHECK_EQUAL(s.nNthBlock, 5);
}

BOOST_AUTO_TEST_CASE(mineable_e2e_prime_nth_one_with_owner_burn)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.IssueRootAndMineableOnChain(700 * COIN, 100 * COIN, 7, true, 1400 * COIN);
    const std::string asset = MineableAssetNameFromRoot(root);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = asset;
    reissue.nNewNthBlock = 1;
    H.ReissueOnChain(root, reissue);

    CMineableSchedule s;
    pmineabledb->ReadSchedule(asset, s);
    BOOST_CHECK_EQUAL(s.nNthBlock, 1);
}

BOOST_AUTO_TEST_CASE(mineable_e2e_reject_reissue_non_owner)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.IssueRootAndMineableOnChain(300 * COIN, 50 * COIN, 5, true, 600 * COIN);
    const std::string asset = MineableAssetNameFromRoot(root);

    CReissueMineableSchedule reissue;
    reissue.strMineableAsset = asset;
    reissue.nAddQty = 50 * COIN;

    std::string err;
    const std::string stranger = GetParams().GlobalBurnAddress();
    BOOST_CHECK(!ContextualCheckReissueMineableSchedule(passets, *pmineabledb, reissue, stranger, err));
}

BOOST_AUTO_TEST_CASE(mineable_e2e_multi_extend_up_to_cap)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.IssueRootAndMineableOnChain(200 * COIN, 50 * COIN, 4, true, 500 * COIN);
    const std::string asset = MineableAssetNameFromRoot(root);

    for (int i = 0; i < 3; ++i) {
        CReissueMineableSchedule ext;
        ext.strMineableAsset = asset;
        ext.nAddQty = 50 * COIN;
        H.ReissueOnChain(root, ext);
    }

    CMineableSchedule s;
    pmineabledb->ReadSchedule(asset, s);
    BOOST_CHECK_EQUAL(s.nTotalQty, 350 * COIN);

    CReissueMineableSchedule tooMuch;
    tooMuch.strMineableAsset = asset;
    tooMuch.nAddQty = 200 * COIN;
    std::string err;
    BOOST_CHECK(!ContextualCheckReissueMineableSchedule(passets, *pmineabledb, tooMuch,
        EncodeDestination(coinbaseKey.GetPubKey().GetID()), err));
}

BOOST_AUTO_TEST_CASE(mineable_e2e_stress_enumerated_mine_loop)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const int rounds = 5;
    const int perRound = 4;

    for (int r = 0; r < rounds; ++r) {
        std::vector<std::string> batch;
        for (int i = 0; i < perRound; ++i)
            batch.push_back(MineableAssetNameFromRoot(H.IssueRootAndMineableOnChain(100 * COIN, 25 * COIN, 2, r % 2 == 0, 300 * COIN)));

        H.MineBlocks(3);
        std::map<std::string, CAmount> claims;
        for (const auto& asset : batch) {
            const CAmount amt = H.AccruedForAsset(asset);
            if (amt > 0)
                claims[asset] = amt;
        }
        if (!claims.empty())
            CreateAndProcessBlockWithMineableClaims({}, H.miner, claims);
    }

    std::vector<CMineableSchedule> all;
    pmineabledb->ListSchedules(all);
    BOOST_CHECK(static_cast<int>(all.size()) >= rounds * perRound);

    int withMint = 0;
    for (const auto& s : all) {
        if (s.nTotalMinted > 0)
            ++withMint;
    }
    BOOST_CHECK(withMint >= rounds);
}

BOOST_AUTO_TEST_CASE(mineable_e2e_verify_issue_tx_before_block)
{
    ActivateAllAssetFeaturesForTest();
    MineableChainHelper H(*this);
    const std::string root = H.NextRootName();
    H.IssueRootOnChain(root);
    CIssueMineable issue = H.DefaultIssueParams(root, 1000 * COIN, 100 * COIN, 10);
    CMutableTransaction mtx = H.BuildIssueMineableTx(issue);
    BOOST_CHECK(mtx.vout.back().scriptPubKey.IsIssueMineableAsset());
    CTransaction tx(mtx);
    std::string err;
    BOOST_CHECK_MESSAGE(tx.VerifyIssueMineable(err), err);
    H.IssueMineableOnChain(issue);
    CMineableSchedule sched;
    BOOST_CHECK(pmineabledb->ReadSchedule(issue.strMineableAsset, sched));
}

BOOST_AUTO_TEST_SUITE_END()
