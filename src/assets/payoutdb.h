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

class CPayment
{
public:
    std::string address;
    CAmount payoutAmt;
    bool completed;

    CPayment();
    CPayment(
        const std::string & p_address,
        const CAmount & p_payoutAmt,
        bool p_completed
    );

    void SetNull()
    {
        address = "";
        payoutAmt = 0;
        completed = false;
    }

    bool operator<(const CPayment &rhs) const
    {
        return address < rhs.address;
    }

    // Serialization methods
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(address);
        READWRITE(payoutAmt);
        READWRITE(completed);
    }
};

class CPayoutDBEntry
{
public:
    std::string rewardID;
    std::string assetName;
    std::string srcAssetName;
    std::set<CPayment> payments;

    CPayoutDBEntry();
    CPayoutDBEntry(
        const std::string & p_rewardID,
        const std::string & p_assetName,
        const std::string & p_srcAssetName,
        const std::set<CPayment> & p_payments
    );

    void SetNull()
    {
        rewardID = "";
        assetName = "";
        srcAssetName = "";
        payments.clear();
    }

    bool operator<(const CPayoutDBEntry &rhs) const
    {
        return rewardID < rhs.rewardID;
    }

    // Serialization methods
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(rewardID);
        READWRITE(assetName);
        READWRITE(srcAssetName);
        READWRITE(payments);
    }
};

class CPayoutDB  : public CDBWrapper {
public:
    explicit CPayoutDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CPayoutDB(const CPayoutDB &) = delete;
    CPayoutDB& operator=(const CPayoutDB &) = delete;

    //  Add payout entries for the specified reward request
    bool GeneratePayouts(
        const CRewardRequest & p_rewardReq,
        const CAssetSnapshotDBEntry & p_assetSnapshot,
        CPayoutDBEntry & p_payoutEntry);

    //  Retrieve payout entry for the specified reward
    bool RetrievePayoutEntry(
        const std::string & p_rewardID,
        CPayoutDBEntry & p_payoutEntry);

    //  Remove the payout entry for the specified reward
    bool RemovePayoutEntry(const std::string & p_rewardID);

    //  Save the specified entry back to the database, including any modifications
    bool UpdatePayoutEntry(const CPayoutDBEntry & p_payoutEntry);

    bool Flush();
};


#endif //PAYOUTDB_H
