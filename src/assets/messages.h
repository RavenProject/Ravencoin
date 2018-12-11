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

enum class MessageStatus {
    READ = 0,
    UNREAD = 1,
    EXPIRED = 2,
    SPAM = 2,
    HIDDEN = 3,
    ERROR = 4
};

int IntFromMessageStatus(MessageStatus status);
MessageStatus MessageStatusFromInt(int nStatus);



class CMessage {
public:

    COutPoint out;
    std::string strName;
    std::string ipfsHash;
    int64_t time;
    int64_t nExpiredTime;
    MessageStatus status;

    CMessage() {
        SetNull();
    }

    void SetNull(){
        nExpiredTime = 0;
        out = COutPoint();
        strName = "";
        ipfsHash = "";
        time = 0;
        status = MessageStatus::ERROR;
    }

    std::string   ToString() {
        return strprintf("CMessage(%s, Name=%s, Message=%s, Expires=%u, Time=%u)", out.ToString(), strName, EncodeIPFS(ipfsHash), nExpiredTime, time);
    }

    CMessage(const COutPoint& out, const std::string& strName, const std::string& ipfsHash, const int64_t& nExpiredTime, const int64_t& time);

    bool operator<(const CMessage& rhs) const
    {
        return out < rhs.out;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(out);
        READWRITE(strName);
        READWRITE(ipfsHash);
        READWRITE(time);
        READWRITE(nExpiredTime);
        if (ser_action.ForRead()) {
            ::Serialize(s, IntFromMessageStatus(status));
        } else {
            int nStatus;
            ::Unserialize(s, nStatus);
            status = MessageStatusFromInt(nStatus);
        }
    }
};


#endif //RAVENCOIN_MESSAGES_H
