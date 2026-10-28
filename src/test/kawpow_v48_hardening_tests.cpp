// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "consensus/validation.h"
#include "hash.h"
#include "pow.h"
#include "primitives/block.h"
#include "test/test_raven.h"
#include "timedata.h"
#include "txdb.h"
#include "util.h"
#include "validation.h"
#include "validationinterface.h"
#include "versionbits.h"

#include <boost/test/unit_test.hpp>

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

struct ScopedKAWPOWContextBudget {
    const int64_t previous = GetKAWPOWContextBudgetMiB();

    explicit ScopedKAWPOWContextBudget(int64_t limit)
    {
        BOOST_REQUIRE(SetKAWPOWContextBudgetMiB(limit));
    }

    ~ScopedKAWPOWContextBudget()
    {
        SetKAWPOWContextBudgetMiB(previous);
    }
};

struct ScopedKAWPOWAllocationFailure {
    ScopedKAWPOWAllocationFailure()
    {
        // Drop this thread's cached context so the next full proof must allocate.
        ReleaseKAWPOWThreadContext();
        SetKAWPOWContextAllocationFailureForTesting(true);
    }

    ~ScopedKAWPOWAllocationFailure()
    {
        SetKAWPOWContextAllocationFailureForTesting(false);
    }
};

struct BlockCheckResult : public CValidationInterface {
    int calls = 0;
    bool invalid = false;
    bool error = false;
    std::string reason;

    BlockCheckResult() { RegisterValidationInterface(this); }
    ~BlockCheckResult() { UnregisterValidationInterface(this); }

    void BlockChecked(const CBlock&, const CValidationState& state) override
    {
        ++calls;
        invalid = state.IsInvalid();
        error = state.IsError();
        reason = state.GetRejectReason();
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(kawpow_v48_hardening_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(kawpow_declared_header_height_gate)
{
    std::unique_ptr<CChainParams> mainParams = CreateChainParams("main");
    BOOST_REQUIRE(mainParams);

    const Consensus::Params& consensus = mainParams->GetConsensus();
    const int activationHeight = consensus.nHeightHeaderCheckActivation;
    BOOST_REQUIRE_EQUAL(activationHeight, 4487776);
    BOOST_REQUIRE(nKAWPOWActivationTime > 0);

    CBlockHeader header;
    header.nTime = nKAWPOWActivationTime;

    // Exact height at the first protected block is valid.
    header.nHeight = static_cast<uint32_t>(activationHeight);
    BOOST_CHECK(IsKAWPOWHeaderHeightValid(header,
                                          activationHeight,
                                          activationHeight,
                                          nKAWPOWActivationTime));

    // The exploit primitive: a forged declared KAWPOW height at/after the 4.8
    // gate must be rejected by the same predicate used by validation.cpp.
    header.nHeight = static_cast<uint32_t>(activationHeight - 1);
    BOOST_CHECK(!IsKAWPOWHeaderHeightValid(header,
                                           activationHeight,
                                           activationHeight,
                                           nKAWPOWActivationTime));

    // Legacy blocks before the 4.8 activation height retain historical rules.
    header.nHeight = 1;
    BOOST_CHECK(IsKAWPOWHeaderHeightValid(header,
                                          activationHeight - 1,
                                          activationHeight,
                                          nKAWPOWActivationTime));

    // The declared KAWPOW height is not enforced before KAWPOW itself activates.
    header.nTime = nKAWPOWActivationTime - 1;
    BOOST_CHECK(IsKAWPOWHeaderHeightValid(header,
                                          activationHeight,
                                          activationHeight,
                                          nKAWPOWActivationTime));
}

BOOST_AUTO_TEST_CASE(kawpow_local_context_budget_boundaries)
{
    ScopedKAWPOWContextBudget budget(256);
    BOOST_CHECK(IsKAWPOWContextWithinLocalBudget(1919 * 7500));
    BOOST_CHECK(!IsKAWPOWContextWithinLocalBudget(1920 * 7500));

    BOOST_REQUIRE(SetKAWPOWContextBudgetMiB(65536));
    BOOST_CHECK(IsKAWPOWContextWithinLocalBudget(32639 * 7500));
    BOOST_CHECK(!IsKAWPOWContextWithinLocalBudget(32640 * 7500));
    BOOST_CHECK(!IsKAWPOWContextWithinLocalBudget(std::numeric_limits<uint32_t>::max()));
}

BOOST_AUTO_TEST_CASE(kawpow_context_budget_rejects_unusable_values)
{
    ScopedKAWPOWContextBudget budget(64);
    BOOST_CHECK(!SetKAWPOWContextBudgetMiB(0));
    BOOST_CHECK(!SetKAWPOWContextBudgetMiB(-1));
    BOOST_CHECK(!SetKAWPOWContextBudgetMiB(std::numeric_limits<int64_t>::max()));
    BOOST_CHECK_EQUAL(GetKAWPOWContextBudgetMiB(), 64);
    BOOST_CHECK(SetKAWPOWContextBudgetMiB(1));
    BOOST_CHECK_EQUAL(GetKAWPOWContextBudgetMiB(), 1);
}

BOOST_AUTO_TEST_SUITE_END()

namespace {

struct ForgedKAWPOWHeaderSetup : public TestingSetup {
    uint32_t savedActivationTime;
    bool savedCheckpointsEnabled;

    ForgedKAWPOWHeaderSetup() : TestingSetup(CBaseChainParams::REGTEST),
        savedActivationTime(nKAWPOWActivationTime),
        savedCheckpointsEnabled(fCheckpointsEnabled)
    {
        fCheckpointsEnabled = true;
        LOCK(cs_main);
        nKAWPOWActivationTime = chainActive.Tip()->nTime + 1;
    }

    ~ForgedKAWPOWHeaderSetup()
    {
        nKAWPOWActivationTime = savedActivationTime;
        fCheckpointsEnabled = savedCheckpointsEnabled;
    }
};

CBlockHeader NextKAWPOWHeader(const CBlockIndex* prev, uint32_t declaredHeight)
{
    static uint32_t salt = 0;
    CBlockHeader header;
    header.nVersion = VERSIONBITS_TOP_BITS_ASSETS;
    header.hashPrevBlock = prev->GetBlockHash();
    header.hashMerkleRoot = ArithToUint256(arith_uint256(++salt));
    header.nTime = prev->nTime + 60;
    header.nHeight = declaredHeight;
    header.nBits = GetNextWorkRequired(prev, &header, GetParams().GetConsensus());
    return header;
}

void MineKAWPOWHeader(CBlockHeader& header)
{
    uint256 mixHash;
    while (!CheckProofOfWork(header.GetHashFull(mixHash), header.nBits,
                             GetParams().GetConsensus())) {
        ++header.nNonce64;
    }
    header.mix_hash = mixHash;
}

void GrindClaimedKAWPOWTarget(CBlockHeader& header, bool passesTarget)
{
    header.mix_hash = ArithToUint256(arith_uint256(1));
    while (CheckProofOfWork(header.GetHash(), header.nBits,
                            GetParams().GetConsensus()) != passesTarget) {
        ++header.nNonce64;
    }
}

void ForgeKAWPOWHeader(CBlockHeader& header)
{
    // Grind the final hash using an invented mix_hash, without performing ProgPoW.
    GrindClaimedKAWPOWTarget(header, true);
    uint256 actualMixHash;
    header.GetHashFull(actualMixHash);
    BOOST_REQUIRE(actualMixHash != header.mix_hash);
}

bool SubmitKAWPOWHeader(const CBlockHeader& header, std::string& reason)
{
    CValidationState state;
    const CBlockIndex* index = nullptr;
    const bool accepted = ProcessNewBlockHeaders({header}, state, GetParams(), &index);
    reason = state.GetRejectReason();
    return accepted;
}

std::vector<const CBlockIndex*> BuildKAWPOWHeaderChain(int length)
{
    std::vector<const CBlockIndex*> chain;
    {
        LOCK(cs_main);
        chain.push_back(chainActive.Tip());
    }
    for (int height = 1; height <= length; ++height) {
        CBlockHeader header = NextKAWPOWHeader(chain.back(), height);
        MineKAWPOWHeader(header);
        std::string reason;
        BOOST_REQUIRE_MESSAGE(SubmitKAWPOWHeader(header, reason),
                              "valid KAWPOW header rejected: " + reason);
        LOCK(cs_main);
        const auto it = mapBlockIndex.find(header.GetHash());
        BOOST_REQUIRE(it != mapBlockIndex.end());
        chain.push_back(it->second);
    }
    return chain;
}

void CheckForgedHeaderRejected(const CBlockHeader& header, const std::string& expectedReason)
{
    std::string reason;
    BOOST_CHECK(!SubmitKAWPOWHeader(header, reason));
    if (!expectedReason.empty()) {
        BOOST_CHECK_EQUAL(reason, expectedReason);
    } else {
        BOOST_CHECK(reason == "high-hash" || reason == "invalid-mix-hash");
    }
}

void CheckHonestHeaderAccepted(const CBlockHeader& header)
{
    std::string reason;
    BOOST_CHECK_MESSAGE(SubmitKAWPOWHeader(header, reason),
                        "honest KAWPOW header rejected: " + reason);
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(kawpow_forged_header_tests, ForgedKAWPOWHeaderSetup)

BOOST_AUTO_TEST_CASE(pre_activation_false_height_requires_full_work)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(3);
    const_cast<CCheckpointData&>(GetParams().Checkpoints()).mapCheckpoints[3] = chain[3]->GetBlockHash();

    CBlockHeader forged = NextKAWPOWHeader(chain[3], 1);
    ForgeKAWPOWHeader(forged);
    CheckForgedHeaderRejected(forged, "");

    CBlockHeader honest = NextKAWPOWHeader(chain[3], 1);
    MineKAWPOWHeader(honest);
    CheckHonestHeaderAccepted(honest);

    // The historical rule also permits a higher declared height before the
    // activation gate when its full KAWPOW proof is genuine.
    CBlockHeader honestHigher = NextKAWPOWHeader(chain[3], 5);
    MineKAWPOWHeader(honestHigher);
    CheckHonestHeaderAccepted(honestHigher);
}

BOOST_AUTO_TEST_CASE(checkpoint_height_sibling_requires_full_work)
{
    const auto chain = BuildKAWPOWHeaderChain(3);
    const_cast<CCheckpointData&>(GetParams().Checkpoints()).mapCheckpoints[3] = chain[3]->GetBlockHash();

    CBlockHeader forged = NextKAWPOWHeader(chain[2], 3);
    ForgeKAWPOWHeader(forged);
    CheckForgedHeaderRejected(forged, "");

    CBlockHeader honest = NextKAWPOWHeader(chain[2], 3);
    MineKAWPOWHeader(honest);
    CheckHonestHeaderAccepted(honest);
}

BOOST_AUTO_TEST_CASE(disabled_checkpoints_require_full_work)
{
    const auto chain = BuildKAWPOWHeaderChain(3);
    const_cast<CCheckpointData&>(GetParams().Checkpoints()).mapCheckpoints[3] = chain[3]->GetBlockHash();
    fCheckpointsEnabled = false;

    CBlockHeader forged = NextKAWPOWHeader(chain[1], 2);
    ForgeKAWPOWHeader(forged);
    CheckForgedHeaderRejected(forged, "");

    CBlockHeader honest = NextKAWPOWHeader(chain[1], 2);
    MineKAWPOWHeader(honest);
    CheckHonestHeaderAccepted(honest);
}

BOOST_AUTO_TEST_CASE(older_checkpoint_fork_is_rejected_before_full_work)
{
    const auto chain = BuildKAWPOWHeaderChain(3);
    const_cast<CCheckpointData&>(GetParams().Checkpoints()).mapCheckpoints[3] = chain[3]->GetBlockHash();

    CBlockHeader forged = NextKAWPOWHeader(chain[1], 2);
    ForgeKAWPOWHeader(forged);
    std::string reason;
    BOOST_CHECK(!SubmitKAWPOWHeader(forged, reason));
    BOOST_CHECK_EQUAL(reason, "bad-fork-prior-to-checkpoint");
}

BOOST_AUTO_TEST_CASE(indexed_fork_header_does_not_regain_shortcut)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(3);
    const_cast<CCheckpointData&>(GetParams().Checkpoints()).mapCheckpoints[3] = chain[3]->GetBlockHash();

    CBlockHeader forged = NextKAWPOWHeader(chain[3], 1);
    ForgeKAWPOWHeader(forged);
    {
        // Model a header saved by an older node before this validation fix.
        LOCK(cs_main);
        CBlockIndex* saved = InsertBlockIndex(forged.GetHash());
        saved->pprev = const_cast<CBlockIndex*>(chain[3]);
        saved->nHeight = 4;
        saved->BuildSkip();
    }

    CValidationState state;
    const CBlock block(forged);
    BOOST_CHECK(!CheckBlock(block, state, GetParams().GetConsensus(), true, false));
    BOOST_CHECK(state.GetRejectReason() == "high-hash" ||
                state.GetRejectReason() == "invalid-mix-hash");

}

BOOST_AUTO_TEST_CASE(saved_forged_branch_is_excluded_on_restart)
{
    const auto chain = BuildKAWPOWHeaderChain(3);
    const uint256 checkpointHash = chain[3]->GetBlockHash();
    const_cast<CCheckpointData&>(GetParams().Checkpoints()).mapCheckpoints[3] = checkpointHash;

    CBlockHeader forged = NextKAWPOWHeader(chain[2], 3);
    ForgeKAWPOWHeader(forged);
    CBlockHeader child = NextKAWPOWHeader(chain[3], 4);
    child.hashPrevBlock = forged.GetHash();
    MineKAWPOWHeader(child);
    const uint256 forgedHash = forged.GetHash();
    const uint256 childHash = child.GetHash();

    std::vector<const CBlockIndex*> entries;
    {
        LOCK(cs_main);
        CBlockIndex* forkIndex = InsertBlockIndex(forgedHash);
        forkIndex->pprev = const_cast<CBlockIndex*>(chain[2]);
        forkIndex->nHeight = 3;
        forkIndex->nVersion = forged.nVersion;
        forkIndex->hashMerkleRoot = forged.hashMerkleRoot;
        forkIndex->nTime = forged.nTime;
        forkIndex->nBits = forged.nBits;
        forkIndex->nNonce64 = forged.nNonce64;
        forkIndex->mix_hash = forged.mix_hash;
        forkIndex->nHeaderHeight = forged.nHeight;
        forkIndex->nStatus = BLOCK_VALID_TREE;
        forkIndex->BuildSkip();

        CBlockIndex* childIndex = InsertBlockIndex(childHash);
        childIndex->pprev = forkIndex;
        childIndex->nHeight = 4;
        childIndex->nVersion = child.nVersion;
        childIndex->hashMerkleRoot = child.hashMerkleRoot;
        childIndex->nTime = child.nTime;
        childIndex->nBits = child.nBits;
        childIndex->nNonce64 = child.nNonce64;
        childIndex->mix_hash = child.mix_hash;
        childIndex->nHeaderHeight = child.nHeight;
        childIndex->nStatus = BLOCK_VALID_TREE;
        childIndex->BuildSkip();

        for (const auto& entry : mapBlockIndex)
            entries.push_back(entry.second);
    }
    BOOST_REQUIRE(pblocktree->WriteBatchSync({}, 0, entries));
    UnloadBlockIndex();
    BOOST_REQUIRE(LoadBlockIndex(GetParams()));

    LOCK(cs_main);
    BOOST_REQUIRE(mapBlockIndex.count(forgedHash) == 1);
    BOOST_REQUIRE(mapBlockIndex.count(childHash) == 1);
    BOOST_CHECK(mapBlockIndex.at(forgedHash)->nStatus & BLOCK_FAILED_VALID);
    BOOST_CHECK(mapBlockIndex.at(childHash)->nStatus & BLOCK_FAILED_CHILD);
    BOOST_REQUIRE(pindexBestHeader != nullptr);
    BOOST_CHECK(pindexBestHeader->GetBlockHash() == checkpointHash);
    pcoinsTip->SetBestBlock(forgedHash);
    BOOST_CHECK(!LoadChainTip(GetParams()));
}

BOOST_AUTO_TEST_CASE(saved_header_key_must_match_reconstructed_hash)
{
    const auto chain = BuildKAWPOWHeaderChain(3);
    CBlockHeader declared = NextKAWPOWHeader(chain[2], 1);
    declared.mix_hash = ArithToUint256(arith_uint256(1));
    CBlockHeader reconstructed = declared;
    reconstructed.nHeight = 3;
    while (!CheckProofOfWork(declared.GetHash(), declared.nBits, GetParams().GetConsensus()) ||
           !CheckProofOfWork(reconstructed.GetHash(), reconstructed.nBits, GetParams().GetConsensus())) {
        ++declared.nNonce64;
        reconstructed.nNonce64 = declared.nNonce64;
    }
    const uint256 declaredHash = declared.GetHash();
    const uint256 reconstructedHash = reconstructed.GetHash();
    BOOST_REQUIRE(declaredHash != reconstructedHash);

    std::vector<const CBlockIndex*> entries;
    {
        LOCK(cs_main);
        CBlockIndex* saved = InsertBlockIndex(declaredHash);
        saved->pprev = const_cast<CBlockIndex*>(chain[2]);
        saved->nHeight = 3;
        saved->nVersion = declared.nVersion;
        saved->hashMerkleRoot = declared.hashMerkleRoot;
        saved->nTime = declared.nTime;
        saved->nBits = declared.nBits;
        saved->nNonce64 = declared.nNonce64;
        saved->mix_hash = declared.mix_hash;
        saved->nHeaderHeight = declared.nHeight;
        saved->nStatus = BLOCK_VALID_TREE;
        saved->BuildSkip();
        for (const auto& entry : mapBlockIndex)
            entries.push_back(entry.second);
    }
    BOOST_REQUIRE(pblocktree->WriteBatchSync({}, 0, entries));
    UnloadBlockIndex();
    BOOST_REQUIRE(LoadBlockIndex(GetParams()));

    LOCK(cs_main);
    BOOST_CHECK(mapBlockIndex.count(declaredHash) == 0);
    BOOST_CHECK(mapBlockIndex.count(reconstructedHash) == 0);
}

BOOST_AUTO_TEST_CASE(honest_pre_gate_declared_height_survives_restart)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(3);
    const_cast<CCheckpointData&>(GetParams().Checkpoints()).mapCheckpoints[3] = chain[3]->GetBlockHash();

    CBlockHeader honest = NextKAWPOWHeader(chain[3], 5);
    MineKAWPOWHeader(honest);
    const uint256 originalHash = honest.GetHash();
    CheckHonestHeaderAccepted(honest);

    std::vector<const CBlockIndex*> entries;
    {
        LOCK(cs_main);
        BOOST_REQUIRE(mapBlockIndex.count(originalHash) == 1);
        BOOST_CHECK_EQUAL(mapBlockIndex.at(originalHash)->nHeight, 4);
        for (const auto& entry : mapBlockIndex)
            entries.push_back(entry.second);
    }
    BOOST_REQUIRE(pblocktree->WriteBatchSync({}, 0, entries));
    UnloadBlockIndex();
    BOOST_REQUIRE(LoadBlockIndex(GetParams()));

    LOCK(cs_main);
    BOOST_REQUIRE(mapBlockIndex.count(originalHash) == 1);
    const CBlockIndex* restored = mapBlockIndex.at(originalHash);
    BOOST_CHECK_EQUAL(restored->nHeight, 4);
    BOOST_CHECK(restored->nStatus & BLOCK_HAVE_DECLARED_HEIGHT);
    BOOST_CHECK_EQUAL(restored->GetBlockHeader().nHeight, 5U);
    BOOST_CHECK(restored->GetBlockHeader().GetHash() == originalHash);
}

BOOST_AUTO_TEST_CASE(post_gate_false_height_is_rejected_before_full_hash)
{
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader forged = NextKAWPOWHeader(chain[1], 5);
    ForgeKAWPOWHeader(forged);
    std::string reason;
    BOOST_CHECK(!SubmitKAWPOWHeader(forged, reason));
    BOOST_CHECK_EQUAL(reason, "bad-blk-height");
}

BOOST_AUTO_TEST_CASE(pre_gate_higher_declared_height_keeps_full_work_rule)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader honest = NextKAWPOWHeader(chain[1], 1001);
    MineKAWPOWHeader(honest);
    CheckHonestHeaderAccepted(honest);
}

BOOST_AUTO_TEST_CASE(pre_gate_extreme_declared_height_is_a_local_error)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader extreme = NextKAWPOWHeader(chain[1], std::numeric_limits<uint32_t>::max());
    GrindClaimedKAWPOWTarget(extreme, true);

    CValidationState state;
    const CBlockIndex* index = nullptr;
    BOOST_CHECK(!ProcessNewBlockHeaders({extreme}, state, GetParams(), &index));
    BOOST_CHECK(state.IsError());
    BOOST_CHECK(!state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "kawpow-context-unsupported");
    BOOST_CHECK(index == nullptr);

    LOCK(cs_main);
    BOOST_CHECK(mapBlockIndex.count(extreme.GetHash()) == 0);
}

BOOST_AUTO_TEST_CASE(pre_gate_over_target_mix_precedes_context_allocation)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader overTarget = NextKAWPOWHeader(chain[1], 7500);
    GrindClaimedKAWPOWTarget(overTarget, false);

    ScopedKAWPOWContextBudget budget(16);
    CValidationState state;
    const CBlockIndex* index = nullptr;
    BOOST_CHECK(!ProcessNewBlockHeaders({overTarget}, state, GetParams(), &index));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "high-hash");
    BOOST_CHECK(index == nullptr);
}

BOOST_AUTO_TEST_CASE(pre_gate_under_target_mix_still_requires_full_work)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader underTarget = NextKAWPOWHeader(chain[1], 7500);
    GrindClaimedKAWPOWTarget(underTarget, true);

    ScopedKAWPOWContextBudget budget(16);
    CValidationState state;
    const CBlockIndex* index = nullptr;
    BOOST_CHECK(!ProcessNewBlockHeaders({underTarget}, state, GetParams(), &index));
    BOOST_CHECK(state.IsError());
    BOOST_CHECK(!state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "kawpow-context-unsupported");
    BOOST_CHECK(index == nullptr);
}

BOOST_AUTO_TEST_CASE(pre_gate_budget_error_does_not_invalidate_valid_header)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader honest = NextKAWPOWHeader(chain[1], 7500);
    MineKAWPOWHeader(honest);

    ScopedKAWPOWContextBudget budget(16);
    CValidationState constrainedState;
    const CBlockIndex* index = nullptr;
    BOOST_CHECK(!ProcessNewBlockHeaders({honest}, constrainedState, GetParams(), &index));
    BOOST_CHECK(constrainedState.IsError());
    BOOST_CHECK(!constrainedState.IsInvalid());
    BOOST_CHECK_EQUAL(constrainedState.GetRejectReason(), "kawpow-context-unsupported");
    BOOST_CHECK(index == nullptr);
    {
        LOCK(cs_main);
        BOOST_CHECK(mapBlockIndex.count(honest.GetHash()) == 0);
    }

    BOOST_REQUIRE(SetKAWPOWContextBudgetMiB(256));
    CheckHonestHeaderAccepted(honest);
    {
        LOCK(cs_main);
        BOOST_REQUIRE(mapBlockIndex.count(honest.GetHash()) == 1);
        BOOST_CHECK(!(mapBlockIndex.at(honest.GetHash())->nStatus & BLOCK_FAILED_MASK));
    }
}

BOOST_AUTO_TEST_CASE(pre_gate_bad_difficulty_precedes_context_allocation)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader bad = NextKAWPOWHeader(chain[1], 7500);
    bad.nBits ^= 1;

    ScopedKAWPOWContextBudget budget(16);
    CValidationState state;
    const CBlockIndex* index = nullptr;
    BOOST_CHECK(!ProcessNewBlockHeaders({bad}, state, GetParams(), &index));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-diffbits");
    BOOST_CHECK(index == nullptr);
}

BOOST_AUTO_TEST_CASE(pre_gate_full_block_bad_difficulty_precedes_context_allocation)
{
    auto& consensus = const_cast<Consensus::Params&>(GetParams().GetConsensus());
    consensus.nHeightHeaderCheckActivation = 1000;
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader bad = NextKAWPOWHeader(chain[1], 7500);
    bad.nBits ^= 1;

    ScopedKAWPOWContextBudget budget(16);
    BlockCheckResult result;
    BOOST_CHECK(!ProcessNewBlock(GetParams(), std::make_shared<const CBlock>(bad),
                                 true, nullptr));
    BOOST_CHECK_EQUAL(result.calls, 1);
    BOOST_CHECK(result.invalid);
    BOOST_CHECK(!result.error);
    BOOST_CHECK_EQUAL(result.reason, "bad-diffbits");

    LOCK(cs_main);
    BOOST_CHECK(mapBlockIndex.count(bad.GetHash()) == 0);
}

BOOST_AUTO_TEST_CASE(unknown_parent_is_rejected_before_full_hash)
{
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader forged = NextKAWPOWHeader(chain[1], 2);
    forged.hashPrevBlock = ArithToUint256(arith_uint256(123456));
    ForgeKAWPOWHeader(forged);
    std::string reason;
    BOOST_CHECK(!SubmitKAWPOWHeader(forged, reason));
    BOOST_CHECK_EQUAL(reason, "prev-blk-not-found");
}

BOOST_AUTO_TEST_CASE(context_allocation_failure_is_a_local_error)
{
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader honest = NextKAWPOWHeader(chain[1], 2);
    MineKAWPOWHeader(honest);
    {
        ScopedKAWPOWAllocationFailure failure;
        CValidationState state;
        const CBlockIndex* index = nullptr;
        BOOST_CHECK(!ProcessNewBlockHeaders({honest}, state, GetParams(), &index));
        BOOST_CHECK(state.IsError());
        BOOST_CHECK(!state.IsInvalid());
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "kawpow-context-allocation-failed");
        BOOST_CHECK(index == nullptr);
        LOCK(cs_main);
        BOOST_CHECK(mapBlockIndex.count(honest.GetHash()) == 0);
    }

    // The local failure must not be remembered as a proof result.
    CheckHonestHeaderAccepted(honest);
    LOCK(cs_main);
    BOOST_REQUIRE(mapBlockIndex.count(honest.GetHash()) == 1);
    BOOST_CHECK(!(mapBlockIndex.at(honest.GetHash())->nStatus & BLOCK_FAILED_MASK));
}

BOOST_AUTO_TEST_CASE(replayed_invalid_proof_needs_no_context)
{
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader forged = NextKAWPOWHeader(chain[1], 2);
    ForgeKAWPOWHeader(forged);
    std::string firstReason;
    BOOST_CHECK(!SubmitKAWPOWHeader(forged, firstReason));
    BOOST_CHECK(firstReason == "high-hash" || firstReason == "invalid-mix-hash");

    // Another full check would need a new context and fail locally.
    ScopedKAWPOWAllocationFailure failure;
    CValidationState state;
    BOOST_CHECK(!ProcessNewBlockHeaders({forged}, state, GetParams()));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK(!state.IsError());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), firstReason);
}

BOOST_AUTO_TEST_CASE(replayed_valid_proof_still_runs_contextual_checks)
{
    const auto chain = BuildKAWPOWHeaderChain(1);
    CBlockHeader future = NextKAWPOWHeader(chain[1], 2);
    future.nTime = GetAdjustedTime() + 24 * 60 * 60;
    future.nBits = GetNextWorkRequired(chain[1], &future, GetParams().GetConsensus());
    MineKAWPOWHeader(future);
    std::string reason;
    BOOST_CHECK(!SubmitKAWPOWHeader(future, reason));
    BOOST_CHECK_EQUAL(reason, "time-too-new");

    // The stored valid proof skips ProgPoW, not the contextual rule.
    ScopedKAWPOWAllocationFailure failure;
    BOOST_CHECK(!SubmitKAWPOWHeader(future, reason));
    BOOST_CHECK_EQUAL(reason, "time-too-new");
}

BOOST_AUTO_TEST_SUITE_END()
