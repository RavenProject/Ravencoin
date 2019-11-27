// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef RAVENCOIN_RESTRICTEDDB_H
#define RAVENCOIN_RESTRICTEDDB_H

#include <dbwrapper.h>

class CRestrictedDB  : public CDBWrapper {

public:
    explicit CRestrictedDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CRestrictedDB(const CRestrictedDB&) = delete;
    CRestrictedDB& operator=(const CRestrictedDB&) = delete;

    // Database of restricted asset verifier strings
    bool WriteVerifier(const std::string& assetName, const std::string& verifier);
    bool ReadVerifier(const std::string& assetName, std::string& verifier);
    bool EraseVerifier(const std::string& assetName);

    // Database of Addresses and the Tag that are assigned to them
    bool WriteAddressQualifier(const std::string &address, const std::string &tag);
    bool ReadAddressQualifier(const std::string &address, const std::string &tag);
    bool EraseAddressQualifier(const std::string &address, const std::string &tag);

    // Database of the Qualifier to the address that are assigned to them
    bool WriteQualifierAddress(const std::string &address, const std::string &tag);
    bool ReadQualifierAddress(const std::string &address, const std::string &tag);
    bool EraseQualifierAddress(const std::string &address, const std::string &tag);

    // Database of Blacklist addresses
    bool WriteRestrictedAddress(const std::string& address, const std::string& assetName);
    bool ReadRestrictedAddress(const std::string& address, const std::string& assetName);
    bool EraseRestrictedAddress(const std::string& address, const std::string& assetName);

    // Database of Restricted Trading Global Off
    bool WriteGlobalRestriction(const std::string& assetName);
    bool ReadGlobalRestriction(const std::string& assetName);
    bool EraseGlobalRestriction(const std::string& assetName);

    // Write / Read Database flags
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);

    bool GetQualifierAddresses(std::string& qualifier, std::vector<std::string>& addresses);
    bool GetAddressQualifiers(std::string& address, std::vector<std::string>& qualifiers);
    bool GetAddressRestrictions(std::string& address, std::vector<std::string>& restrictions);
    bool GetGlobalRestrictions(std::vector<std::string>& restrictions);

    bool CheckForAddressRootQualifier(const std::string& address, const std::string& qualifier);

    bool Flush();
};


#endif //RAVENCOIN_RESTRICTEDDB_H
