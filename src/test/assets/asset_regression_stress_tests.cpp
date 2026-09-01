// Copyright (c) 2026 The Ravencoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Full asset regression and stress tests.
 * Covers every major asset transaction type and restricted operations.
 */

#include <assets/assets.h>
#include <assets/mineable.h>
#include <assets/mineabledb.h>

#include <test/assets/asset_test_helpers.h>
#include <test/test_raven.h>

#include <boost/test/unit_test.hpp>

#include <base58.h>
#include <chainparams.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <script/standard.h>
#include <validation.h>

namespace {

CScript IssueMineableBurnScript()
{
    return GetScriptForDestination(DecodeDestination(GetParams().IssueAssetBurnAddress()));
}

CMutableTransaction MakeValidIssueRootTx(const CScript& dest)
{
    CMutableTransaction tx;
    CNewAsset asset("REGROOT", COIN, 0, 1, 0, "");
    tx.vout.emplace_back(GetBurnAmount(AssetType::ROOT), AssetBurnScript(AssetType::ROOT));
    CScript ownerScript = dest;
    asset.ConstructOwnerTransaction(ownerScript);
    tx.vout.emplace_back(0, ownerScript);
    CScript assetScript = dest;
    asset.ConstructTransaction(assetScript);
    tx.vout.emplace_back(0, assetScript);
    return tx;
}

CMutableTransaction MakeValidReissueTx(const std::string& root, const CScript& dest)
{
    CMutableTransaction tx;
    tx.vout.emplace_back(GetReissueAssetBurnAmount(), GetScriptForDestination(
        DecodeDestination(GetParams().ReissueAssetBurnAddress())));
    CReissueAsset reissue(root, COIN, 0, 0, "");
    CScript reissueScript = dest;
    reissue.ConstructTransaction(reissueScript);
    tx.vout.emplace_back(0, reissueScript);
    CAssetTransfer ownerReturn(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
    CScript ownerScript = dest;
    ownerReturn.ConstructTransaction(ownerScript);
    tx.vout.emplace_back(0, ownerScript);
    return tx;
}

CMutableTransaction MakeValidRestrictedIssueTx(const std::string& root, const CScript& dest)
{
    CMutableTransaction tx;
    tx.vout.emplace_back(GetBurnAmount(AssetType::RESTRICTED), AssetBurnScript(AssetType::RESTRICTED));
    CAssetTransfer rootOwner(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
    CScript rootOwnerScript = dest;
    rootOwner.ConstructTransaction(rootOwnerScript);
    tx.vout.emplace_back(0, rootOwnerScript);
    CScript verifierScript;
    CNullAssetTxVerifierString verifier("true");
    verifier.ConstructTransaction(verifierScript);
    tx.vout.emplace_back(0, verifierScript);
    CNewAsset restricted("$" + root, 5 * COIN, 0, 0, 0, "");
    CScript assetScript = dest;
    restricted.ConstructTransaction(assetScript);
    tx.vout.emplace_back(0, assetScript);
    return tx;
}

CMutableTransaction MakeValidQualifierIssueTx(const CScript& dest)
{
    CMutableTransaction tx;
    tx.vout.emplace_back(GetBurnAmount(AssetType::QUALIFIER), AssetBurnScript(AssetType::QUALIFIER));
    CNewAsset qual("#REGQUAL", 5 * COIN, 0, 0, 0, "");
    CScript qualScript = dest;
    qual.ConstructTransaction(qualScript);
    tx.vout.emplace_back(0, qualScript);
    return tx;
}

CMutableTransaction MakeValidUniqueIssueTx(const std::string& root, const CScript& dest)
{
    CMutableTransaction tx;
    tx.vout.emplace_back(GetBurnAmount(AssetType::UNIQUE), AssetBurnScript(AssetType::UNIQUE));
    CAssetTransfer rootOwner(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
    CScript rootOwnerScript = dest;
    rootOwner.ConstructTransaction(rootOwnerScript);
    tx.vout.emplace_back(0, rootOwnerScript);
    CNewAsset unique(root + "#UNIQ1", 1, 0, 0, 0, "");
    CScript uniqueScript = dest;
    unique.ConstructTransaction(uniqueScript);
    tx.vout.emplace_back(0, uniqueScript);
    return tx;
}

CMutableTransaction MakeValidMsgChannelTx(const std::string& root, const CScript& dest)
{
    CMutableTransaction tx;
    tx.vout.emplace_back(GetBurnAmount(AssetType::MSGCHANNEL), AssetBurnScript(AssetType::MSGCHANNEL));
    CAssetTransfer rootOwner(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
    CScript rootOwnerScript = dest;
    rootOwner.ConstructTransaction(rootOwnerScript);
    tx.vout.emplace_back(0, rootOwnerScript);
    CNewAsset channel(root + "~CHANNEL", 1 * COIN, 0, 0, 0, "");
    CScript channelScript = dest;
    channel.ConstructTransaction(channelScript);
    tx.vout.emplace_back(0, channelScript);
    return tx;
}

CMutableTransaction MakeValidIssueMineableTx(const std::string& root, const CScript& dest)
{
    CMutableTransaction tx;
    CIssueMineable issue;
    issue.strRootAsset = root;
    issue.strMineableAsset = MineableAssetNameFromRoot(root);
    issue.nTotalQty = 500 * COIN;
    issue.nPerBlock = 50 * COIN;
    issue.nNthBlock = 5;
    issue.nUnits = 0;
    tx.vout.emplace_back(CalculateMineableIssuanceCost(issue), IssueMineableBurnScript());
    CScript mineScript = dest;
    issue.ConstructTransaction(mineScript);
    tx.vout.emplace_back(0, mineScript);
    return tx;
}

} // namespace

// ---------------------------------------------------------------------------
// Suite 1: Verify* matrix — every issuance / reissue transaction shape
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_SUITE(asset_regression_verify_matrix, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(verify_all_issue_types)
{
    ActivateAllAssetFeaturesForTest();
    const CScript dest = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    std::string err;

    BOOST_CHECK(CTransaction(MakeValidIssueRootTx(dest)).VerifyNewAsset(err));

    BOOST_CHECK(CTransaction(MakeValidQualifierIssueTx(dest)).VerifyNewQualfierAsset(err));

    BOOST_CHECK(CTransaction(MakeValidRestrictedIssueTx("RESTROOT", dest)).VerifyNewRestrictedAsset(err));

    BOOST_CHECK(CTransaction(MakeValidUniqueIssueTx("UNIQROOT", dest)).VerifyNewUniqueAsset(err));

    BOOST_CHECK(CTransaction(MakeValidMsgChannelTx("MSGROOT", dest)).VerifyNewMsgChannelAsset(err));

    CMutableTransaction subQual = MakeValidQualifierIssueTx(dest);
    CNewAsset subQ("#REGQUAL/#SUB1", 5 * COIN, 0, 0, 0, "");
    subQual.vout.clear();
    subQual.vout.emplace_back(GetBurnAmount(AssetType::SUB_QUALIFIER), AssetBurnScript(AssetType::SUB_QUALIFIER));
    CAssetTransfer parent("#REGQUAL", OWNER_ASSET_AMOUNT);
    CScript parentScript = dest;
    parent.ConstructTransaction(parentScript);
    subQual.vout.emplace_back(0, parentScript);
    CScript sqScript = dest;
    subQ.ConstructTransaction(sqScript);
    subQual.vout.emplace_back(0, sqScript);
    BOOST_CHECK(CTransaction(subQual).VerifyNewQualfierAsset(err));

    CMutableTransaction sub = MakeValidIssueRootTx(dest);
    sub.vout.clear();
    CNewAsset subAsset("UNIQROOT/SUB1", COIN, 0, 1, 0, "");
    sub.vout.emplace_back(GetBurnAmount(AssetType::SUB), AssetBurnScript(AssetType::SUB));
    CAssetTransfer parentOwner("UNIQROOT!", OWNER_ASSET_AMOUNT);
    CScript poScript = dest;
    parentOwner.ConstructTransaction(poScript);
    sub.vout.emplace_back(0, poScript);
    CScript saScript = dest;
    subAsset.ConstructTransaction(saScript);
    sub.vout.emplace_back(0, saScript);
    BOOST_CHECK(CTransaction(sub).VerifyNewAsset(err));
}

BOOST_AUTO_TEST_CASE(verify_transfer_and_null_data_scripts)
{
    ActivateAllAssetFeaturesForTest();
    const CScript dest = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

    CAssetTransfer transfer("REGROOT", 100 * COIN);
    CScript xferScript = dest;
    transfer.ConstructTransaction(xferScript);
    CNullAssetTxData tagData("#TAG", (int)QualifierType::ADD_QUALIFIER);
    CScript tagScript = GetScriptForNullAssetDataDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    tagData.ConstructTransaction(tagScript);

    CNullAssetTxData freezeData("$ASSET", (int)RestrictedType::FREEZE_ADDRESS);
    CScript freezeScript = tagScript;
    freezeData.ConstructTransaction(freezeScript);

    CNullAssetTxData globalFreeze("$ASSET", (int)RestrictedType::GLOBAL_FREEZE);
    CScript globalScript;
    globalFreeze.ConstructGlobalRestrictionTransaction(globalScript);

    CNullAssetTxVerifierString verifier("true");
    CScript verifierScript;
    verifier.ConstructTransaction(verifierScript);

    CNullAssetTxData parsed;
    std::string addr;
    BOOST_CHECK(AssetNullDataFromScript(tagScript, parsed, addr));
    BOOST_CHECK(AssetNullDataFromScript(freezeScript, parsed, addr));
    BOOST_CHECK(GlobalAssetNullDataFromScript(globalScript, parsed));
    std::string verifyErr;
    BOOST_CHECK(CheckVerifierAssetTxOut(CTxOut(0, verifierScript), verifyErr));
}

BOOST_AUTO_TEST_SUITE_END()

// ---------------------------------------------------------------------------
// Suite 2: Cache / contextual — restricted + reissue without full chain
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_SUITE(asset_regression_cache_ops, TestingSetup)

BOOST_AUTO_TEST_CASE(verify_reissue_and_mineable_tx)
{
    ActivateAllAssetFeaturesForTest();
    const CScript dest = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    std::string err;

    BOOST_CHECK(CTransaction(MakeValidReissueTx("REISSUE_ROOT", dest)).VerifyReissueAsset(err));

    passets->mapAssetsAddressAmount[std::make_pair("MINE_ROOT!", GetParams().GlobalBurnAddress())] = OWNER_ASSET_AMOUNT;
    CMineableSchedule sched;
    sched.strRootAsset = "MINE_ROOT";
    sched.strMineableAsset = "&MINE_ROOT";
    sched.nTotalQty = 500 * COIN;
    sched.nPerBlock = 50 * COIN;
    sched.nNthBlock = 5;
    sched.fAllowExtension = true;
    sched.nMaxTotalQty = 1000 * COIN;
    sched.fActive = true;
    pmineabledb->WriteSchedule(sched);

    CMutableTransaction mineIssue = MakeValidIssueMineableTx("MINE_ROOT", dest);
    BOOST_CHECK(CTransaction(mineIssue).VerifyIssueMineable(err));

    CReissueMineableSchedule ext;
    ext.strMineableAsset = "&MINE_ROOT";
    ext.nAddQty = 50 * COIN;
    CMutableTransaction mineExt;
    mineExt.vout.emplace_back(CalculateMineableExtensionCost(sched, ext.nAddQty, 0), IssueMineableBurnScript());
    CAssetTransfer ownerReturn("MINE_ROOT!", OWNER_ASSET_AMOUNT);
    CScript ownerScript = dest;
    ownerReturn.ConstructTransaction(ownerScript);
    mineExt.vout.emplace_back(0, ownerScript);
    CScript extScript = dest;
    ext.ConstructTransaction(extScript);
    mineExt.vout.emplace_back(0, extScript);
    BOOST_CHECK(CTransaction(mineExt).VerifyReissueMineable(err));
}

BOOST_AUTO_TEST_CASE(cache_reissue_roundtrip)
{
    ActivateAllAssetFeaturesForTest();
    CAssetsCache cache;
    const std::string root = "CACHERT";
    CNewAsset asset(root, 100 * COIN, 0, 1, 0, "");
    BOOST_CHECK(cache.AddNewAsset(asset, GetParams().GlobalBurnAddress(), 1, uint256()));

    CReissueAsset reissue(root, 10 * COIN, 0, 0, "");
    COutPoint out(uint256S("ab"), 1);
    BOOST_CHECK(cache.AddReissueAsset(reissue, GetParams().GlobalBurnAddress(), out));

    CNewAsset updated;
    BOOST_CHECK(cache.GetAssetMetaDataIfExists(root, updated));
    BOOST_CHECK_EQUAL(updated.nAmount, 110 * COIN);

    std::vector<std::pair<std::string, CBlockAssetUndo>> undo;
    undo.emplace_back(root, CBlockAssetUndo{true, false, "", 0, ASSET_UNDO_INCLUDES_VERIFIER_STRING, false, ""});
    BOOST_CHECK(cache.RemoveReissueAsset(reissue, GetParams().GlobalBurnAddress(), out, undo));
    BOOST_CHECK(cache.GetAssetMetaDataIfExists(root, updated));
    BOOST_CHECK_EQUAL(updated.nAmount, 100 * COIN);
}

BOOST_AUTO_TEST_CASE(cache_restricted_qualifier_freeze_ops)
{
    ActivateAllAssetFeaturesForTest();
    CAssetsCache cache;
    const std::string restricted = "$RESTEST";
    const std::string address = GetParams().GlobalBurnAddress();
    const std::string qual = "#VIP";

    CNewAsset asset(restricted.substr(1), 100 * COIN, 0, 1, 0, "");
    cache.AddNewAsset(CNewAsset(restricted, 100 * COIN, 0, 0, 0, ""), address, 1, uint256());
    cache.AddRestrictedVerifier(restricted, "true");

    CNullAssetTxData addTag(qual, (int)QualifierType::ADD_QUALIFIER);
    std::string err;
    BOOST_CHECK(VerifyQualifierChange(cache, addTag, address, err));
    BOOST_CHECK(cache.AddQualifierAddress(restricted, address, QualifierType::ADD_QUALIFIER));

    CNullAssetTxData freeze(restricted, (int)RestrictedType::FREEZE_ADDRESS);
    BOOST_CHECK(VerifyRestrictedAddressChange(cache, freeze, address, err));
    BOOST_CHECK(cache.AddRestrictedAddress(restricted, address, RestrictedType::FREEZE_ADDRESS));

    CNullAssetTxData globalFreeze(restricted, (int)RestrictedType::GLOBAL_FREEZE);
    BOOST_CHECK(VerifyGlobalRestrictedChange(cache, globalFreeze, err));
    BOOST_CHECK(cache.AddGlobalRestricted(restricted, RestrictedType::GLOBAL_FREEZE));

    CNullAssetTxData unfreeze(restricted, (int)RestrictedType::UNFREEZE_ADDRESS);
    BOOST_CHECK(VerifyRestrictedAddressChange(cache, unfreeze, address, err));

    CNullAssetTxData removeTag(qual, (int)QualifierType::REMOVE_QUALIFIER);
    BOOST_CHECK(VerifyQualifierChange(cache, removeTag, address, err));

    CNullAssetTxData globalUnfreeze(restricted, (int)RestrictedType::GLOBAL_UNFREEZE);
    BOOST_CHECK(VerifyGlobalRestrictedChange(cache, globalUnfreeze, err));
}

BOOST_AUTO_TEST_CASE(contextual_transfer_and_reissue_checks)
{
    ActivateAllAssetFeaturesForTest();
    CAssetsCache cache;
    const std::string root = "CTXROOT";
    CNewAsset asset(root, 1000 * COIN, 0, 1, 0, "");
    cache.AddNewAsset(asset, GetParams().GlobalBurnAddress(), 1, uint256());

    CAssetTransfer xfer(root, 100 * COIN);
    std::string err;
    BOOST_CHECK(ContextualCheckTransferAsset(&cache, xfer, GetParams().GlobalBurnAddress(), err));

    CReissueAsset reissue(root, 50 * COIN, 0, 0, "");
    BOOST_CHECK(ContextualCheckReissueAsset(&cache, reissue, err));

    CNewAsset duplicate(root, 1 * COIN, 0, 1, 0, "");
    BOOST_CHECK(!ContextualCheckNewAsset(&cache, duplicate, err, true));
}

BOOST_AUTO_TEST_CASE(check_tx_assets_transfer_balance)
{
    ActivateAllAssetFeaturesForTest();
    CCoinsView view;
    CCoinsViewCache coins(&view);

    CAssetTransfer asset("BALTEST", 1000 * COIN);
    CScript script = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    asset.ConstructTransaction(script);
    uint256 hash = uint256S("1111111111111111111111111111111111111111111111111111111111111111");
    coins.AddCoin(COutPoint(hash, 0), Coin(CTxOut(0, script), 200, 0), true);

    CMutableTransaction spend;
    spend.vin.emplace_back(COutPoint(hash, 0));
    CAssetTransfer outXfer("BALTEST", 1000 * COIN);
    CScript outScript = script;
    outXfer.ConstructTransaction(outScript);
    spend.vout.emplace_back(0, outScript);

    CValidationState state;
    std::vector<std::pair<std::string, uint256>> vReissue;
    BOOST_CHECK(Consensus::CheckTxAssets(spend, state, coins, passets, false, vReissue, true));

    spend.vout[0].nValue = 1;
    CScript badScript = script;
    CAssetTransfer badXfer("BALTEST", 500 * COIN);
    badXfer.ConstructTransaction(badScript);
    spend.vout[0].scriptPubKey = badScript;
    BOOST_CHECK(!Consensus::CheckTxAssets(spend, state, coins, passets, false, vReissue, true));
}

BOOST_AUTO_TEST_SUITE_END()

// ---------------------------------------------------------------------------
// Suite 3: On-chain regression — every major type on regtest chain
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_SUITE(asset_regression_chain, TestChain100Setup)

BOOST_AUTO_TEST_CASE(chain_issue_root_sub_unique_qualifier_restricted_msg_mineable)
{
    ActivateAllAssetFeaturesForTest();
    AssetChainHelper H(*this);

    const std::string root = H.NextName("ROOT");
    H.IssueRootOnChain(root);

    CMutableTransaction subTx = H.BuildIssueSubTx(root, root + "/SUBA");
    std::string err;
    BOOST_CHECK(CTransaction(subTx).VerifyNewAsset(err));
    H.SubmitBlock(subTx);

    CMutableTransaction uniqueTx = H.BuildIssueUniqueTx(root, root + "#U001");
    BOOST_CHECK(CTransaction(uniqueTx).VerifyNewUniqueAsset(err));
    H.SubmitBlock(uniqueTx);

    const std::string qualName = "#" + H.NextName("Q");
    CMutableTransaction qualTx = H.BuildIssueQualifierTx(qualName);
    BOOST_CHECK(CTransaction(qualTx).VerifyNewQualfierAsset(err));
    H.SubmitBlock(qualTx);

    CMutableTransaction subQualTx = H.BuildIssueSubQualifierTx(qualName, qualName + "/SUBQ");
    BOOST_CHECK(CTransaction(subQualTx).VerifyNewQualfierAsset(err));
    H.SubmitBlock(subQualTx);

    CMutableTransaction restTx = H.BuildIssueRestrictedTx(root, "$" + root);
    BOOST_CHECK(CTransaction(restTx).VerifyNewRestrictedAsset(err));
    H.SubmitBlock(restTx);

    CMutableTransaction msgTx = H.BuildIssueMsgChannelTx(root, root + "~CH01");
    BOOST_CHECK(CTransaction(msgTx).VerifyNewMsgChannelAsset(err));
    H.SubmitBlock(msgTx);

    CIssueMineable mine;
    mine.strRootAsset = root;
    mine.strMineableAsset = MineableAssetNameFromRoot(root);
    mine.nTotalQty = 200 * COIN;
    mine.nPerBlock = 50 * COIN;
    mine.nNthBlock = 2;
    mine.nUnits = 0;
    CMutableTransaction mineTx = H.BuildIssueMineableTx(mine);
    BOOST_CHECK(CTransaction(mineTx).VerifyIssueMineable(err));
    H.SubmitBlock(mineTx);

    CMineableSchedule mineSched;
    BOOST_CHECK(pmineabledb->ReadSchedule(mine.strMineableAsset, mineSched));
    BOOST_CHECK(passets->CheckIfAssetExists(root));
    BOOST_CHECK(passets->CheckIfAssetExists("$" + root));
}

BOOST_AUTO_TEST_CASE(chain_reissue_and_transfer)
{
    ActivateAllAssetFeaturesForTest();
    AssetChainHelper H(*this);
    const std::string root = H.IssueRootOnChain(H.NextName("RIS"));

    CMutableTransaction reissueTx = H.BuildReissueTx(root, 5 * COIN);
    std::string err;
    BOOST_CHECK(CTransaction(reissueTx).VerifyReissueAsset(err));
    H.SubmitBlock(reissueTx);

    CNewAsset meta;
    BOOST_CHECK(passets->GetAssetMetaDataIfExists(root, meta));
    BOOST_CHECK(meta.nAmount >= 6 * COIN);

    CMutableTransaction xferTx = H.BuildTransferTx(H.assetOutpoints.at(root), H.issueTxByRoot.at(root),
                                                    root, 1 * COIN, H.miner);
    CValidationState state;
    std::vector<std::pair<std::string, uint256>> vReissue;
    CCoinsViewCache coins(pcoinsTip);
    BOOST_CHECK(Consensus::CheckTxAssets(xferTx, state, coins, passets, false, vReissue, true));
    H.SubmitBlock(xferTx);
}

BOOST_AUTO_TEST_CASE(chain_mineable_mine_claim)
{
    ActivateAllAssetFeaturesForTest();
    AssetChainHelper H(*this);
    const std::string root = H.IssueRootOnChain(H.NextName("MNE"));
    CIssueMineable issue;
    issue.strRootAsset = root;
    issue.strMineableAsset = MineableAssetNameFromRoot(root);
    issue.nTotalQty = 200 * COIN;
    issue.nPerBlock = 50 * COIN;
    issue.nNthBlock = 2;
    issue.nUnits = 0;
    CMutableTransaction mineTx = H.BuildIssueMineableTx(issue);
    H.SubmitBlock(mineTx);

    H.MineBlocks(4);
    H.ClaimMineable(issue.strMineableAsset);

    CMineableSchedule s;
    pmineabledb->ReadSchedule(issue.strMineableAsset, s);
    BOOST_CHECK(s.nTotalMinted > 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ---------------------------------------------------------------------------
// Suite 4: Full stress — enumerate assets, all operation types in a loop
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_SUITE(asset_regression_stress, TestChain100Setup)

BOOST_AUTO_TEST_CASE(stress_multi_type_regression_loop)
{
    ActivateAllAssetFeaturesForTest();
    AssetChainHelper H(*this);
    const int rounds = 6;
    int rootsIssued = 0;
    int restrictedIssued = 0;
    int mineableClaimed = 0;
    int reissues = 0;

    for (int r = 0; r < rounds; ++r) {
        const std::string root = H.NextName("STR");
        H.IssueRootOnChain(root);
        ++rootsIssued;

        if (r % 2 == 0) {
            CMutableTransaction reissueTx = H.BuildReissueTx(root, 1 * COIN);
            H.SubmitBlock(reissueTx);
            ++reissues;
        }

        const std::string qual = "#" + H.NextName("TQ");
        H.SubmitBlock(H.BuildIssueQualifierTx(qual));

        CMutableTransaction restTx = H.BuildIssueRestrictedTx(root, "$" + root, "true");
        H.SubmitBlock(restTx);
        ++restrictedIssued;

        passets->AddRestrictedVerifier("$" + root, "true");
        const std::string addr = H.MinerAddress();
        CNullAssetTxData addTag(qual, (int)QualifierType::ADD_QUALIFIER);
        std::string err;
        VerifyQualifierChange(*passets, addTag, addr, err);
        passets->AddQualifierAddress("$" + root, addr, QualifierType::ADD_QUALIFIER);

        CIssueMineable mine;
        mine.strRootAsset = root;
        mine.strMineableAsset = MineableAssetNameFromRoot(root);
        mine.nTotalQty = 100 * COIN;
        mine.nPerBlock = 25 * COIN;
        mine.nNthBlock = 2;
        mine.nUnits = 0;
        mine.fAllowExtension = (r % 3 == 0);
        mine.nMaxTotalQty = mine.fAllowExtension ? 200 * COIN : 0;
        CMutableTransaction mineTx = H.BuildIssueMineableTx(mine);
        H.SubmitBlock(mineTx);

        H.MineBlocks(3);
        H.ClaimMineable(mine.strMineableAsset);

        CMineableSchedule ms;
        if (pmineabledb->ReadSchedule(mine.strMineableAsset, ms) && ms.nTotalMinted > 0)
            ++mineableClaimed;

        if (mine.fAllowExtension) {
            CReissueMineableSchedule ext;
            ext.strMineableAsset = mine.strMineableAsset;
            ext.nAddQty = 25 * COIN;
            CMineableSchedule sched;
            pmineabledb->ReadSchedule(mine.strMineableAsset, sched);
            CMutableTransaction extTx = H.BuildReissueMineableTx(ext, sched);
            H.SubmitBlock(extTx);
        }

        H.SubmitBlock(H.BuildIssueUniqueTx(root, root + "#S" + std::to_string(r)));

        if (r % 3 == 1)
            H.SubmitBlock(H.BuildIssueMsgChannelTx(root, root + "~M" + std::to_string(r)));
    }

    BOOST_CHECK_EQUAL(rootsIssued, rounds);
    BOOST_CHECK(restrictedIssued >= rounds);
    BOOST_CHECK(reissues >= rounds / 2);
    BOOST_CHECK(mineableClaimed >= rounds / 2);

    std::vector<CMineableSchedule> mineableSchedules;
    pmineabledb->ListSchedules(mineableSchedules);
    BOOST_CHECK(static_cast<int>(mineableSchedules.size()) >= rounds);
}

BOOST_AUTO_TEST_CASE(stress_restricted_lifecycle_cache)
{
    ActivateAllAssetFeaturesForTest();
    CAssetsCache cache;
    const std::string restricted = "$STRESSREST";
    const std::string addr = GetParams().GlobalBurnAddress();

    cache.AddNewAsset(CNewAsset(restricted, 100 * COIN, 0, 0, 0, ""), addr, 1, uint256());
    cache.AddRestrictedVerifier(restricted, "true");

    for (int i = 0; i < 10; ++i) {
        const std::string qual = "#TAG" + std::to_string(i);
        CNullAssetTxData add(qual, (int)QualifierType::ADD_QUALIFIER);
        std::string err;
        BOOST_CHECK(VerifyQualifierChange(cache, add, addr, err));
        cache.AddQualifierAddress(restricted, addr, QualifierType::ADD_QUALIFIER);

        CNullAssetTxData freeze(restricted, (int)RestrictedType::FREEZE_ADDRESS);
        BOOST_CHECK(VerifyRestrictedAddressChange(cache, freeze, addr, err));
        cache.AddRestrictedAddress(restricted, addr, RestrictedType::FREEZE_ADDRESS);

        CNullAssetTxData unfreeze(restricted, (int)RestrictedType::UNFREEZE_ADDRESS);
        BOOST_CHECK(VerifyRestrictedAddressChange(cache, unfreeze, addr, err));

        CNullAssetTxData remove(qual, (int)QualifierType::REMOVE_QUALIFIER);
        BOOST_CHECK(VerifyQualifierChange(cache, remove, addr, err));
    }

    CNullAssetTxData gFreeze(restricted, (int)RestrictedType::GLOBAL_FREEZE);
    std::string err;
    BOOST_CHECK(VerifyGlobalRestrictedChange(cache, gFreeze, err));
    cache.AddGlobalRestricted(restricted, RestrictedType::GLOBAL_FREEZE);
    CNullAssetTxData gUnfreeze(restricted, (int)RestrictedType::GLOBAL_UNFREEZE);
    BOOST_CHECK(VerifyGlobalRestrictedChange(cache, gUnfreeze, err));
}

BOOST_AUTO_TEST_CASE(stress_verify_matrix_all_types_not_broken)
{
    ActivateAllAssetFeaturesForTest();
    const CScript dest = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    std::string err;

    BOOST_CHECK(CTransaction(MakeValidIssueRootTx(dest)).VerifyNewAsset(err));
    err.clear();
    BOOST_CHECK(CTransaction(MakeValidReissueTx("XROOT", dest)).VerifyReissueAsset(err));
    err.clear();
    BOOST_CHECK(CTransaction(MakeValidQualifierIssueTx(dest)).VerifyNewQualfierAsset(err));
    err.clear();
    BOOST_CHECK(CTransaction(MakeValidRestrictedIssueTx("RROOT", dest)).VerifyNewRestrictedAsset(err));
    err.clear();
    BOOST_CHECK(CTransaction(MakeValidUniqueIssueTx("UROOT", dest)).VerifyNewUniqueAsset(err));
    err.clear();
    BOOST_CHECK(CTransaction(MakeValidMsgChannelTx("MROOT", dest)).VerifyNewMsgChannelAsset(err));
    err.clear();
    BOOST_CHECK(CTransaction(MakeValidIssueMineableTx("IROOT", dest)).VerifyIssueMineable(err));
}

BOOST_AUTO_TEST_SUITE_END()
