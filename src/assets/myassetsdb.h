// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_MYASSETSDB_H
#define RAVENCOIN_MYASSETSDB_H

#include <dbwrapper.h>

class CMessage;
class COutPoint;

class CMessageDB  : public CDBWrapper {

public:
    explicit CMessageDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CMessageDB(const CMessageDB&) = delete;
    CMessageDB& operator=(const CMessageDB&) = delete;

    // Database of messages
    bool WriteMessage(const CMessage& message);
    bool ReadMessage(const COutPoint& out, CMessage& message);
    bool EraseMessage(const COutPoint& out);
    bool LoadMessages(std::set<CMessage>& setMessages);
    bool EraseAllMessages(int& count);

    // Write / Read Database flags
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);

    bool Flush();
};

class CMessageChannelDB  : public CDBWrapper {
public:
    explicit CMessageChannelDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CMessageChannelDB(const CMessageChannelDB&) = delete;
    CMessageChannelDB& operator=(const CMessageChannelDB&) = delete;

    // My message channels
    bool WriteMyMessageChannel(const std::string& channelname);
    bool ReadMyMessageChannel(const std::string& channelname);
    bool EraseMyMessageChannel(const std::string& channelname);
    bool LoadMyMessageChannels(std::set<std::string>& setChannels);

    bool WriteUsedAddress(const std::string& address);
    bool ReadUsedAddress(const std::string& address);
    bool EraseUsedAddress(const std::string& address);

    // Write / Read Database flags
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);

    bool Flush();
};

class CMyRestrictedDB : public CDBWrapper {
public:
    explicit CMyRestrictedDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CMyRestrictedDB(const CMyRestrictedDB&) = delete;
    CMyRestrictedDB& operator=(const CMyRestrictedDB&) = delete;

    bool WriteTaggedAddress(const std::string& address, const std::string& tag_name, const bool fAdd, const uint32_t& nHeight);
    bool ReadTaggedAddress(const std::string& address, const std::string& tag_name, bool& fAdd, uint32_t& nHeight);
    bool EraseTaggedAddress(const std::string& address, const std::string& tag_name);
    bool LoadMyTaggedAddresses(std::vector<std::tuple<std::string, std::string, bool, uint32_t> >& vecTaggedAddresses);

    bool WriteRestrictedAddress(const std::string& address, const std::string& tag_name, const bool fAdd, const uint32_t& nHeight);
    bool ReadRestrictedAddress(const std::string& address, const std::string& tag_name, bool& fAdd, uint32_t& nHeight);
    bool EraseRestrictedAddress(const std::string& address, const std::string& tag_name);
    bool LoadMyRestrictedAddresses(std::vector<std::tuple<std::string, std::string, bool, uint32_t> >& vecRestrictedAddresses);

    // Write / Read Database flags
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
};


#endif //RAVENCOIN_MYASSETSDB_H
