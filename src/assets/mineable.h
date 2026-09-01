// Copyright (c) 2026 The Ravencoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_MINEABLE_H
#define RAVENCOIN_MINEABLE_H

#include "amount.h"
#include "assets/assettypes.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "uint256.h"

#include <string>

class CMineableAssetsDB;

#define MINEABLE_CHAR '&'
#define MIN_MINEABLE_ISSUANCE_COST (500 * COIN)
#define MINEABLE_COST_PER_PERIOD (1 * COIN)

class CAssetsCache;
class CValidationState;

/** Schedule registered by issuemineable. */
class CMineableSchedule
{
public:
    std::string strRootAsset;
    std::string strMineableAsset;
    CAmount nTotalQty;
    CAmount nPerBlock;
    int nNthBlock;
    CAmount nFundAmt;
    int8_t nUnits;
    int8_t nHasIPFS;
    std::string strIPFSHash;
    int nStartHeight;
    int nMaturedPeriods;
    int nClaimedPeriods;
    CAmount nTotalMinted;
    uint256 issuanceTxid;
    bool fActive;
    /** When false, schedule qty and nth_block are fixed after issuance. */
    bool fAllowExtension;
    /** Hard cap on total mineable qty (>= nTotalQty). No mint beyond this. */
    CAmount nMaxTotalQty;

    CMineableSchedule();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(strRootAsset);
        READWRITE(strMineableAsset);
        READWRITE(nTotalQty);
        READWRITE(nPerBlock);
        READWRITE(nNthBlock);
        READWRITE(nFundAmt);
        READWRITE(nUnits);
        READWRITE(nHasIPFS);
        if (nHasIPFS == 1) {
            ReadWriteAssetHash(s, ser_action, strIPFSHash);
        } else if (ser_action.ForRead()) {
            strIPFSHash.clear();
        }
        READWRITE(nStartHeight);
        READWRITE(nMaturedPeriods);
        READWRITE(nClaimedPeriods);
        READWRITE(nTotalMinted);
        READWRITE(issuanceTxid);
        READWRITE(fActive);
        READWRITE(fAllowExtension);
        READWRITE(nMaxTotalQty);
    }

    int GetTotalPeriods() const;
    void UpdateMaturity(int nHeight);
    CAmount GetAccruedAmount() const;
    CAmount GetAccruedFundAmount() const;
    CAmount GetAccruedMinerAmount() const;
    bool IsComplete() const;
};

/** Payload in issuemineable transactions (rvnm script). */
class CIssueMineable
{
public:
    std::string strRootAsset;
    std::string strMineableAsset;
    CAmount nTotalQty;
    CAmount nPerBlock;
    int nNthBlock;
    CAmount nFundAmt;
    int8_t nUnits;
    int8_t nHasIPFS;
    std::string strIPFSHash;
    /** When true, root owner may extend schedule up to nMaxTotalQty. */
    bool fAllowExtension;
    /** Maximum total qty (0 = fixed at nTotalQty). */
    CAmount nMaxTotalQty;

    CIssueMineable();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(strRootAsset);
        READWRITE(strMineableAsset);
        READWRITE(nTotalQty);
        READWRITE(nPerBlock);
        READWRITE(nNthBlock);
        READWRITE(nFundAmt);
        READWRITE(nUnits);
        READWRITE(nHasIPFS);
        if (nHasIPFS == 1) {
            ReadWriteAssetHash(s, ser_action, strIPFSHash);
        } else if (ser_action.ForRead()) {
            strIPFSHash.clear();
        }
        READWRITE(fAllowExtension);
        READWRITE(nMaxTotalQty);
    }

    void ConstructTransaction(CScript& script) const;
    bool IsNull() const;
};

/** Additive schedule extension (reissuemineable). Only increases qty and/or nth frequency. */
class CReissueMineableSchedule
{
public:
    std::string strMineableAsset;
    CAmount nAddQty;
    /** 0 = keep current nth_block. Must divide current nth or be 1 with recost. */
    int nNewNthBlock;

    CReissueMineableSchedule();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(strMineableAsset);
        READWRITE(nAddQty);
        READWRITE(nNewNthBlock);
    }

    void ConstructTransaction(CScript& script) const;
    bool IsNull() const;
};

bool IsMineableAssetName(const std::string& name);
std::string MineableAssetNameFromRoot(const std::string& rootAsset);

CAmount CalculateMineableIssuanceCost(const CIssueMineable& issue);
void GetMineableIssuanceEstimate(const CIssueMineable& issue, CAmount& cost, int& days, int& months);

/** True when new_nth divides old_nth (faster issuance) or new_nth is 0 (unchanged). */
bool IsNthBlockExtensionCompatible(int oldNth, int newNth);
CAmount CalculateMineableExtensionCost(const CMineableSchedule& schedule, CAmount nAddQty, int nNewNthBlock);

bool CheckIssueMineable(const CIssueMineable& issue, std::string& strError);
bool ContextualCheckIssueMineable(CAssetsCache* assetCache, const CIssueMineable& issue,
                                  const std::string& address, int nHeight, std::string& strError);

bool IssueMineableFromScript(const CScript& script, CIssueMineable& issue, std::string& address);
bool IssueMineableFromTransaction(const CTransaction& tx, CIssueMineable& issue, std::string& address);

bool CheckReissueMineableSchedule(const CReissueMineableSchedule& reissue, std::string& strError);
bool ContextualCheckReissueMineableSchedule(CAssetsCache* assetCache, CMineableAssetsDB& db,
                                            const CReissueMineableSchedule& reissue,
                                            const std::string& address, std::string& strError);
bool ReissueMineableFromScript(const CScript& script, CReissueMineableSchedule& reissue, std::string& address);
bool ReissueMineableFromTransaction(const CTransaction& tx, CReissueMineableSchedule& reissue, std::string& address);
bool ApplyScheduleExtension(CMineableSchedule& schedule, CAmount nAddQty, int nNewNthBlock, std::string& strError);
bool ExtendMineableScheduleFromTx(const CTransaction& tx, int nHeight, CMineableAssetsDB& db);

bool IsMineableCoinbaseAssetOutput(const CTxOut& out, CAssetTransfer& transfer, std::string& address);

bool ProcessMineableMaturityAtHeight(int nHeight, CMineableAssetsDB& db);
bool ValidateMineableCoinbaseClaims(const CTransaction& coinbase, int nHeight,
                                    CAssetsCache* assetCache, CMineableAssetsDB& db,
                                    CValidationState& state);
bool ApplyMineableCoinbaseClaims(const CTransaction& coinbase, int nHeight,
                                 CAssetsCache* assetCache, CMineableAssetsDB& db);
bool RegisterMineableScheduleFromTx(const CTransaction& tx, int nHeight, const uint256& blockHash,
                                    CMineableAssetsDB& db, CAssetsCache* assetsCache);

struct CMineableAccrualInfo
{
    int matured_periods;
    int claimed_periods;
    int delta_periods;
    CAmount accrued_amount;
    CAmount accrued_miner;
    CAmount accrued_fund;
};

bool GetMineableAccrualForAsset(const std::string& assetName, int nHeight,
                                CMineableAssetsDB& db, CMineableAccrualInfo& info);

#endif // RAVENCOIN_MINEABLE_H
