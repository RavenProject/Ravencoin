// Copyright (c) 2026 The Ravencoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_TEST_ASSET_TEST_HELPERS_H
#define RAVEN_TEST_ASSET_TEST_HELPERS_H

#include <assets/assets.h>
#include <assets/mineable.h>
#include <base58.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <key.h>
#include <keystore.h>
#include <script/sign.h>
#include <test/test_raven.h>
#include <util.h>
#include <validation.h>

#include <map>
#include <string>
#include <vector>

/** Force all asset soft forks active in unit / chain tests. */
inline void ActivateAllAssetFeaturesForTest()
{
    SetAssetsActive(true);
    SetRip5AssetsActive(true);
    SetMineableAssetsActive(true);
    SetEnforcedValues(true);
    SetEnforcedCoinbase(true);
    SetTransferOverflow(true);
    SetTransferScriptsSizeActive(true);
    fAssetIndex = true;
}

inline CScript MinerP2PKHScript(const CKey& key)
{
    return CScript() << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
}

inline CScript AssetBurnScript(AssetType type)
{
    return GetScriptForDestination(DecodeDestination(GetBurnAddress(type)));
}

/** Signed regtest chain helper for asset regression tests. */
struct AssetChainHelper
{
    TestChain100Setup& chain;
    CScript miner;
    CBasicKeyStore keystore;
    int nextCoinbase;
    int nameSerial;

    std::map<std::string, COutPoint> ownerOutpoints;
    std::map<std::string, CMutableTransaction> issueTxByRoot;
    std::map<std::string, COutPoint> assetOutpoints;

    explicit AssetChainHelper(TestChain100Setup& setup)
        : chain(setup), miner(MinerP2PKHScript(setup.coinbaseKey)), nextCoinbase(0), nameSerial(0)
    {
        keystore.AddKey(setup.coinbaseKey);
    }

    std::string NextName(const std::string& prefix = "REG")
    {
        return strprintf("%s%04d", prefix, nameSerial++);
    }

    std::string MinerAddress() const
    {
        return EncodeDestination(chain.coinbaseKey.GetPubKey().GetID());
    }

    void SignPrevout(CMutableTransaction& tx, unsigned int nIn, const CTransaction& prevTx) const
    {
        if (!SignSignature(keystore, prevTx, tx, nIn, SIGHASH_ALL))
            throw std::runtime_error("SignSignature failed");
    }

    void AppendCoinbaseInput(CMutableTransaction& tx)
    {
        if (nextCoinbase >= static_cast<int>(chain.coinbaseTxns.size()))
            throw std::runtime_error("out of coinbase txns");
        tx.vin.emplace_back(COutPoint(chain.coinbaseTxns[nextCoinbase].GetHash(), 0));
        ++nextCoinbase;
    }

    void AppendAssetInput(CMutableTransaction& tx, const COutPoint& out, const CMutableTransaction& prevTx)
    {
        tx.vin.emplace_back(out);
        SignPrevout(tx, tx.vin.size() - 1, prevTx);
    }

    uint256 SubmitBlock(const CMutableTransaction& tx)
    {
        chain.CreateAndProcessBlock({tx}, miner);
        return tx.GetHash();
    }

    CMutableTransaction BuildIssueRootTx(const std::string& root, CAmount qty = COIN)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        CNewAsset asset(root, qty, 0, 1, 0, "");
        tx.vout.emplace_back(GetBurnAmount(AssetType::ROOT), AssetBurnScript(AssetType::ROOT));
        CScript ownerScript = miner;
        asset.ConstructOwnerTransaction(ownerScript);
        tx.vout.emplace_back(0, ownerScript);
        CScript assetScript = miner;
        asset.ConstructTransaction(assetScript);
        tx.vout.emplace_back(0, assetScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    std::string IssueRootOnChain(const std::string& root, CAmount qty = COIN)
    {
        CMutableTransaction tx = BuildIssueRootTx(root, qty);
        SubmitBlock(tx);
        ownerOutpoints[root] = COutPoint(tx.GetHash(), 1);
        assetOutpoints[root] = COutPoint(tx.GetHash(), 2);
        issueTxByRoot[root] = tx;
        return root;
    }

    CMutableTransaction BuildIssueSubTx(const std::string& root, const std::string& subName, CAmount qty = COIN)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        CNewAsset asset(subName, qty, 0, 1, 0, "");
        tx.vout.emplace_back(GetBurnAmount(AssetType::SUB), AssetBurnScript(AssetType::SUB));
        CAssetTransfer parentOwner(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
        CScript parentScript = miner;
        parentOwner.ConstructTransaction(parentScript);
        tx.vout.emplace_back(0, parentScript);
        CScript assetScript = miner;
        asset.ConstructTransaction(assetScript);
        tx.vout.emplace_back(0, assetScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CMutableTransaction BuildIssueUniqueTx(const std::string& root, const std::string& uniqueName)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        CNewAsset unique(uniqueName, 1, 0, 0, 0, "");
        tx.vout.emplace_back(GetBurnAmount(AssetType::UNIQUE), AssetBurnScript(AssetType::UNIQUE));
        CAssetTransfer parentOwner(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
        CScript parentScript = miner;
        parentOwner.ConstructTransaction(parentScript);
        tx.vout.emplace_back(0, parentScript);
        CScript uniqueScript = miner;
        unique.ConstructTransaction(uniqueScript);
        tx.vout.emplace_back(0, uniqueScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CMutableTransaction BuildIssueQualifierTx(const std::string& qualName, CAmount qty = 5 * COIN)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        CNewAsset qual(qualName, qty, 0, 0, 0, "");
        tx.vout.emplace_back(GetBurnAmount(AssetType::QUALIFIER), AssetBurnScript(AssetType::QUALIFIER));
        CScript qualScript = miner;
        qual.ConstructTransaction(qualScript);
        tx.vout.emplace_back(0, qualScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CMutableTransaction BuildIssueSubQualifierTx(const std::string& parentQual, const std::string& subQual)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        CNewAsset qual(subQual, 5 * COIN, 0, 0, 0, "");
        tx.vout.emplace_back(GetBurnAmount(AssetType::SUB_QUALIFIER), AssetBurnScript(AssetType::SUB_QUALIFIER));
        CAssetTransfer parentTransfer(parentQual, OWNER_ASSET_AMOUNT);
        CScript parentScript = miner;
        parentTransfer.ConstructTransaction(parentScript);
        tx.vout.emplace_back(0, parentScript);
        CScript qualScript = miner;
        qual.ConstructTransaction(qualScript);
        tx.vout.emplace_back(0, qualScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CMutableTransaction BuildIssueRestrictedTx(const std::string& root, const std::string& restrictedName,
                                               const std::string& verifier = "true")
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        CNewAsset restricted(restrictedName, 5 * COIN, 0, 0, 0, "");
        tx.vout.emplace_back(GetBurnAmount(AssetType::RESTRICTED), AssetBurnScript(AssetType::RESTRICTED));
        CAssetTransfer rootOwner(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
        CScript rootOwnerScript = miner;
        rootOwner.ConstructTransaction(rootOwnerScript);
        tx.vout.emplace_back(0, rootOwnerScript);
        CScript verifierScript;
        CNullAssetTxVerifierString verifierData(verifier);
        verifierData.ConstructTransaction(verifierScript);
        tx.vout.emplace_back(0, verifierScript);
        CScript assetScript = miner;
        restricted.ConstructTransaction(assetScript);
        tx.vout.emplace_back(0, assetScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CMutableTransaction BuildIssueMsgChannelTx(const std::string& root, const std::string& channelName)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        CNewAsset channel(channelName, 1 * COIN, 0, 0, 0, "");
        tx.vout.emplace_back(GetBurnAmount(AssetType::MSGCHANNEL), AssetBurnScript(AssetType::MSGCHANNEL));
        CAssetTransfer rootOwner(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
        CScript rootOwnerScript = miner;
        rootOwner.ConstructTransaction(rootOwnerScript);
        tx.vout.emplace_back(0, rootOwnerScript);
        CScript channelScript = miner;
        channel.ConstructTransaction(channelScript);
        tx.vout.emplace_back(0, channelScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CMutableTransaction BuildReissueTx(const std::string& root, CAmount addQty)
    {
        CMutableTransaction tx;
        tx.vin.emplace_back(ownerOutpoints.at(root));
        AppendCoinbaseInput(tx);
        tx.vout.emplace_back(GetReissueAssetBurnAmount(), GetScriptForDestination(
            DecodeDestination(GetParams().ReissueAssetBurnAddress())));
        CReissueAsset reissue(root, addQty, 0, 0, "");
        CScript reissueScript = miner;
        reissue.ConstructTransaction(reissueScript);
        tx.vout.emplace_back(0, reissueScript);
        CAssetTransfer ownerReturn(root + OWNER_TAG, OWNER_ASSET_AMOUNT);
        CScript ownerScript = miner;
        ownerReturn.ConstructTransaction(ownerScript);
        tx.vout.emplace_back(0, ownerScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, issueTxByRoot.at(root));
        SignPrevout(tx, 1, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CMutableTransaction BuildTransferTx(const COutPoint& assetIn, const CMutableTransaction& prevIssueTx,
                                        const std::string& assetName, CAmount amount, const CScript& dest)
    {
        CMutableTransaction tx;
        tx.vin.emplace_back(assetIn);
        CAssetTransfer xfer(assetName, amount);
        CScript outScript = dest;
        xfer.ConstructTransaction(outScript);
        tx.vout.emplace_back(0, outScript);
        SignPrevout(tx, 0, prevIssueTx);
        return tx;
    }

    CMutableTransaction BuildIssueMineableTx(const CIssueMineable& issue)
    {
        CMutableTransaction tx;
        AppendCoinbaseInput(tx);
        const CAmount burn = CalculateMineableIssuanceCost(issue);
        tx.vout.emplace_back(burn, GetScriptForDestination(
            DecodeDestination(GetParams().IssueAssetBurnAddress())));
        CScript mineScript = miner;
        issue.ConstructTransaction(mineScript);
        tx.vout.emplace_back(0, mineScript);
        tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    CMutableTransaction BuildReissueMineableTx(const CReissueMineableSchedule& reissue,
                                             const CMineableSchedule& schedule)
    {
        CMutableTransaction tx;
        tx.vin.emplace_back(ownerOutpoints.at(schedule.strRootAsset));
        const CAmount burn = CalculateMineableExtensionCost(schedule, reissue.nAddQty, reissue.nNewNthBlock);
        unsigned int coinbaseVin = 0;
        if (burn > 0) {
            AppendCoinbaseInput(tx);
            coinbaseVin = 1;
            tx.vout.emplace_back(burn, GetScriptForDestination(
                DecodeDestination(GetParams().IssueAssetBurnAddress())));
        }
        CAssetTransfer ownerReturn(schedule.strRootAsset + OWNER_TAG, OWNER_ASSET_AMOUNT);
        CScript ownerScript = miner;
        ownerReturn.ConstructTransaction(ownerScript);
        tx.vout.emplace_back(0, ownerScript);
        CScript reissueScript = miner;
        reissue.ConstructTransaction(reissueScript);
        tx.vout.emplace_back(0, reissueScript);
        if (burn > 0)
            tx.vout.emplace_back(50 * COIN, miner);
        SignPrevout(tx, 0, issueTxByRoot.at(schedule.strRootAsset));
        if (burn > 0)
            SignPrevout(tx, coinbaseVin, chain.coinbaseTxns[nextCoinbase - 1]);
        return tx;
    }

    void MineBlocks(int n)
    {
        for (int i = 0; i < n; ++i)
            chain.CreateAndProcessBlock({}, miner);
    }

    void ClaimMineable(const std::string& mineableAsset)
    {
        CMineableSchedule live;
        pmineabledb->ReadSchedule(mineableAsset, live);
        live.UpdateMaturity(chainActive.Height());
        const CAmount amt = live.GetAccruedAmount();
        if (amt <= 0)
            return;
        std::map<std::string, CAmount> claims;
        claims[mineableAsset] = amt;
        chain.CreateAndProcessBlockWithMineableClaims({}, miner, claims);
    }
};

#endif // RAVEN_TEST_ASSET_TEST_HELPERS_H
