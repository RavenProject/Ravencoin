// Copyright (c) 2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Regression tests for the KAWPOW header validation shortcut.
//
// CheckBlockHeader lets a KAWPOW header below the last known checkpoint prove its work with the
// supplied mix_hash alone, without recomputing ProgPoW. In August 2026 that shortcut was abused
// on mainnet by headers that lied about their height. These tests forge headers the same way
// (grinding only the final hash over a made-up mix_hash) and check every route into the shortcut.

#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "consensus/validation.h"
#include "pow.h"
#include "validation.h"
#include "versionbits.h"
#include "test/test_raven.h"

#include <boost/test/unit_test.hpp>

namespace {

struct KawpowHeaderSetup : public TestingSetup {
    uint32_t nSavedKawpowActivationTime;
    bool fSavedCheckpointsEnabled;

    KawpowHeaderSetup() : TestingSetup(CBaseChainParams::REGTEST)
    {
        nSavedKawpowActivationTime = nKAWPOWActivationTime;
        fSavedCheckpointsEnabled = fCheckpointsEnabled;
        // Other suites may leave checkpoints disabled; these tests need the default.
        fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
        // Regtest keeps KAWPOW inactive. Switch it on for every block after genesis.
        LOCK(cs_main);
        nKAWPOWActivationTime = chainActive.Tip()->nTime + 1;
    }

    ~KawpowHeaderSetup()
    {
        nKAWPOWActivationTime = nSavedKawpowActivationTime;
        fCheckpointsEnabled = fSavedCheckpointsEnabled;
    }
};

const Consensus::Params& ConsensusParams()
{
    return GetParams().GetConsensus();
}

void AddCheckpoint(const CBlockIndex* pindex)
{
    const_cast<CCheckpointData&>(GetParams().Checkpoints()).mapCheckpoints[pindex->nHeight] = pindex->GetBlockHash();
}

void SetHeaderCheckActivation(int nHeight)
{
    const_cast<Consensus::Params&>(GetParams().GetConsensus()).nHeightHeaderCheckActivation = nHeight;
}

CBlockHeader NewHeader(const CBlockIndex* pprev, uint32_t nDeclaredHeight)
{
    static uint32_t nSalt = 0;
    CBlockHeader header;
    header.nVersion = VERSIONBITS_TOP_BITS_ASSETS;
    header.hashPrevBlock = pprev->GetBlockHash();
    header.hashMerkleRoot = ArithToUint256(arith_uint256(++nSalt)); // keeps sibling headers distinct
    header.nTime = pprev->nTime + 60;
    header.nHeight = nDeclaredHeight;
    header.nNonce = 0;
    header.nNonce64 = 0;
    header.nBits = GetNextWorkRequired(pprev, &header, ConsensusParams());
    return header;
}

// Real ProgPoW work for the declared height.
void MineHonest(CBlockHeader& header)
{
    uint256 mix_hash;
    while (!CheckProofOfWork(header.GetHashFull(mix_hash), header.nBits, ConsensusParams()))
        ++header.nNonce64;
    header.mix_hash = mix_hash;
}

// No ProgPoW work at all: grind only the final hash over an invented mix_hash. Such a header
// satisfies the mix_hash-only check and nothing else.
void MineForged(CBlockHeader& header)
{
    header.mix_hash = uint256S("0x1111111111111111111111111111111111111111111111111111111111111111");
    while (!CheckProofOfWork(header.GetHash(), header.nBits, ConsensusParams()))
        ++header.nNonce64;
    uint256 real_mix_hash;
    header.GetHashFull(real_mix_hash);
    BOOST_REQUIRE(real_mix_hash != header.mix_hash);
}

bool AcceptHeader(const CBlockHeader& header, std::string& reason)
{
    CValidationState state;
    const CBlockIndex* pindex = nullptr;
    bool fAccepted = ProcessNewBlockHeaders({header}, state, GetParams(), &pindex);
    reason = state.GetRejectReason();
    return fAccepted;
}

// Genesis plus nBlocks honestly mined KAWPOW headers; element i is at height i.
std::vector<const CBlockIndex*> BuildHonestChain(int nBlocks)
{
    std::vector<const CBlockIndex*> chain;
    {
        LOCK(cs_main);
        chain.push_back(chainActive.Tip());
    }
    for (int i = 1; i <= nBlocks; ++i) {
        CBlockHeader header = NewHeader(chain.back(), i);
        MineHonest(header);
        std::string reason;
        BOOST_REQUIRE_MESSAGE(AcceptHeader(header, reason), "honest header rejected: " + reason);
        LOCK(cs_main);
        BlockMap::const_iterator it = mapBlockIndex.find(header.GetHash());
        BOOST_REQUIRE(it != mapBlockIndex.end());
        chain.push_back(it->second);
    }
    return chain;
}

bool IsProofOfWorkFailure(const std::string& reason)
{
    return reason == "high-hash" || reason == "invalid-mix-hash";
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(kawpow_header_tests, KawpowHeaderSetup)

BOOST_AUTO_TEST_CASE(honest_headers_are_accepted)
{
    std::vector<const CBlockIndex*> chain = BuildHonestChain(4);
    BOOST_CHECK_EQUAL(chain.back()->nHeight, 4);
    BOOST_CHECK(chain.back()->nTime >= nKAWPOWActivationTime);
}

// The v4.8.0 rule: from nHeightHeaderCheckActivation on, the declared height must be the real one.
BOOST_AUTO_TEST_CASE(lying_height_rejected_after_activation)
{
    std::vector<const CBlockIndex*> chain = BuildHonestChain(3);
    AddCheckpoint(chain[3]);

    CBlockHeader forged = NewHeader(chain[3], 1); // real height 4, claims 1 to reach the shortcut
    MineForged(forged);
    std::string reason;
    BOOST_CHECK(!AcceptHeader(forged, reason));
    BOOST_CHECK_EQUAL(reason, "bad-blk-height");
}

// Below nHeightHeaderCheckActivation a lying height is tolerated for historical compatibility,
// which left the shortcut open to forged headers there. They must now show real ProgPoW work.
BOOST_AUTO_TEST_CASE(lying_height_needs_real_work_before_activation)
{
    SetHeaderCheckActivation(1000);
    std::vector<const CBlockIndex*> chain = BuildHonestChain(3);
    AddCheckpoint(chain[3]);

    CBlockHeader forged = NewHeader(chain[3], 1);
    MineForged(forged);
    std::string reason;
    BOOST_CHECK(!AcceptHeader(forged, reason));
    BOOST_CHECK_EQUAL(reason, "bad-blk-height-pow");

    // An honestly mined header with the same mislabelled height is still accepted, so blocks
    // mined honestly before the activation height keep validating.
    CBlockHeader honest = NewHeader(chain[3], 1);
    MineHonest(honest);
    BOOST_CHECK_MESSAGE(AcceptHeader(honest, reason), "honest header rejected: " + reason);
}

// A sibling of the checkpoint block passed the shortcut (declared height <= checkpoint height)
// and the fork rule (which only refuses heights strictly below the checkpoint).
BOOST_AUTO_TEST_CASE(forged_sibling_of_checkpoint_rejected)
{
    std::vector<const CBlockIndex*> chain = BuildHonestChain(3);
    AddCheckpoint(chain[3]);

    CBlockHeader forged = NewHeader(chain[2], 3);
    MineForged(forged);
    std::string reason;
    BOOST_CHECK(!AcceptHeader(forged, reason));
    BOOST_CHECK_MESSAGE(IsProofOfWorkFailure(reason), "unexpected reject reason: " + reason);

    CBlockHeader honest = NewHeader(chain[2], 3);
    MineHonest(honest);
    BOOST_CHECK_MESSAGE(AcceptHeader(honest, reason), "honest header rejected: " + reason);
}

// With checkpoints enabled, a fork below the checkpoint is refused by the checkpoint rule.
BOOST_AUTO_TEST_CASE(fork_below_checkpoint_rejected)
{
    std::vector<const CBlockIndex*> chain = BuildHonestChain(3);
    AddCheckpoint(chain[3]);

    CBlockHeader forged = NewHeader(chain[1], 2);
    MineForged(forged);
    std::string reason;
    BOOST_CHECK(!AcceptHeader(forged, reason));
    BOOST_CHECK_EQUAL(reason, "bad-fork-prior-to-checkpoint");
}

// With -checkpoints=0 that rule is off, but the shortcut kept trusting the checkpoints, so forks
// below them could be built with no ProgPoW work at all.
BOOST_AUTO_TEST_CASE(shortcut_disabled_without_checkpoints)
{
    std::vector<const CBlockIndex*> chain = BuildHonestChain(3);
    AddCheckpoint(chain[3]);
    fCheckpointsEnabled = false;

    CBlockHeader forged = NewHeader(chain[1], 2);
    MineForged(forged);
    std::string reason;
    BOOST_CHECK(!AcceptHeader(forged, reason));
    BOOST_CHECK_MESSAGE(IsProofOfWorkFailure(reason), "unexpected reject reason: " + reason);

    CBlockHeader honest = NewHeader(chain[1], 2);
    MineHonest(honest);
    BOOST_CHECK_MESSAGE(AcceptHeader(honest, reason), "honest header rejected: " + reason);
}

BOOST_AUTO_TEST_SUITE_END()
