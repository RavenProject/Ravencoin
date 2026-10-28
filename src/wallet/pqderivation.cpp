// Copyright (c) 2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/pqderivation.h"

#include "crypto/common.h"
#include "crypto/sha256.h"
#include "key.h"
#include "support/cleanse.h"
#include "tinyformat.h"

namespace pqderivation {
namespace {

static const unsigned char KEYGEN_DOMAIN[] = "RVN/ML-DSA-44/keygen/v1";
static const unsigned char LINEAGE_DOMAIN[] = "RVN/ML-DSA-44/lineage/v1";

bool DeriveHardened(const CExtKey& parent, uint32_t child, CExtKey& out)
{
    if (child >= HARDENED_LIMIT)
        return false;
    return parent.Derive(out, child | HARDENED_LIMIT);
}

} // namespace

bool DeriveSeed(const unsigned char* walletSeed, size_t walletSeedLen,
                uint32_t coinType, uint32_t index, SecureVector& pqSeedOut)
{
    SecureVector().swap(pqSeedOut);
    if (!walletSeed ||
        (walletSeedLen != LEGACY_SEED_BYTES && walletSeedLen != BIP39_SEED_BYTES) ||
        coinType >= HARDENED_LIMIT || index >= HARDENED_LIMIT) {
        return false;
    }

    CExtKey master;
    CExtKey purpose;
    CExtKey network;
    CExtKey account;
    CExtKey receive;
    CExtKey leaf;
    struct ChainCodeCleaner
    {
        CExtKey* keys[6];
        ~ChainCodeCleaner()
        {
            for (CExtKey* key : keys)
                key->chaincode.SetNull();
        }
    } chainCodeCleaner{{&master, &purpose, &network, &account, &receive, &leaf}};
    master.SetSeed(walletSeed, walletSeedLen);
    if (!master.key.IsValid())
        return false;

    if (!DeriveHardened(master, PURPOSE, purpose) ||
        !DeriveHardened(purpose, coinType, network) ||
        !DeriveHardened(network, ACCOUNT, account) ||
        !DeriveHardened(account, RECEIVE_BRANCH, receive) ||
        !DeriveHardened(receive, index, leaf) || leaf.key.size() != PQ_SEED_BYTES) {
        return false;
    }

    pqSeedOut.assign(PQ_SEED_BYTES, 0);
    CSHA256 hasher;
    hasher.Write(KEYGEN_DOMAIN, sizeof(KEYGEN_DOMAIN) - 1)
          .Write(leaf.key.begin(), leaf.key.size())
          .Finalize(pqSeedOut.data());
    memory_cleanse(&hasher, sizeof(hasher));
    return true;
}

bool GetLineageId(const unsigned char* walletSeed, size_t walletSeedLen,
                  uint8_t seedSource, uint32_t coinType,
                  uint256& lineageIdOut)
{
    lineageIdOut.SetNull();
    const bool validSourceAndSize =
        (seedSource == SEED_SOURCE_LEGACY_HD &&
         walletSeedLen == LEGACY_SEED_BYTES) ||
        (seedSource == SEED_SOURCE_BIP39 &&
         walletSeedLen == BIP39_SEED_BYTES);
    if (!walletSeed || !validSourceAndSize || coinType >= HARDENED_LIMIT)
        return false;

    unsigned char encodedCoinType[4];
    WriteBE32(encodedCoinType, coinType);
    CSHA256 hasher;
    hasher.Write(LINEAGE_DOMAIN, sizeof(LINEAGE_DOMAIN) - 1)
          .Write(&seedSource, 1)
          .Write(encodedCoinType, sizeof(encodedCoinType))
          .Write(walletSeed, walletSeedLen)
          .Finalize(lineageIdOut.begin());
    memory_cleanse(&hasher, sizeof(hasher));
    return true;
}

std::string GetKeypath(uint32_t coinType, uint32_t index)
{
    if (coinType >= HARDENED_LIMIT || index >= HARDENED_LIMIT)
        return std::string();
    return strprintf("m/25'/%u'/0'/0'/%u'", coinType, index);
}

} // namespace pqderivation
