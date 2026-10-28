// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ravenconsensus.h"

#include "consensus/rip25.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/interpreter.h"
#include "version.h"

namespace {

/** A class that deserializes a single CTransaction one time. */
class TxInputStream
{
public:
    TxInputStream(int nTypeIn, int nVersionIn, const unsigned char *txTo, size_t txToLen) :
    m_type(nTypeIn),
    m_version(nVersionIn),
    m_data(txTo),
    m_remaining(txToLen)
    {}

    void read(char* pch, size_t nSize)
    {
        if (nSize > m_remaining)
            throw std::ios_base::failure(std::string(__func__) + ": end of data");

        if (pch == nullptr)
            throw std::ios_base::failure(std::string(__func__) + ": bad destination buffer");

        if (m_data == nullptr)
            throw std::ios_base::failure(std::string(__func__) + ": bad source buffer");

        memcpy(pch, m_data, nSize);
        m_remaining -= nSize;
        m_data += nSize;
    }

    template<typename T>
    TxInputStream& operator>>(T& obj)
    {
        ::Unserialize(*this, obj);
        return *this;
    }

    int GetVersion() const { return m_version; }
    int GetType() const { return m_type; }
private:
    const int m_type;
    const int m_version;
    const unsigned char* m_data;
    size_t m_remaining;
};

inline int set_error(ravenconsensus_error* ret, ravenconsensus_error serror)
{
    if (ret)
        *ret = serror;
    return 0;
}

struct ECCryptoClosure
{
    ECCVerifyHandle handle;
};

ECCryptoClosure instance_of_eccryptoclosure;

bool HasWitnessV2Program(const CScript& script)
{
    int version = -1;
    std::vector<unsigned char> program;
    return script.IsWitnessProgram(version, program) && version == 2;
}

bool IsPQPrevout(const CScript& scriptPubKey, const CScript& scriptSig)
{
    if (HasWitnessV2Program(scriptPubKey))
        return true;
    if (!scriptPubKey.IsPayToScriptHash())
        return false;

    // With P2SH, the final push is the redeem script. Reject witness-v2 here
    // before the legacy interface can treat it as an unknown witness version.
    CScript::const_iterator pc = scriptSig.begin();
    opcodetype opcode = OP_INVALIDOPCODE;
    std::vector<unsigned char> pushed;
    while (pc != scriptSig.end()) {
        if (!scriptSig.GetOp(pc, opcode, pushed))
            return false;
    }
    return opcode <= OP_PUSHDATA4 &&
           HasWitnessV2Program(CScript(pushed.begin(), pushed.end()));
}

bool NetworkContext(unsigned int network, Consensus::PQSignatureContext& context)
{
    static const char main[] = "RVN/ML-DSA-44/v1/0000006b444bc2f2ffe627be9d9e7e7a0730000870ef6eb6da46c8eae389df90";
    static const char test[] = "RVN/ML-DSA-44/v1/000000ecfc5e6324a079542221d00e10362bdc894d56500c414060eea8a3ad5a";
    static const char regtest[] = "RVN/ML-DSA-44/v1/0b2c703dc93bb63a36c4e33b85be4855ddbca2ac951a7a0a29b8de0408200a3c";
    static_assert(sizeof(main) == Consensus::PQ_SIGNATURE_CONTEXT_BYTES + 1, "invalid mainnet RIP-25 context length");
    static_assert(sizeof(test) == Consensus::PQ_SIGNATURE_CONTEXT_BYTES + 1, "invalid testnet RIP-25 context length");
    static_assert(sizeof(regtest) == Consensus::PQ_SIGNATURE_CONTEXT_BYTES + 1, "invalid regtest RIP-25 context length");

    const char* bytes = nullptr;
    switch (network) {
    case ravenconsensus_NETWORK_MAIN: bytes = main; break;
    case ravenconsensus_NETWORK_TEST: bytes = test; break;
    case ravenconsensus_NETWORK_REGTEST: bytes = regtest; break;
    default: return false;
    }
    for (size_t i = 0; i < context.size(); ++i)
        context[i] = static_cast<unsigned char>(bytes[i]);
    return Consensus::IsValidPQSignatureContext(context);
}
} // namespace

/** Check that all specified flags are part of the libconsensus interface. */
static bool verify_flags(unsigned int flags)
{
    if ((flags & ~(ravenconsensus_SCRIPT_FLAGS_VERIFY_ALL)) != 0)
        return false;
    if ((flags & ravenconsensus_SCRIPT_FLAGS_VERIFY_WITNESS) &&
        !(flags & ravenconsensus_SCRIPT_FLAGS_VERIFY_P2SH))
        return false;
    if ((flags & ravenconsensus_SCRIPT_FLAGS_VERIFY_PQ_HYBRID) &&
        !(flags & ravenconsensus_SCRIPT_FLAGS_VERIFY_WITNESS))
        return false;
    return true;
}

static int verify_script(const unsigned char *scriptPubKey, unsigned int scriptPubKeyLen, CAmount amount,
                                    const unsigned char *txTo        , unsigned int txToLen,
                                    unsigned int nIn, unsigned int flags,
                                    const Consensus::PQSignatureContext& pqSignatureContext,
                                    bool legacyInterface, ravenconsensus_error* err)
{
    if (!verify_flags(flags)) {
        return set_error(err, ravenconsensus_ERR_INVALID_FLAGS);
    }
    try {
        TxInputStream stream(SER_NETWORK, PROTOCOL_VERSION, txTo, txToLen);
        CTransaction tx(deserialize, stream);
        if (nIn >= tx.vin.size())
            return set_error(err, ravenconsensus_ERR_TX_INDEX);
        if (GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) != txToLen)
            return set_error(err, ravenconsensus_ERR_TX_SIZE_MISMATCH);

        // Regardless of the verification result, the tx did not error.
        set_error(err, ravenconsensus_ERR_OK);

        const CScript prevout(scriptPubKey, scriptPubKey + scriptPubKeyLen);
        if (legacyInterface && IsPQPrevout(prevout, tx.vin[nIn].scriptSig))
            return set_error(err, ravenconsensus_ERR_PQ_CONTEXT_REQUIRED);

        PrecomputedTransactionData txdata(tx);
        return VerifyScript(tx.vin[nIn].scriptSig, prevout,
                            &tx.vin[nIn].scriptWitness, flags,
                            TransactionSignatureChecker(&tx, nIn, amount, txdata,
                                                        pqSignatureContext), nullptr);
    } catch (const std::exception&) {
        return set_error(err, ravenconsensus_ERR_TX_DESERIALIZE); // Error deserializing
    }
}

int ravenconsensus_verify_script_with_amount(const unsigned char *scriptPubKey, unsigned int scriptPubKeyLen, int64_t amount,
                                    const unsigned char *txTo        , unsigned int txToLen,
                                    unsigned int nIn, unsigned int flags, ravenconsensus_error* err)
{
    CAmount am(amount);
    return ::verify_script(scriptPubKey, scriptPubKeyLen, am, txTo, txToLen, nIn, flags,
                           Consensus::NullPQSignatureContext(), true, err);
}

int ravenconsensus_verify_script_with_amount_and_network(const unsigned char *scriptPubKey, unsigned int scriptPubKeyLen, int64_t amount,
                                    const unsigned char *txTo        , unsigned int txToLen,
                                    unsigned int nIn, unsigned int flags, unsigned int network, ravenconsensus_error* err)
{
    Consensus::PQSignatureContext context{};
    if (!NetworkContext(network, context))
        return set_error(err, ravenconsensus_ERR_INVALID_NETWORK);
    return ::verify_script(scriptPubKey, scriptPubKeyLen, CAmount(amount), txTo,
                           txToLen, nIn, flags, context, false, err);
}


int ravenconsensus_verify_script(const unsigned char *scriptPubKey, unsigned int scriptPubKeyLen,
                                   const unsigned char *txTo        , unsigned int txToLen,
                                   unsigned int nIn, unsigned int flags, ravenconsensus_error* err)
{
    if (!verify_flags(flags)) {
        return set_error(err, ravenconsensus_ERR_INVALID_FLAGS);
    }
    if (flags & ravenconsensus_SCRIPT_FLAGS_VERIFY_WITNESS) {
        return set_error(err, ravenconsensus_ERR_AMOUNT_REQUIRED);
    }

    CAmount am(0);
    return ::verify_script(scriptPubKey, scriptPubKeyLen, am, txTo, txToLen, nIn, flags,
                           Consensus::NullPQSignatureContext(), true, err);
}

unsigned int ravenconsensus_version()
{
    // Just use the API version for now
    return RAVENCONSENSUS_API_VER;
}
