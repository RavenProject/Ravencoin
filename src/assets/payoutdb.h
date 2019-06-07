// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PAYOUTDB_H
#define PAYOUTDB_H

#include <set>

#include <dbwrapper.h>

#include "assetsnapshotdb.h"
#include "rewardrequestdb.h"
#include "amount.h"

class CPayoutDBEntry
{
public:
    std::string assetName;
    std::string srcAssetName;
    std::set<std::pair<std::string, CAmount>> ownersAndPayouts;

    CPayoutDBEntry();
    CPayoutDBEntry(
        const std::string & p_assetName,
        const std::string & p_srcAssetName,
        const std::set<std::pair<std::string, CAmount>> & p_ownersAndPayouts
    );

    void SetNull()
    {
        assetName = "";
        srcAssetName = "";
        ownersAndPayouts.clear();
    }

    bool operator<(const CPayoutDBEntry &rhs) const
    {
        return assetName < rhs.assetName;
    }

    // Serialization methods
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(assetName);
        READWRITE(srcAssetName);
        READWRITE(ownersAndPayouts);
    }
};

class CPayoutDB  : public CDBWrapper {
public:
    explicit CPayoutDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CPayoutDB(const CPayoutDB &) = delete;
    CPayoutDB& operator=(const CPayoutDB &) = delete;

    //  Add payout entries for the specified reward request
    bool GeneratePayouts(
        const CRewardRequestDBEntry & p_rewardReq,
        const CAssetSnapshotDBEntry & p_assetSnapshot);

    //  Retrieve payout entries for the specified asset
    bool RetrievePayouts(
        const std::string & p_assetName,
        std::set<CPayoutDBEntry> & p_payoutEntries);

    bool Flush();
};


#endif //PAYOUTDB_H
