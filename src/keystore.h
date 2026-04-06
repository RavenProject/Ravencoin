// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_KEYSTORE_H
#define RAVEN_KEYSTORE_H

#include "key.h"
#include "pqkey.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"

#include <boost/signals2/signal.hpp>

/** A virtual base class for key stores */
class CKeyStore
{
protected:
    mutable CCriticalSection cs_KeyStore;

public:
    virtual ~CKeyStore() {}

    //! Add a key to the store.
    virtual bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) =0;
    virtual bool AddKey(const CKey &key);

    //! Check whether a key corresponding to a given address is present in the store.
    virtual bool HaveKey(const CKeyID &address) const =0;
    virtual bool GetKey(const CKeyID &address, CKey& keyOut) const =0;
    virtual std::set<CKeyID> GetKeys() const =0;
    virtual bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const =0;

    //! RIP-25: Post-quantum key support
    virtual bool AddPQKeyPubKey(const CPQKey &key, const CPQPubKey &pubkey) =0;
    virtual bool HavePQKey(const uint256 &witnessProgram) const =0;
    virtual bool GetPQKey(const uint256 &witnessProgram, CPQKey &keyOut) const =0;
    virtual bool GetPQPubKey(const uint256 &witnessProgram, CPQPubKey &pubkeyOut) const =0;

    //! Support for BIP 0013 : see https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki
    virtual bool AddCScript(const CScript& redeemScript) =0;
    virtual bool HaveCScript(const CScriptID &hash) const =0;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const =0;

    //! Support for Watch-only addresses
    virtual bool AddWatchOnly(const CScript &dest) =0;
    virtual bool RemoveWatchOnly(const CScript &dest) =0;
    virtual bool HaveWatchOnly(const CScript &dest) const =0;
    virtual bool HaveWatchOnly() const =0;
};

typedef std::map<CKeyID, CKey> KeyMap;
typedef std::map<CKeyID, CPubKey> WatchKeyMap;
typedef std::map<CScriptID, CScript > ScriptMap;
typedef std::set<CScript> WatchOnlySet;

// RIP-25: PQ key maps keyed by witness program (SHA256 of ML-DSA pubkey)
typedef std::map<uint256, CPQKey> PQKeyMap;
typedef std::map<uint256, CPQPubKey> PQPubKeyMap;

/** Basic key store, that keeps keys in an address->secret map */
class CBasicKeyStore : public CKeyStore
{
protected:
    KeyMap mapKeys;
    WatchKeyMap mapWatchKeys;
    ScriptMap mapScripts;
    WatchOnlySet setWatchOnly;

    // RIP-25: PQ key storage
    PQKeyMap mapPQKeys;
    PQPubKeyMap mapPQPubKeys;

    uint256 nWordHash;
    std::vector<unsigned char> vchWords;
    std::vector<unsigned char> vchPassphrase;
    std::vector<unsigned char> g_vchSeed;

public:
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey) override;
    bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const override;
    bool HaveKey(const CKeyID &address) const override
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapKeys.count(address) > 0);
        }
        return result;
    }
    std::set<CKeyID> GetKeys() const override
    {
        LOCK(cs_KeyStore);
        std::set<CKeyID> set_address;
        for (const auto& mi : mapKeys) {
            set_address.insert(mi.first);
        }
        return set_address;
    }
    bool GetKey(const CKeyID &address, CKey &keyOut) const override
    {
        {
            LOCK(cs_KeyStore);
            KeyMap::const_iterator mi = mapKeys.find(address);
            if (mi != mapKeys.end())
            {
                keyOut = mi->second;
                return true;
            }
        }
        return false;
    }
    // RIP-25: PQ key methods
    bool AddPQKeyPubKey(const CPQKey &key, const CPQPubKey &pubkey) override
    {
        LOCK(cs_KeyStore);
        uint256 wp = pubkey.GetWitnessProgram();
        mapPQKeys[wp] = key;
        mapPQPubKeys[wp] = pubkey;
        return true;
    }
    bool HavePQKey(const uint256 &witnessProgram) const override
    {
        LOCK(cs_KeyStore);
        return mapPQKeys.count(witnessProgram) > 0;
    }
    bool GetPQKey(const uint256 &witnessProgram, CPQKey &keyOut) const override
    {
        LOCK(cs_KeyStore);
        auto mi = mapPQKeys.find(witnessProgram);
        if (mi != mapPQKeys.end()) {
            keyOut = mi->second;
            return true;
        }
        return false;
    }
    bool GetPQPubKey(const uint256 &witnessProgram, CPQPubKey &pubkeyOut) const override
    {
        LOCK(cs_KeyStore);
        auto mi = mapPQPubKeys.find(witnessProgram);
        if (mi != mapPQPubKeys.end()) {
            pubkeyOut = mi->second;
            return true;
        }
        return false;
    }

    bool AddCScript(const CScript& redeemScript) override;
    bool HaveCScript(const CScriptID &hash) const override;
    bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const override;

    bool AddWatchOnly(const CScript &dest) override;
    bool RemoveWatchOnly(const CScript &dest) override;
    bool HaveWatchOnly(const CScript &dest) const override;
    bool HaveWatchOnly() const override;

    bool AddWords(const uint256& p_hash, const std::vector<unsigned char>& p_vchWords);
    bool AddPassphrase(const std::vector<unsigned char>& p_vchPassphrase);
    bool AddVchSeed(const std::vector<unsigned char>& p_vchSeed);
    void GetBip39Data(uint256& p_hash, std::vector<unsigned char>& p_vchWords, std::vector<unsigned char>& p_vchPassphrase, std::vector<unsigned char>& p_vchSeed);
};

typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;
typedef std::map<CKeyID, std::pair<CPubKey, std::vector<unsigned char> > > CryptedKeyMap;
typedef std::map<uint256, std::pair<CPQPubKey, std::vector<unsigned char> > > CryptedPQKeyMap;

#endif // RAVEN_KEYSTORE_H
