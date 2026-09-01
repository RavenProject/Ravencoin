// Copyright (c) 2026 The Ravencoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/mineable.h"
#include "assets/mineabledb.h"
#include "assets/assets.h"
#include "base58.h"
#include "consensus/validation.h"
#include "assets/assetdb.h"
#include "validation.h"

#include <algorithm>

CMineableSchedule::CMineableSchedule()
{
    strRootAsset.clear();
    strMineableAsset.clear();
    nTotalQty = 0;
    nPerBlock = 0;
    nNthBlock = 10;
    nFundAmt = 0;
    nUnits = 0;
    nHasIPFS = 0;
    strIPFSHash.clear();
    nStartHeight = 0;
    nMaturedPeriods = 0;
    nClaimedPeriods = 0;
    nTotalMinted = 0;
    issuanceTxid.SetNull();
    fActive = false;
    fAllowExtension = false;
    nMaxTotalQty = 0;
}

int CMineableSchedule::GetTotalPeriods() const
{
    if (nPerBlock <= 0)
        return 0;
    return static_cast<int>(nTotalQty / nPerBlock);
}

void CMineableSchedule::UpdateMaturity(int nHeight)
{
    if (!fActive || IsComplete())
        return;
    if (nHeight <= nStartHeight || nNthBlock <= 0)
        return;

    const int elapsed = nHeight - nStartHeight;
    const int shouldMature = std::min(GetTotalPeriods(), elapsed / nNthBlock);
    if (shouldMature > nMaturedPeriods)
        nMaturedPeriods = shouldMature;
}

CAmount CMineableSchedule::GetAccruedAmount() const
{
    const int delta = nMaturedPeriods - nClaimedPeriods;
    if (delta <= 0)
        return 0;
    CAmount remaining = nTotalQty - nTotalMinted;
    if (remaining <= 0)
        return 0;
    CAmount accrued = delta * nPerBlock;
    return std::min(accrued, remaining);
}

CAmount CMineableSchedule::GetAccruedFundAmount() const
{
    const int delta = nMaturedPeriods - nClaimedPeriods;
    if (delta <= 0 || nFundAmt <= 0)
        return 0;
    return delta * nFundAmt;
}

CAmount CMineableSchedule::GetAccruedMinerAmount() const
{
    CAmount total = GetAccruedAmount();
    CAmount fund = GetAccruedFundAmount();
    if (total <= fund)
        return 0;
    return total - fund;
}

bool CMineableSchedule::IsComplete() const
{
    return nTotalMinted >= nTotalQty || nClaimedPeriods >= GetTotalPeriods();
}

CIssueMineable::CIssueMineable()
{
    strRootAsset.clear();
    strMineableAsset.clear();
    nTotalQty = 0;
    nPerBlock = 0;
    nNthBlock = 10;
    nFundAmt = 0;
    nUnits = 0;
    nHasIPFS = 0;
    strIPFSHash.clear();
    fAllowExtension = false;
    nMaxTotalQty = 0;
}

CReissueMineableSchedule::CReissueMineableSchedule()
{
    strMineableAsset.clear();
    nAddQty = 0;
    nNewNthBlock = 0;
}

void CIssueMineable::ConstructTransaction(CScript& script) const
{
    CDataStream ssIssue(SER_NETWORK, PROTOCOL_VERSION);
    ssIssue << *this;

    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(RVN_R);
    vchMessage.push_back(RVN_V);
    vchMessage.push_back(RVN_N);
    vchMessage.push_back(RVN_M);
    vchMessage.insert(vchMessage.end(), ssIssue.begin(), ssIssue.end());
    script << OP_RVN_ASSET << ToByteVector(vchMessage) << OP_DROP;
}

void CReissueMineableSchedule::ConstructTransaction(CScript& script) const
{
    CDataStream ssReissue(SER_NETWORK, PROTOCOL_VERSION);
    ssReissue << *this;

    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(RVN_R);
    vchMessage.push_back(RVN_V);
    vchMessage.push_back(RVN_N);
    vchMessage.push_back(RVN_E);
    vchMessage.insert(vchMessage.end(), ssReissue.begin(), ssReissue.end());
    script << OP_RVN_ASSET << ToByteVector(vchMessage) << OP_DROP;
}

bool CReissueMineableSchedule::IsNull() const
{
    return strMineableAsset.empty() || (nAddQty <= 0 && nNewNthBlock <= 0);
}

bool CIssueMineable::IsNull() const
{
    return strRootAsset.empty() || nTotalQty <= 0 || nPerBlock <= 0;
}

bool IsMineableAssetName(const std::string& name)
{
    return name.size() >= 4 && name[0] == MINEABLE_CHAR;
}

std::string MineableAssetNameFromRoot(const std::string& rootAsset)
{
    return std::string(1, MINEABLE_CHAR) + rootAsset;
}

CAmount CalculateMineableIssuanceCost(const CIssueMineable& issue)
{
    if (issue.nPerBlock <= 0)
        return MIN_MINEABLE_ISSUANCE_COST;
    const int periods = static_cast<int>(issue.nTotalQty / issue.nPerBlock);
    CAmount cost = periods * MINEABLE_COST_PER_PERIOD;
    return std::max(MIN_MINEABLE_ISSUANCE_COST, cost);
}

void GetMineableIssuanceEstimate(const CIssueMineable& issue, CAmount& cost, int& days, int& months)
{
    cost = CalculateMineableIssuanceCost(issue);
    days = 0;
    months = 0;
    if (issue.nPerBlock <= 0 || issue.nNthBlock <= 0)
        return;
    const int periods = static_cast<int>(issue.nTotalQty / issue.nPerBlock);
    const int blocks = periods * issue.nNthBlock;
    days = blocks / (60 * 24);
    months = days / 30;
}

bool CheckIssueMineable(const CIssueMineable& issue, std::string& strError)
{
    strError.clear();
    if (issue.strRootAsset.empty()) {
        strError = "bad-txns-mineable-empty-root";
        return false;
    }
    if (issue.strMineableAsset != MineableAssetNameFromRoot(issue.strRootAsset)) {
        strError = "bad-txns-mineable-name-mismatch";
        return false;
    }
    if (issue.nTotalQty <= 0 || issue.nTotalQty > MAX_MONEY) {
        strError = "bad-txns-mineable-qty";
        return false;
    }
    if (issue.nPerBlock <= 0 || issue.nTotalQty % issue.nPerBlock != 0) {
        strError = "bad-txns-mineable-per-block";
        return false;
    }
    if (issue.nNthBlock < 1 || issue.nNthBlock > 10080) {
        strError = "bad-txns-mineable-nth-block";
        return false;
    }
    if (issue.nFundAmt < 0 || issue.nFundAmt > issue.nPerBlock) {
        strError = "bad-txns-mineable-fund-amt";
        return false;
    }
    if (issue.nUnits < MIN_UNIT || issue.nUnits > MAX_UNIT) {
        strError = "bad-txns-mineable-units";
        return false;
    }
    if (issue.nMaxTotalQty < 0 || issue.nMaxTotalQty > MAX_MONEY) {
        strError = "bad-txns-mineable-max-qty";
        return false;
    }
    if (issue.fAllowExtension) {
        const CAmount cap = issue.nMaxTotalQty > 0 ? issue.nMaxTotalQty : issue.nTotalQty;
        if (cap < issue.nTotalQty) {
            strError = "bad-txns-mineable-max-below-total";
            return false;
        }
    } else if (issue.nMaxTotalQty > 0 && issue.nMaxTotalQty != issue.nTotalQty) {
        strError = "bad-txns-mineable-max-without-extension";
        return false;
    }
    AssetType rootType;
    if (!IsAssetNameValid(issue.strRootAsset, rootType) || rootType != AssetType::ROOT) {
        strError = "bad-txns-mineable-root-invalid";
        return false;
    }
    return true;
}

bool IsNthBlockExtensionCompatible(int oldNth, int newNth)
{
    if (newNth <= 0 || newNth == oldNth)
        return true;
    if (newNth > oldNth)
        return false;
    return oldNth % newNth == 0;
}

CAmount CalculateMineableExtensionCost(const CMineableSchedule& schedule, CAmount nAddQty, int nNewNthBlock)
{
    CAmount cost = 0;
    if (nAddQty > 0) {
        if (schedule.nPerBlock <= 0)
            return 0;
        const int addPeriods = static_cast<int>(nAddQty / schedule.nPerBlock);
        cost += addPeriods * MINEABLE_COST_PER_PERIOD;
    }

    const int effectiveNewNth = nNewNthBlock > 0 ? nNewNthBlock : schedule.nNthBlock;
    if (effectiveNewNth != schedule.nNthBlock) {
        if (effectiveNewNth == 1 && schedule.nNthBlock > 1) {
            const int remaining = schedule.GetTotalPeriods() - schedule.nClaimedPeriods;
            if (remaining > 0)
                cost += static_cast<CAmount>(remaining) * (schedule.nNthBlock - 1) * MINEABLE_COST_PER_PERIOD;
        } else if (!IsNthBlockExtensionCompatible(schedule.nNthBlock, effectiveNewNth)) {
            return MAX_MONEY;
        }
    }
    return cost;
}

bool CheckReissueMineableSchedule(const CReissueMineableSchedule& reissue, std::string& strError)
{
    strError.clear();
    if (!IsMineableAssetName(reissue.strMineableAsset)) {
        strError = "bad-txns-mineable-reissue-name";
        return false;
    }
    if (reissue.nAddQty < 0 || reissue.nAddQty > MAX_MONEY) {
        strError = "bad-txns-mineable-reissue-qty";
        return false;
    }
    if (reissue.nAddQty == 0 && reissue.nNewNthBlock <= 0) {
        strError = "bad-txns-mineable-reissue-empty";
        return false;
    }
    if (reissue.nNewNthBlock < 0 || reissue.nNewNthBlock > 10080) {
        strError = "bad-txns-mineable-reissue-nth";
        return false;
    }
    return true;
}

bool ApplyScheduleExtension(CMineableSchedule& schedule, CAmount nAddQty, int nNewNthBlock, std::string& strError)
{
    strError.clear();
    if (nAddQty > 0) {
        if (schedule.nPerBlock <= 0 || nAddQty % schedule.nPerBlock != 0) {
            strError = "bad-txns-mineable-reissue-per-block";
            return false;
        }
        if (schedule.nTotalQty + nAddQty > schedule.nMaxTotalQty) {
            strError = "bad-txns-mineable-reissue-cap";
            return false;
        }
        schedule.nTotalQty += nAddQty;
    }

    if (nNewNthBlock > 0 && nNewNthBlock != schedule.nNthBlock) {
        if (nNewNthBlock > schedule.nNthBlock) {
            strError = "bad-txns-mineable-reissue-slower-nth";
            return false;
        }
        if (nNewNthBlock != 1 && schedule.nNthBlock % nNewNthBlock != 0) {
            strError = "bad-txns-mineable-reissue-nth-indivisible";
            return false;
        }
        schedule.nNthBlock = nNewNthBlock;
    }
    return true;
}

bool ContextualCheckReissueMineableSchedule(CAssetsCache* assetCache, CMineableAssetsDB& db,
                                            const CReissueMineableSchedule& reissue,
                                            const std::string& address, std::string& strError)
{
    if (!CheckReissueMineableSchedule(reissue, strError))
        return false;

    CMineableSchedule schedule;
    if (!db.ReadSchedule(reissue.strMineableAsset, schedule) || !schedule.fActive) {
        strError = "bad-txns-mineable-reissue-unknown";
        return false;
    }
    if (!schedule.fAllowExtension) {
        strError = "bad-txns-mineable-reissue-not-extendable";
        return false;
    }
    if (reissue.nAddQty > 0 && schedule.nPerBlock > 0 && reissue.nAddQty % schedule.nPerBlock != 0) {
        strError = "bad-txns-mineable-reissue-per-block";
        return false;
    }
    if (reissue.nAddQty > 0 && schedule.nTotalQty + reissue.nAddQty > schedule.nMaxTotalQty) {
        strError = "bad-txns-mineable-reissue-cap";
        return false;
    }
    const int effectiveNewNth = reissue.nNewNthBlock > 0 ? reissue.nNewNthBlock : schedule.nNthBlock;
    if (effectiveNewNth != schedule.nNthBlock) {
        if (effectiveNewNth > schedule.nNthBlock) {
            strError = "bad-txns-mineable-reissue-slower-nth";
            return false;
        }
        if (effectiveNewNth != 1 && schedule.nNthBlock % effectiveNewNth != 0) {
            strError = "bad-txns-mineable-reissue-nth-indivisible";
            return false;
        }
    }
    if (CalculateMineableExtensionCost(schedule, reissue.nAddQty, reissue.nNewNthBlock) >= MAX_MONEY) {
        strError = "bad-txns-mineable-reissue-nth-indivisible";
        return false;
    }

    if (assetCache && !address.empty()) {
        const std::string ownerName = schedule.strRootAsset + OWNER_TAG;
        const auto key = std::make_pair(ownerName, address);
        if (!assetCache->mapAssetsAddressAmount.count(key) ||
            assetCache->mapAssetsAddressAmount.at(key) < OWNER_ASSET_AMOUNT) {
            strError = "bad-txns-mineable-reissue-not-owner";
            return false;
        }
    }

    return true;
}

bool ContextualCheckIssueMineable(CAssetsCache* assetCache, const CIssueMineable& issue,
                                  const std::string& address, int nHeight, std::string& strError)
{
    if (!CheckIssueMineable(issue, strError))
        return false;

    if (!assetCache) {
        strError = "bad-txns-mineable-no-cache";
        return false;
    }

    CNewAsset root;
    if (!assetCache->GetAssetMetaDataIfExists(issue.strRootAsset, root)) {
        strError = "bad-txns-mineable-root-missing";
        return false;
    }

    (void)address;
    (void)nHeight;
    return true;
}

static bool GetOwnerTokenHolder(const std::string& rootAsset, std::string& holderAddress)
{
    if (!passetsdb)
        return false;
    std::vector<std::pair<std::string, CAmount>> holders;
    int total = 0;
    if (!passetsdb->AssetAddressDir(holders, total, true, rootAsset + OWNER_TAG, 0, 0))
        return false;
    if (holders.empty())
        return false;
    holderAddress = holders[0].first;
    return true;
}

bool RegisterMineableScheduleFromTx(const CTransaction& tx, int nHeight, const uint256& blockHash,
                                    CMineableAssetsDB& db, CAssetsCache* assetsCache)
{
    CIssueMineable issue;
    std::string address;
    if (!IssueMineableFromTransaction(tx, issue, address))
        return false;

    CMineableSchedule schedule;
    schedule.strRootAsset = issue.strRootAsset;
    schedule.strMineableAsset = issue.strMineableAsset;
    schedule.nTotalQty = issue.nTotalQty;
    schedule.nPerBlock = issue.nPerBlock;
    schedule.nNthBlock = issue.nNthBlock;
    schedule.nFundAmt = issue.nFundAmt;
    schedule.nUnits = issue.nUnits;
    schedule.nHasIPFS = issue.nHasIPFS;
    schedule.strIPFSHash = issue.strIPFSHash;
    schedule.nStartHeight = nHeight;
    schedule.nMaturedPeriods = 0;
    schedule.nClaimedPeriods = 0;
    schedule.nTotalMinted = 0;
    schedule.issuanceTxid = tx.GetHash();
    schedule.fActive = true;
    schedule.fAllowExtension = issue.fAllowExtension;
    schedule.nMaxTotalQty = issue.nMaxTotalQty > 0 ? issue.nMaxTotalQty : issue.nTotalQty;

    if (!assetsCache)
        return false;

    CNewAsset asset(issue.strMineableAsset, 0, issue.nUnits, 0, issue.nHasIPFS, issue.strIPFSHash);
    if (!assetsCache->CheckIfAssetExists(issue.strMineableAsset)) {
        if (!assetsCache->AddNewAsset(asset, address, nHeight, blockHash))
            return error("%s: failed to register mineable asset metadata", __func__);
    }

    return db.WriteSchedule(schedule);
}

bool ExtendMineableScheduleFromTx(const CTransaction& tx, int nHeight, CMineableAssetsDB& db)
{
    CReissueMineableSchedule reissue;
    std::string address;
    if (!ReissueMineableFromTransaction(tx, reissue, address))
        return false;

    CMineableSchedule schedule;
    if (!db.ReadSchedule(reissue.strMineableAsset, schedule) || !schedule.fActive)
        return false;

    std::string strError;
    if (!ContextualCheckReissueMineableSchedule(nullptr, db, reissue, address, strError))
        return false;

    if (!ApplyScheduleExtension(schedule, reissue.nAddQty, reissue.nNewNthBlock, strError))
        return false;

    (void)nHeight;
    return db.WriteSchedule(schedule);
}

static bool DeserializeIssueMineableFromScript(const CScript& script, CIssueMineable& issue, std::string& address)
{
    int nType = 0;
    bool fIsOwner = false;
    int nStartingIndex = 0;
    if (!script.IsAssetScript(nType, fIsOwner, nStartingIndex) || nType != TX_ISSUE_MINEABLE)
        return false;

    std::vector<unsigned char> vchAsset;
    vchAsset.insert(vchAsset.end(), script.begin() + nStartingIndex, script.end());
    CDataStream ssAsset(vchAsset, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssAsset >> issue;
    } catch (const std::exception&) {
        return false;
    }

    txnouttype type;
    std::vector<CTxDestination> vDest;
    int nRequired = 0;
    if (!ExtractDestinations(script, type, vDest, nRequired) || vDest.empty())
        return false;
    address = EncodeDestination(vDest[0]);
    return true;
}

bool IssueMineableFromScript(const CScript& script, CIssueMineable& issue, std::string& address)
{
    return DeserializeIssueMineableFromScript(script, issue, address);
}

bool IssueMineableFromTransaction(const CTransaction& tx, CIssueMineable& issue, std::string& address)
{
    for (const auto& out : tx.vout) {
        if (IsScriptIssueMineableAsset(out.scriptPubKey))
            return IssueMineableFromScript(out.scriptPubKey, issue, address);
    }
    return false;
}

static bool DeserializeReissueMineableFromScript(const CScript& script, CReissueMineableSchedule& reissue, std::string& address)
{
    int nType = 0;
    bool fIsOwner = false;
    int nStartingIndex = 0;
    if (!script.IsAssetScript(nType, fIsOwner, nStartingIndex) || nType != TX_REISSUE_MINEABLE)
        return false;

    std::vector<unsigned char> vchAsset;
    vchAsset.insert(vchAsset.end(), script.begin() + nStartingIndex, script.end());
    CDataStream ssAsset(vchAsset, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssAsset >> reissue;
    } catch (const std::exception&) {
        return false;
    }

    txnouttype type;
    std::vector<CTxDestination> vDest;
    int nRequired = 0;
    if (!ExtractDestinations(script, type, vDest, nRequired) || vDest.empty())
        return false;
    address = EncodeDestination(vDest[0]);
    return true;
}

bool ReissueMineableFromScript(const CScript& script, CReissueMineableSchedule& reissue, std::string& address)
{
    return DeserializeReissueMineableFromScript(script, reissue, address);
}

bool ReissueMineableFromTransaction(const CTransaction& tx, CReissueMineableSchedule& reissue, std::string& address)
{
    for (const auto& out : tx.vout) {
        if (IsScriptReissueMineableAsset(out.scriptPubKey))
            return ReissueMineableFromScript(out.scriptPubKey, reissue, address);
    }
    return false;
}

bool IsMineableCoinbaseAssetOutput(const CTxOut& out, CAssetTransfer& transfer, std::string& address)
{
    if (!IsMineableAssetsDeployed())
        return false;
    if (!TransferAssetFromScript(out.scriptPubKey, transfer, address))
        return false;
    return IsMineableAssetName(transfer.strName);
}

bool ProcessMineableMaturityAtHeight(int nHeight, CMineableAssetsDB& db)
{
    if (!IsMineableAssetsDeployed())
        return true;

    std::vector<CMineableSchedule> schedules;
    if (!db.ListSchedules(schedules))
        return false;

    for (auto& schedule : schedules) {
        schedule.UpdateMaturity(nHeight);
        if (!db.WriteSchedule(schedule))
            return false;
    }
    return true;
}

bool GetMineableAccrualForAsset(const std::string& assetName, int nHeight,
                                CMineableAssetsDB& db, CMineableAccrualInfo& info)
{
    CMineableSchedule schedule;
    if (!db.ReadSchedule(assetName, schedule) || !schedule.fActive)
        return false;

    schedule.UpdateMaturity(nHeight);
    info.matured_periods = schedule.nMaturedPeriods;
    info.claimed_periods = schedule.nClaimedPeriods;
    info.delta_periods = schedule.nMaturedPeriods - schedule.nClaimedPeriods;
    info.accrued_amount = schedule.GetAccruedAmount();
    info.accrued_fund = schedule.GetAccruedFundAmount();
    info.accrued_miner = schedule.GetAccruedMinerAmount();
    return true;
}

bool ValidateMineableCoinbaseClaims(const CTransaction& coinbase, int nHeight,
                                    CAssetsCache* assetCache, CMineableAssetsDB& db,
                                    CValidationState& state)
{
    if (!IsMineableAssetsDeployed())
        return true;
    if (!coinbase.IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "bad-mineable-not-coinbase");

    std::map<std::string, CAmount> minerByAsset;
    std::map<std::string, CAmount> fundByAsset;

    for (const auto& out : coinbase.vout) {
        CAssetTransfer transfer;
        std::string address;
        if (!IsMineableCoinbaseAssetOutput(out, transfer, address))
            continue;

        CMineableSchedule schedule;
        if (!db.ReadSchedule(transfer.strName, schedule) || !schedule.fActive) {
            return state.DoS(100, false, REJECT_INVALID, "bad-mineable-unknown-schedule");
        }

        schedule.UpdateMaturity(nHeight);
        const CAmount expectedFund = schedule.GetAccruedFundAmount();
        std::string ownerAddress;
        if (expectedFund > 0 && !GetOwnerTokenHolder(schedule.strRootAsset, ownerAddress)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-mineable-owner-address");
        }

        if (expectedFund > 0 && address == ownerAddress) {
            fundByAsset[transfer.strName] += transfer.nAmount;
        } else {
            if (minerByAsset.count(transfer.strName)) {
                return state.DoS(100, false, REJECT_INVALID, "bad-mineable-duplicate-claim");
            }
            minerByAsset[transfer.strName] = transfer.nAmount;
        }
    }

    for (const auto& entry : minerByAsset) {
        CMineableSchedule schedule;
        if (!db.ReadSchedule(entry.first, schedule))
            return state.DoS(100, false, REJECT_INVALID, "bad-mineable-unknown-schedule");
        schedule.UpdateMaturity(nHeight);

        const CAmount expectedTotal = schedule.GetAccruedAmount();
        const CAmount expectedMiner = schedule.GetAccruedMinerAmount();
        const CAmount expectedFund = schedule.GetAccruedFundAmount();

        if (expectedTotal <= 0) {
            return state.DoS(100, false, REJECT_INVALID, "bad-mineable-nothing-accrued");
        }
        if (entry.second != expectedMiner) {
            return state.DoS(100, false, REJECT_INVALID, "bad-mineable-claim-amount");
        }
        if (expectedFund > 0) {
            if (!fundByAsset.count(entry.first) || fundByAsset.at(entry.first) != expectedFund) {
                return state.DoS(100, false, REJECT_INVALID, "bad-mineable-fund-amount");
            }
        }
        (void)assetCache;
    }

    for (const auto& entry : fundByAsset) {
        if (!minerByAsset.count(entry.first)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-mineable-fund-without-miner");
        }
    }

    return true;
}

bool ApplyMineableCoinbaseClaims(const CTransaction& coinbase, int nHeight,
                                 CAssetsCache* assetCache, CMineableAssetsDB& db)
{
    if (!IsMineableAssetsDeployed())
        return true;

    std::set<std::string> claimed;

    for (const auto& out : coinbase.vout) {
        CAssetTransfer transfer;
        std::string address;
        if (!IsMineableCoinbaseAssetOutput(out, transfer, address))
            continue;
        if (claimed.count(transfer.strName))
            continue;

        CMineableSchedule schedule;
        if (!db.ReadSchedule(transfer.strName, schedule))
            return false;

        schedule.UpdateMaturity(nHeight);
        const CAmount accrued = schedule.GetAccruedAmount();
        const CAmount expectedMiner = schedule.GetAccruedMinerAmount();
        if (accrued <= 0)
            continue;

        CAmount claimedMiner = 0;
        for (const auto& claimOut : coinbase.vout) {
            CAssetTransfer t;
            std::string addr;
            if (!IsMineableCoinbaseAssetOutput(claimOut, t, addr))
                continue;
            if (t.strName != transfer.strName)
                continue;
            std::string ownerAddress;
            if (schedule.GetAccruedFundAmount() > 0 && GetOwnerTokenHolder(schedule.strRootAsset, ownerAddress) && addr == ownerAddress)
                continue;
            claimedMiner = t.nAmount;
            break;
        }
        if (claimedMiner != expectedMiner)
            continue;

        schedule.nClaimedPeriods = schedule.nMaturedPeriods;
        schedule.nTotalMinted += accrued;
        if (schedule.IsComplete())
            schedule.fActive = false;

        if (!db.WriteSchedule(schedule))
            return false;

        claimed.insert(transfer.strName);
        (void)assetCache;
    }

    return true;
}
