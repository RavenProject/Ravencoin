// Copyright (c) 2018 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef RAVENCOIN_MESSAGES_H
#define RAVENCOIN_MESSAGES_H


#include <uint256.h>
#include <serialize.h>
#include <primitives/transaction.h>
#include "assettypes.h"

class CMessage;

extern std::set<COutPoint> setMyMessageOutPoints;

class CMessage {
public:

    COutPoint out;
    std::string strName;
    std::string ipfsHash;
    int64_t time;
    bool fExpired;

    CMessage() {
        SetNull();
    }

    void SetNull(){
        fExpired = false;
        out = COutPoint();
        strName = "";
        ipfsHash = "";
        time = 0;
    }

    CMessage(const COutPoint& out, const std::string& strName, const std::string& ipfsHash, const bool fExpired, const int64_t& time);

    bool operator<(const CMessage& rhs) const
    {
        return out< rhs.out;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(out);
        READWRITE(strName);
        READWRITE(ipfsHash);
        READWRITE(time);
        READWRITE(fExpired);
    }
};

void GetMyMessages();


#endif //RAVENCOIN_MESSAGES_H
