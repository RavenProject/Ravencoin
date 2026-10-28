// Copyright (c) 2026 ALENOC (https://github.com/ALENOC)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h"
#include "assets/assets.h"
#include "base58.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "primitives/block.h"
#include "script/standard.h"
#include "test/test_raven.h"
#include "validation.h"
#include "versionbits.h"

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {

class SyntheticVersionBitsChain
{
private:
    CBlockIndex* base;
    std::vector<std::unique_ptr<CBlockIndex>> blocks;

public:
    explicit SyntheticVersionBitsChain(CBlockIndex* baseIn = nullptr) : base(baseIn) {}

    CBlockIndex* Tip() const
    {
        return blocks.empty() ? base : blocks.back().get();
    }

    void Mine(unsigned int count, int32_t version)
    {
        for (unsigned int i = 0; i < count; ++i) {
            auto block = std::make_unique<CBlockIndex>();
            block->pprev = Tip();
            block->nHeight = block->pprev ? block->pprev->nHeight + 1 : 0;
            block->nTime = 100000 + block->nHeight;
            block->nVersion = version;
            block->BuildSkip();
            blocks.emplace_back(std::move(block));
        }
    }
};

Consensus::Params MakeRIP25VersionBitsParams()
{
    Consensus::Params params;
    params.nMinerConfirmationWindow = 4;
    params.nRuleChangeActivationThreshold = 3;

    auto& overflow = params.vDeployments[Consensus::DEPLOYMENT_TRANSFER_OVERFLOW];
    overflow.bit = 11;
    overflow.nStartTime = 0;
    overflow.nTimeout = std::numeric_limits<int64_t>::max();
    overflow.nOverrideRuleChangeActivationThreshold = 3;
    overflow.nOverrideMinerConfirmationWindow = 4;

    auto& pq = params.vDeployments[Consensus::DEPLOYMENT_PQ_HYBRID];
    pq.bit = 12;
    pq.nStartTime = 0;
    pq.nTimeout = std::numeric_limits<int64_t>::max();
    pq.nOverrideRuleChangeActivationThreshold = 3;
    pq.nOverrideMinerConfirmationWindow = 4;

    auto& pqAssets = params.vDeployments[Consensus::DEPLOYMENT_PQ_ASSETS];
    pqAssets.bit = 13;
    pqAssets.nStartTime = 0;
    pqAssets.nTimeout = std::numeric_limits<int64_t>::max();
    pqAssets.nOverrideRuleChangeActivationThreshold = 3;
    pqAssets.nOverrideMinerConfirmationWindow = 4;

    return params;
}

} // namespace

BOOST_AUTO_TEST_SUITE(rip25_versionbits_tests)

BOOST_AUTO_TEST_CASE(pq_asset_public_deployment_parameters)
{
    const auto main = CreateChainParams(CBaseChainParams::MAIN);
    const auto testnet = CreateChainParams(CBaseChainParams::TESTNET);
    const auto regtest = CreateChainParams(CBaseChainParams::REGTEST);
    const auto& mainAssets = main->GetConsensus().vDeployments[Consensus::DEPLOYMENT_PQ_ASSETS];
    const auto& testAssets = testnet->GetConsensus().vDeployments[Consensus::DEPLOYMENT_PQ_ASSETS];
    const auto& regAssets = regtest->GetConsensus().vDeployments[Consensus::DEPLOYMENT_PQ_ASSETS];

    BOOST_CHECK_EQUAL(mainAssets.bit, 13);
    BOOST_CHECK_EQUAL(mainAssets.nStartTime, 1861920000LL);
    BOOST_CHECK_EQUAL(mainAssets.nTimeout, 1909094400LL);
    BOOST_CHECK_EQUAL(mainAssets.nOverrideRuleChangeActivationThreshold, 1714);
    BOOST_CHECK_EQUAL(mainAssets.nOverrideMinerConfirmationWindow, 2016);

    BOOST_CHECK_EQUAL(testAssets.bit, 13);
    BOOST_CHECK_EQUAL(testAssets.nStartTime, 1806537600LL);
    BOOST_CHECK_EQUAL(testAssets.nTimeout, 1853971200LL);
    BOOST_CHECK_EQUAL(testAssets.nOverrideRuleChangeActivationThreshold, 1310);
    BOOST_CHECK_EQUAL(testAssets.nOverrideMinerConfirmationWindow, 2016);

    BOOST_CHECK_EQUAL(regAssets.nStartTime, 999999999999LL);
    BOOST_CHECK_EQUAL(regAssets.nTimeout, 999999999999LL);
}

BOOST_AUTO_TEST_CASE(pq_bit12_activates_independently_from_v48_overflow_bit11)
{
    Consensus::Params params = MakeRIP25VersionBitsParams();
    VersionBitsCache cache;
    SyntheticVersionBitsChain chain;

    const uint32_t overflowMask = VersionBitsMask(params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW);
    const uint32_t pqMask = VersionBitsMask(params, Consensus::DEPLOYMENT_PQ_HYBRID);

    BOOST_CHECK_EQUAL(overflowMask, (1U << 11));
    BOOST_CHECK_EQUAL(pqMask, (1U << 12));
    BOOST_CHECK_EQUAL(overflowMask & pqMask, 0U);

    // Genesis/first period is DEFINED. After the first four-block period,
    // both deployments enter STARTED because their start time is zero.
    BOOST_CHECK_EQUAL(VersionBitsState(nullptr, params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW, cache), THRESHOLD_DEFINED);
    BOOST_CHECK_EQUAL(VersionBitsState(nullptr, params, Consensus::DEPLOYMENT_PQ_HYBRID, cache), THRESHOLD_DEFINED);

    chain.Mine(4, VERSIONBITS_TOP_BITS);
    BOOST_CHECK_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW, cache), THRESHOLD_STARTED);
    BOOST_CHECK_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_PQ_HYBRID, cache), THRESHOLD_STARTED);

    // Signal only bit 12 in 3/4 blocks. PQ locks in; the v4.8 overflow
    // deployment remains STARTED. This detects any accidental bit collision.
    chain.Mine(3, VERSIONBITS_TOP_BITS | pqMask);
    chain.Mine(1, VERSIONBITS_TOP_BITS);
    BOOST_CHECK_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_PQ_HYBRID, cache), THRESHOLD_LOCKED_IN);
    BOOST_CHECK_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW, cache), THRESHOLD_STARTED);

    // LOCKED_IN becomes ACTIVE for the next period regardless of further
    // signaling. StateSinceHeight must identify the first ACTIVE block exactly.
    chain.Mine(4, VERSIONBITS_TOP_BITS);
    BOOST_CHECK_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_PQ_HYBRID, cache), THRESHOLD_ACTIVE);
    BOOST_CHECK_EQUAL(VersionBitsStateSinceHeight(chain.Tip(), params, Consensus::DEPLOYMENT_PQ_HYBRID, cache), 12);
    BOOST_CHECK_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW, cache), THRESHOLD_STARTED);
}

BOOST_AUTO_TEST_CASE(pq_bit12_resources_follow_exact_boundary_and_reorg)
{
    Consensus::Params params = MakeRIP25VersionBitsParams();
    params.nPQHybridEnabled = false;
    params.nPowTargetSpacing = 60;
    const uint32_t pqMask = VersionBitsMask(params, Consensus::DEPLOYMENT_PQ_HYBRID);

    SyntheticVersionBitsChain common;
    common.Mine(4, VERSIONBITS_TOP_BITS);
    SyntheticVersionBitsChain signaled(common.Tip());
    signaled.Mine(3, VERSIONBITS_TOP_BITS | pqMask);
    signaled.Mine(1, VERSIONBITS_TOP_BITS);
    signaled.Mine(2, VERSIONBITS_TOP_BITS);
    CBlockIndex* twoBefore = signaled.Tip();
    signaled.Mine(1, VERSIONBITS_TOP_BITS);
    CBlockIndex* oneBefore = signaled.Tip();
    signaled.Mine(1, VERSIONBITS_TOP_BITS);
    CBlockIndex* activationParent = signaled.Tip();
    signaled.Mine(1, VERSIONBITS_TOP_BITS);
    CBlockIndex* oneAfter = signaled.Tip();
    signaled.Mine(1, VERSIONBITS_TOP_BITS);
    CBlockIndex* twoAfter = signaled.Tip();

    BOOST_REQUIRE_EQUAL(twoBefore->nHeight, 9);
    BOOST_REQUIRE_EQUAL(oneBefore->nHeight, 10);
    BOOST_REQUIRE_EQUAL(activationParent->nHeight, 11);
    BOOST_REQUIRE_EQUAL(oneAfter->nHeight, 12);
    BOOST_REQUIRE_EQUAL(twoAfter->nHeight, 13);

    const CBlockIndex* parents[] = {
        twoBefore, oneBefore, activationParent, oneAfter, twoAfter,
    };
    const bool expectedActive[] = {false, false, true, true, true};
    {
        LOCK(cs_main);
        versionbitscache.Clear();
        for (unsigned int i = 0; i < 5; ++i)
            BOOST_CHECK_EQUAL(IsPQWitnessDiscountActive(parents[i], params), expectedActive[i]);
    }
    for (unsigned int i = 0; i < 5; ++i) {
        const unsigned int expectedWeight = expectedActive[i] ?
            MAX_BLOCK_WEIGHT_RIP25_PHASE1 : MAX_BLOCK_WEIGHT_RIP2;
        BOOST_CHECK_EQUAL(GetMaxBlockWeightForPrev(parents[i], params), expectedWeight);
        BOOST_CHECK_EQUAL(GetMaxBlockSerializedSizeForPrev(parents[i], params), expectedWeight);
    }

    SyntheticVersionBitsChain unsignaled(common.Tip());
    unsignaled.Mine(10, VERSIONBITS_TOP_BITS);
    {
        LOCK(cs_main);
        BOOST_CHECK(!IsPQWitnessDiscountActive(unsignaled.Tip(), params));
    }
    BOOST_CHECK_EQUAL(GetMaxBlockWeightForPrev(unsignaled.Tip(), params),
                      MAX_BLOCK_WEIGHT_RIP2);
    {
        LOCK(cs_main);
        versionbitscache.Clear();
    }
}

BOOST_AUTO_TEST_CASE(overflow_bit11_does_not_signal_pq_bit12)
{
    Consensus::Params params = MakeRIP25VersionBitsParams();
    VersionBitsCache cache;
    SyntheticVersionBitsChain chain;

    const uint32_t overflowMask = VersionBitsMask(params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW);

    chain.Mine(4, VERSIONBITS_TOP_BITS);
    BOOST_REQUIRE_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_PQ_HYBRID, cache), THRESHOLD_STARTED);

    chain.Mine(3, VERSIONBITS_TOP_BITS | overflowMask);
    chain.Mine(1, VERSIONBITS_TOP_BITS);

    BOOST_CHECK_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW, cache), THRESHOLD_LOCKED_IN);
    BOOST_CHECK_EQUAL(VersionBitsState(chain.Tip(), params, Consensus::DEPLOYMENT_PQ_HYBRID, cache), THRESHOLD_STARTED);
}

BOOST_AUTO_TEST_CASE(pq_asset_bit13_is_distinct_and_rewinds_across_forks)
{
    Consensus::Params params = MakeRIP25VersionBitsParams();
    VersionBitsCache cache;
    SyntheticVersionBitsChain common;
    const uint32_t overflowMask = VersionBitsMask(params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW);
    const uint32_t nativeMask = VersionBitsMask(params, Consensus::DEPLOYMENT_PQ_HYBRID);
    const uint32_t assetMask = VersionBitsMask(params, Consensus::DEPLOYMENT_PQ_ASSETS);
    BOOST_CHECK_EQUAL(assetMask, 1U << 13);
    BOOST_CHECK_EQUAL(assetMask & (overflowMask | nativeMask), 0U);

    common.Mine(4, VERSIONBITS_TOP_BITS);
    SyntheticVersionBitsChain signaled(common.Tip());
    signaled.Mine(3, VERSIONBITS_TOP_BITS | assetMask);
    signaled.Mine(1, VERSIONBITS_TOP_BITS);
    BOOST_CHECK_EQUAL(VersionBitsState(signaled.Tip(), params, Consensus::DEPLOYMENT_PQ_ASSETS, cache), THRESHOLD_LOCKED_IN);
    signaled.Mine(4, VERSIONBITS_TOP_BITS);
    BOOST_CHECK_EQUAL(VersionBitsState(signaled.Tip(), params, Consensus::DEPLOYMENT_PQ_ASSETS, cache), THRESHOLD_ACTIVE);
    BOOST_CHECK_EQUAL(VersionBitsStateSinceHeight(signaled.Tip(), params, Consensus::DEPLOYMENT_PQ_ASSETS, cache), 12);
    BOOST_CHECK_EQUAL(VersionBitsState(signaled.Tip(), params, Consensus::DEPLOYMENT_PQ_HYBRID, cache), THRESHOLD_STARTED);

    SyntheticVersionBitsChain unsignaled(common.Tip());
    unsignaled.Mine(8, VERSIONBITS_TOP_BITS);
    BOOST_CHECK_EQUAL(VersionBitsState(unsignaled.Tip(), params, Consensus::DEPLOYMENT_PQ_ASSETS, cache), THRESHOLD_STARTED);
}

BOOST_AUTO_TEST_CASE(transfer_overflow_state_rewinds_across_forks)
{
    Consensus::Params params = MakeRIP25VersionBitsParams();
    VersionBitsCache cache;
    SyntheticVersionBitsChain common;
    const uint32_t overflowMask = VersionBitsMask(params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW);

    common.Mine(4, VERSIONBITS_TOP_BITS);

    SyntheticVersionBitsChain activeBranch(common.Tip());
    activeBranch.Mine(3, VERSIONBITS_TOP_BITS | overflowMask);
    activeBranch.Mine(1, VERSIONBITS_TOP_BITS);
    activeBranch.Mine(4, VERSIONBITS_TOP_BITS);

    SyntheticVersionBitsChain startedBranch(common.Tip());
    startedBranch.Mine(8, VERSIONBITS_TOP_BITS);

    // Query ACTIVE first using the same cache, then rewind to the alternate
    // STARTED fork.  Activation must be a property of pindexPrev, not history.
    BOOST_REQUIRE_EQUAL(VersionBitsState(activeBranch.Tip(), params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW, cache), THRESHOLD_ACTIVE);
    BOOST_CHECK_EQUAL(VersionBitsState(startedBranch.Tip(), params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW, cache), THRESHOLD_STARTED);
    BOOST_CHECK(IsTransferOverflowCheckActive(activeBranch.Tip(), params));
    BOOST_CHECK(!IsTransferOverflowCheckActive(startedBranch.Tip(), params));
}

struct TransferOverflowRegtestSetup : BasicTestingSetup
{
    TransferOverflowRegtestSetup() : BasicTestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_CASE(transfer_overflow_active_tip_policy_is_not_sticky, TransferOverflowRegtestSetup)
{
    const Consensus::Params& params = GetParams().GetConsensus();
    const uint32_t overflowMask = VersionBitsMask(params, Consensus::DEPLOYMENT_TRANSFER_OVERFLOW);
    const unsigned int period = params.vDeployments[Consensus::DEPLOYMENT_TRANSFER_OVERFLOW].nOverrideMinerConfirmationWindow;
    const unsigned int threshold = params.vDeployments[Consensus::DEPLOYMENT_TRANSFER_OVERFLOW].nOverrideRuleChangeActivationThreshold;
    BOOST_REQUIRE(period > 0);
    BOOST_REQUIRE(threshold > 0);
    BOOST_REQUIRE(threshold <= period);

    SyntheticVersionBitsChain common;
    common.Mine(period, VERSIONBITS_TOP_BITS);

    SyntheticVersionBitsChain activeBranch(common.Tip());
    activeBranch.Mine(threshold, VERSIONBITS_TOP_BITS | overflowMask);
    activeBranch.Mine(period - threshold, VERSIONBITS_TOP_BITS);
    activeBranch.Mine(period, VERSIONBITS_TOP_BITS);

    SyntheticVersionBitsChain startedBranch(common.Tip());
    startedBranch.Mine(2 * period, VERSIONBITS_TOP_BITS);

    CBlockIndex* originalTip = nullptr;
    {
        LOCK(cs_main);
        originalTip = chainActive.Tip();
        // Synthetic indices from earlier tests may have reused addresses.
        versionbitscache.Clear();
        chainActive.SetTip(activeBranch.Tip());
    }
    BOOST_REQUIRE(IsTransferOverflowCheckDeployed());

    {
        LOCK(cs_main);
        chainActive.SetTip(startedBranch.Tip());
    }
    BOOST_CHECK(!IsTransferOverflowCheckDeployed());

    {
        LOCK(cs_main);
        chainActive.SetTip(originalTip);
        versionbitscache.Clear();
    }
}

BOOST_FIXTURE_TEST_CASE(asset_deployment_queries_rewind_after_active_tip, TransferOverflowRegtestSetup)
{
    struct DeploymentQuery {
        Consensus::DeploymentPos deployment;
        bool (*query)();
        const char* name;
    };
    const DeploymentQuery queries[] = {
        {Consensus::DEPLOYMENT_ASSETS, AreAssetsDeployed, "assets"},
        {Consensus::DEPLOYMENT_MSG_REST_ASSETS, IsRip5Active, "restricted assets"},
        {Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE, AreTransferScriptsSizeDeployed, "transfer script size"},
        {Consensus::DEPLOYMENT_ENFORCE_VALUE, AreEnforcedValuesDeployed, "enforced values"},
        {Consensus::DEPLOYMENT_COINBASE_ASSETS, AreCoinbaseCheckAssetsDeployed, "coinbase assets"},
    };
    const Consensus::Params& params = GetParams().GetConsensus();

    for (const DeploymentQuery& check : queries) {
        const auto& deployment = params.vDeployments[check.deployment];
        const unsigned int period = deployment.nOverrideMinerConfirmationWindow;
        const unsigned int threshold = deployment.nOverrideRuleChangeActivationThreshold;
        BOOST_REQUIRE(period > 0);
        BOOST_REQUIRE(threshold > 0);
        BOOST_REQUIRE(threshold <= period);

        SyntheticVersionBitsChain common;
        common.Mine(period, VERSIONBITS_TOP_BITS);

        SyntheticVersionBitsChain lockedBranch(common.Tip());
        lockedBranch.Mine(threshold, VERSIONBITS_TOP_BITS | VersionBitsMask(params, check.deployment));
        lockedBranch.Mine(period - threshold, VERSIONBITS_TOP_BITS);

        SyntheticVersionBitsChain activeBranch(lockedBranch.Tip());
        activeBranch.Mine(period - 1, VERSIONBITS_TOP_BITS);
        CBlockIndex* beforeActivation = activeBranch.Tip();
        activeBranch.Mine(1, VERSIONBITS_TOP_BITS);
        CBlockIndex* activationParent = activeBranch.Tip();
        activeBranch.Mine(2, VERSIONBITS_TOP_BITS);
        CBlockIndex* afterActivation = activeBranch.Tip();

        SyntheticVersionBitsChain startedBranch(common.Tip());
        startedBranch.Mine(2 * period, VERSIONBITS_TOP_BITS);

        VersionBitsCache expectedCache;
        BOOST_REQUIRE_EQUAL(VersionBitsState(lockedBranch.Tip(), params, check.deployment, expectedCache), THRESHOLD_LOCKED_IN);
        BOOST_REQUIRE_EQUAL(VersionBitsState(beforeActivation, params, check.deployment, expectedCache), THRESHOLD_LOCKED_IN);
        BOOST_REQUIRE_EQUAL(VersionBitsState(activationParent, params, check.deployment, expectedCache), THRESHOLD_ACTIVE);
        BOOST_REQUIRE_EQUAL(VersionBitsState(afterActivation, params, check.deployment, expectedCache), THRESHOLD_ACTIVE);
        BOOST_CHECK_EQUAL(VersionBitsStateSinceHeight(activationParent, params, check.deployment, expectedCache),
                          3 * period);
        BOOST_REQUIRE_EQUAL(VersionBitsState(startedBranch.Tip(), params, check.deployment, expectedCache), THRESHOLD_STARTED);

        SetAssetsDeployed(false);
        SetEnforcedValues(false);
        SetEnforcedCoinbase(false);
        CBlockIndex* originalTip = nullptr;
        {
            LOCK(cs_main);
            originalTip = chainActive.Tip();
            versionbitscache.Clear();
            chainActive.SetTip(beforeActivation);
        }
        const bool beforeResult = check.query();

        {
            LOCK(cs_main);
            chainActive.SetTip(activationParent);
        }
        const bool activeResult = check.query();

        {
            LOCK(cs_main);
            chainActive.SetTip(afterActivation);
        }
        const bool laterResult = check.query();

        {
            LOCK(cs_main);
            chainActive.SetTip(startedBranch.Tip());
        }
        const bool rewoundResult = check.query();

        {
            LOCK(cs_main);
            chainActive.SetTip(originalTip);
            versionbitscache.Clear();
        }
        const bool expectedLocked = check.deployment == Consensus::DEPLOYMENT_ENFORCE_VALUE;
        BOOST_CHECK_MESSAGE(beforeResult == expectedLocked, check.name << " has the wrong pre-activation rule");
        BOOST_CHECK_MESSAGE(activeResult, check.name << " did not activate for the first active candidate");
        BOOST_CHECK_MESSAGE(laterResult, check.name << " did not stay active for later candidates");
        BOOST_CHECK_MESSAGE(!rewoundResult, check.name << " remained active after rewind");
    }
}

BOOST_FIXTURE_TEST_CASE(checkblock_asset_result_does_not_depend_on_unrelated_tip, TransferOverflowRegtestSetup)
{
    const Consensus::Params& params = GetParams().GetConsensus();
    const auto& deployment = params.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS];
    const unsigned int period = deployment.nOverrideMinerConfirmationWindow;
    const unsigned int threshold = deployment.nOverrideRuleChangeActivationThreshold;
    BOOST_REQUIRE(period > 0);
    BOOST_REQUIRE(threshold > 0);
    BOOST_REQUIRE(threshold <= period);

    SyntheticVersionBitsChain common;
    common.Mine(period, VERSIONBITS_TOP_BITS);
    SyntheticVersionBitsChain activeBranch(common.Tip());
    activeBranch.Mine(threshold, VERSIONBITS_TOP_BITS | VersionBitsMask(params, Consensus::DEPLOYMENT_COINBASE_ASSETS));
    activeBranch.Mine(period - threshold, VERSIONBITS_TOP_BITS);
    activeBranch.Mine(period, VERSIONBITS_TOP_BITS);
    SyntheticVersionBitsChain startedBranch(common.Tip());
    startedBranch.Mine(2 * period, VERSIONBITS_TOP_BITS);

    CScript assetScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(assetScript);
    BOOST_REQUIRE(assetScript.IsAssetScript());

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << OP_1 << OP_1;
    coinbase.vout.emplace_back(0, assetScript);
    CBlock candidate;
    candidate.vtx.emplace_back(MakeTransactionRef(coinbase));

    SetEnforcedCoinbase(false);
    CBlockIndex* originalTip = nullptr;
    {
        LOCK(cs_main);
        originalTip = chainActive.Tip();
        versionbitscache.Clear();
        chainActive.SetTip(activeBranch.Tip());
    }
    CValidationState activeState;
    const bool activeResult = CheckBlock(candidate, activeState, params, false, false);

    {
        LOCK(cs_main);
        chainActive.SetTip(startedBranch.Tip());
    }
    CValidationState startedState;
    const bool startedResult = CheckBlock(candidate, startedState, params, false, false);

    {
        LOCK(cs_main);
        chainActive.SetTip(originalTip);
        versionbitscache.Clear();
    }
    BOOST_CHECK_MESSAGE(activeResult, activeState.GetRejectReason());
    BOOST_CHECK_MESSAGE(startedResult, startedState.GetRejectReason());
}

BOOST_FIXTURE_TEST_CASE(transfer_payload_uses_explicit_candidate_context, TransferOverflowRegtestSetup)
{
    CScript assetScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    CAssetTransfer("LONGASSETNAME12345678901234567", COIN, std::string(32, 'x')).ConstructTransaction(assetScript);
    BOOST_REQUIRE(assetScript.IsAssetScript());

    CMutableTransaction transfer;
    transfer.vin.resize(1);
    transfer.vin[0].prevout.n = 0;
    transfer.vout.emplace_back(0, assetScript);
    const CTransaction tx(transfer);

    const TxAssetDeploymentContext before{false, false, false};
    const TxAssetDeploymentContext after{true, false, false};
    CValidationState beforeState;
    CValidationState afterState;
    CValidationState structuralState;
    BOOST_CHECK(CheckTransaction(tx, structuralState, true, false, true, nullptr, true));
    BOOST_CHECK(!CheckTransaction(tx, beforeState, true, false, true, &before));
    BOOST_CHECK_EQUAL(beforeState.GetRejectReason(), "bad-txns-transfer-asset-bad-deserialize");
    BOOST_CHECK(CheckTransaction(tx, afterState, true, false, true, &after));
}

BOOST_AUTO_TEST_SUITE_END()
