// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unit tests for denial-of-service detection/prevention code

#include "chainparams.h"
#include "consensus/validation.h"
#include "crypto/mldsa.h"
#include "hash.h"
#include "keystore.h"
#include "net.h"
#include "net_processing.h"
#include "policy/policy.h"
#include "pow.h"
#include "script/sign.h"
#include "serialize.h"
#include "util.h"
#include "validation.h"

#include "test/test_raven.h"

#include <stdint.h>

#include <boost/test/unit_test.hpp>

// Tests these internal-to-net_processing.cpp methods:
extern bool AddOrphanTx(const CTransactionRef &tx, NodeId peer);

extern void EraseOrphansFor(NodeId peer);

extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);

struct COrphanTx
{
    CTransactionRef tx;
    NodeId fromPeer;
    int64_t nTimeExpire;
};
extern std::map<uint256, COrphanTx> mapOrphanTransactions;

CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), GetParams().GetDefaultPort());
}

static NodeId id = 0;

BOOST_FIXTURE_TEST_SUITE(DoS_tests, TestingSetup)

// Test eviction of an outbound peer whose chain never advances
// Mock a node connection, and use mocktime to simulate a peer
// which never sends any headers messages.  PeerLogic should
// decide to evict that outbound peer, after the appropriate timeouts.
// Note that we protect 4 outbound nodes from being subject to
// this logic; this test takes advantage of that protection only
// being applied to nodes which send headers with sufficient
// work.
    BOOST_AUTO_TEST_CASE(outbound_slow_chain_eviction_test)
    {
        BOOST_TEST_MESSAGE("Running Outbound Slow Chain Eviction Test");

        std::atomic<bool> interruptDummy(false);
        CNetMessageBuffer recvBuffer(MAX_PROTOCOL_MESSAGE_LENGTH);

        // Mock an outbound peer
        CAddress addr1(ip(0xa0b0c001), NODE_NONE);
        CNode dummyNode1(id++, ServiceFlags(NODE_NETWORK | NODE_WITNESS), 0, INVALID_SOCKET, addr1, 0, 0, CAddress(), recvBuffer, "", /*fInboundIn=*/ false);
        dummyNode1.SetSendVersion(PROTOCOL_VERSION);

        peerLogic->InitializeNode(&dummyNode1);
        dummyNode1.nVersion = 1;
        dummyNode1.fSuccessfullyConnected = true;

        // This test requires that we have a chain with non-zero work.
        BOOST_CHECK(chainActive.Tip() != nullptr);
        BOOST_CHECK(chainActive.Tip()->nChainWork > 0);

        // Test starts here
        peerLogic->SendMessages(&dummyNode1, interruptDummy); // should result in getheaders
        BOOST_CHECK(dummyNode1.vSendMsg.size() > 0);
        dummyNode1.vSendMsg.clear();

        int64_t nStartTime = GetTime();
        // Wait 21 minutes
        SetMockTime(nStartTime + 21 * 60);
        peerLogic->SendMessages(&dummyNode1, interruptDummy); // should result in getheaders
        BOOST_CHECK(dummyNode1.vSendMsg.size() > 0);
        // Wait 3 more minutes
        SetMockTime(nStartTime + 24 * 60);
        peerLogic->SendMessages(&dummyNode1, interruptDummy); // should result in disconnect
        BOOST_CHECK(dummyNode1.fDisconnect == true);
        SetMockTime(0);

        bool dummy;
        peerLogic->FinalizeNode(dummyNode1.GetId(), dummy);
    }

    BOOST_AUTO_TEST_CASE(DoS_banning_test)
    {
        BOOST_TEST_MESSAGE("Running DoS Banning Test");

        std::atomic<bool> interruptDummy(false);
        CNetMessageBuffer recvBuffer(MAX_PROTOCOL_MESSAGE_LENGTH);

        connman->ClearBanned();
        CAddress addr1(ip(0xa0b0c001), NODE_NONE);
        CNode dummyNode1(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr1, 0, 0, CAddress(), recvBuffer, "", true);
        dummyNode1.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&dummyNode1);
        dummyNode1.nVersion = 1;
        dummyNode1.fSuccessfullyConnected = true;
        Misbehaving(dummyNode1.GetId(), 100); // Should get banned
        peerLogic->SendMessages(&dummyNode1, interruptDummy);
        BOOST_CHECK(connman->IsBanned(addr1));
        BOOST_CHECK(!connman->IsBanned(ip(0xa0b0c001 | 0x0000ff00))); // Different IP, not banned

        CAddress addr2(ip(0xa0b0c002), NODE_NONE);
        CNode dummyNode2(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr2, 1, 1, CAddress(), recvBuffer, "", true);
        dummyNode2.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&dummyNode2);
        dummyNode2.nVersion = 1;
        dummyNode2.fSuccessfullyConnected = true;
        Misbehaving(dummyNode2.GetId(), 50);
        peerLogic->SendMessages(&dummyNode2, interruptDummy);
        BOOST_CHECK(!connman->IsBanned(addr2)); // 2 not banned yet...
        BOOST_CHECK(connman->IsBanned(addr1));  // ... but 1 still should be
        Misbehaving(dummyNode2.GetId(), 50);
        peerLogic->SendMessages(&dummyNode2, interruptDummy);
        BOOST_CHECK(connman->IsBanned(addr2));

        bool dummy;
        peerLogic->FinalizeNode(dummyNode1.GetId(), dummy);
        peerLogic->FinalizeNode(dummyNode2.GetId(), dummy);
    }

    BOOST_AUTO_TEST_CASE(DoS_banscore_test)
    {
        BOOST_TEST_MESSAGE("Running DoS Banscore Test Test");

        std::atomic<bool> interruptDummy(false);
        CNetMessageBuffer recvBuffer(MAX_PROTOCOL_MESSAGE_LENGTH);

        connman->ClearBanned();
        gArgs.ForceSetArg("-banscore", "111"); // because 11 is my favorite number
        CAddress addr1(ip(0xa0b0c001), NODE_NONE);
        CNode dummyNode1(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr1, 3, 1, CAddress(), recvBuffer, "", true);
        dummyNode1.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&dummyNode1);
        dummyNode1.nVersion = 1;
        dummyNode1.fSuccessfullyConnected = true;
        Misbehaving(dummyNode1.GetId(), 100);
        peerLogic->SendMessages(&dummyNode1, interruptDummy);
        BOOST_CHECK(!connman->IsBanned(addr1));
        Misbehaving(dummyNode1.GetId(), 10);
        peerLogic->SendMessages(&dummyNode1, interruptDummy);
        BOOST_CHECK(!connman->IsBanned(addr1));
        Misbehaving(dummyNode1.GetId(), 1);
        peerLogic->SendMessages(&dummyNode1, interruptDummy);
        BOOST_CHECK(connman->IsBanned(addr1));
        gArgs.ForceSetArg("-banscore", std::to_string(DEFAULT_BANSCORE_THRESHOLD));

        bool dummy;
        peerLogic->FinalizeNode(dummyNode1.GetId(), dummy);
    }

    BOOST_AUTO_TEST_CASE(DoS_bantime_test)
    {
        BOOST_TEST_MESSAGE("Running DoS Bantime Test");

        std::atomic<bool> interruptDummy(false);
        CNetMessageBuffer recvBuffer(MAX_PROTOCOL_MESSAGE_LENGTH);

        connman->ClearBanned();
        int64_t nStartTime = GetTime();
        SetMockTime(nStartTime); // Overrides future calls to GetTime()

        CAddress addr(ip(0xa0b0c001), NODE_NONE);
        CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr, 4, 4, CAddress(), recvBuffer, "", true);
        dummyNode.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&dummyNode);
        dummyNode.nVersion = 1;
        dummyNode.fSuccessfullyConnected = true;

        Misbehaving(dummyNode.GetId(), 100);
        peerLogic->SendMessages(&dummyNode, interruptDummy);
        BOOST_CHECK(connman->IsBanned(addr));

        SetMockTime(nStartTime + 60 * 60);
        BOOST_CHECK(connman->IsBanned(addr));

        SetMockTime(nStartTime + 60 * 60 * 24 + 1);
        BOOST_CHECK(!connman->IsBanned(addr));

        bool dummy;
        peerLogic->FinalizeNode(dummyNode.GetId(), dummy);
    }

    BOOST_AUTO_TEST_CASE(unexpected_blocktxn_is_rejected_before_body_parse)
    {
        std::atomic<bool> interruptDummy(false);
        CNetMessageBuffer recvBuffer(MAX_PROTOCOL_MESSAGE_LENGTH);
        CAddress address(ip(0xa0b0c003), NODE_NONE);
        CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, address, 0, 0,
                        CAddress(), recvBuffer, "", true);
        dummyNode.SetSendVersion(PROTOCOL_VERSION);
        dummyNode.SetRecvVersion(PROTOCOL_VERSION);
        dummyNode.nVersion = PROTOCOL_VERSION;
        dummyNode.fSuccessfullyConnected = true;
        peerLogic->InitializeNode(&dummyNode);

        // Deliberately omit the transaction-count field. An unexpected hash
        // must be discarded after its fixed-width preflight; attempting to
        // deserialize even the count would emit a malformed-message reject.
        CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
        const uint256 unexpectedHash = InsecureRand256();
        payload << unexpectedHash;
        CMessageHeader header(GetParams().MessageStart(), NetMsgType::BLOCKTXN,
                              payload.size());
        const uint256 payloadHash = Hash(payload.begin(), payload.end());
        memcpy(header.pchChecksum, payloadHash.begin(),
               CMessageHeader::CHECKSUM_SIZE);
        CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
        wire << header;
        wire += payload;

        bool complete = false;
        BOOST_REQUIRE(dummyNode.ReceiveMsgBytes(
            wire.data(), static_cast<unsigned int>(wire.size()), complete));
        BOOST_REQUIRE(complete);
        BOOST_REQUIRE(dummyNode.MoveCompletedMessagesToProcessQueue(
            MAX_PROTOCOL_MESSAGE_LENGTH));
        const size_t sendMessagesBefore = dummyNode.vSendMsg.size();
        BOOST_CHECK(!peerLogic->ProcessMessages(&dummyNode, interruptDummy));
        BOOST_CHECK_EQUAL(dummyNode.vSendMsg.size(), sendMessagesBefore);
        BOOST_CHECK(!dummyNode.fDisconnect);

        bool updateConnectionTime = false;
        peerLogic->FinalizeNode(dummyNode.GetId(), updateConnectionTime);
    }

    BOOST_AUTO_TEST_CASE(process_messages_releases_receive_queue_ownership)
    {
        std::atomic<bool> interruptDummy(false);
        CNetMessageBuffer recvBuffer(MAX_PROTOCOL_MESSAGE_LENGTH);
        CAddress address(ip(0xa0b0c004), NODE_NONE);
        CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, address, 0, 0,
                        CAddress(), recvBuffer, "", true);
        dummyNode.SetSendVersion(PROTOCOL_VERSION);
        dummyNode.SetRecvVersion(PROTOCOL_VERSION);
        dummyNode.nVersion = PROTOCOL_VERSION;
        dummyNode.fSuccessfullyConnected = true;
        peerLogic->InitializeNode(&dummyNode);

        for (uint64_t nonce = 1; nonce <= 2; ++nonce) {
            CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
            payload << nonce;
            CMessageHeader header(GetParams().MessageStart(), NetMsgType::PONG,
                                  payload.size());
            const uint256 payloadHash = Hash(payload.begin(), payload.end());
            memcpy(header.pchChecksum, payloadHash.begin(),
                   CMessageHeader::CHECKSUM_SIZE);
            CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
            wire << header;
            wire += payload;

            bool complete = false;
            BOOST_REQUIRE(dummyNode.ReceiveMsgBytes(
                wire.data(), static_cast<unsigned int>(wire.size()), complete));
            BOOST_REQUIRE(complete);
        }

        BOOST_REQUIRE(dummyNode.MoveCompletedMessagesToProcessQueue(0));
        size_t firstUsage = 0;
        size_t secondUsage = 0;
        {
            LOCK(dummyNode.cs_vProcessMsg);
            BOOST_REQUIRE_EQUAL(dummyNode.vProcessMsg.size(), 2U);
            auto it = dummyNode.vProcessMsg.begin();
            firstUsage = it++->GetMemoryUsage();
            secondUsage = it->GetMemoryUsage();
            BOOST_CHECK_EQUAL(dummyNode.nProcessQueueSize,
                              firstUsage + secondUsage);
        }
        BOOST_CHECK_EQUAL(recvBuffer.SizeForOwner(dummyNode.GetId()),
                          firstUsage + secondUsage);
        BOOST_CHECK(dummyNode.fPauseRecv);

        BOOST_CHECK(peerLogic->ProcessMessages(&dummyNode, interruptDummy));
        {
            LOCK(dummyNode.cs_vProcessMsg);
            BOOST_CHECK_EQUAL(dummyNode.vProcessMsg.size(), 1U);
            BOOST_CHECK_EQUAL(dummyNode.nProcessQueueSize, secondUsage);
        }
        BOOST_CHECK_EQUAL(recvBuffer.SizeForOwner(dummyNode.GetId()), secondUsage);
        BOOST_CHECK(dummyNode.fPauseRecv);

        BOOST_CHECK(!peerLogic->ProcessMessages(&dummyNode, interruptDummy));
        {
            LOCK(dummyNode.cs_vProcessMsg);
            BOOST_CHECK(dummyNode.vProcessMsg.empty());
            BOOST_CHECK_EQUAL(dummyNode.nProcessQueueSize, 0U);
        }
        BOOST_CHECK_EQUAL(recvBuffer.SizeForOwner(dummyNode.GetId()), 0U);
        BOOST_CHECK_EQUAL(recvBuffer.Size(), 0U);
        BOOST_CHECK(!dummyNode.fPauseRecv);
        BOOST_CHECK(!dummyNode.fDisconnect);

        bool updateConnectionTime = false;
        peerLogic->FinalizeNode(dummyNode.GetId(), updateConnectionTime);
    }

    CTransactionRef RandomOrphan()
    {
        std::map<uint256, COrphanTx>::iterator it;
        it = mapOrphanTransactions.lower_bound(InsecureRand256());
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        return it->second.tx;
    }

    BOOST_AUTO_TEST_CASE(DoS_maporphans_test)
    {
        BOOST_TEST_MESSAGE("Running DoS MapOrphans Test");

        CKey key;
        key.MakeNewKey(true);
        CBasicKeyStore keystore;
        keystore.AddKey(key);

        // 50 orphan transactions:
        for (int i = 0; i < 50; i++)
        {
            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vin[0].prevout.n = 0;
            tx.vin[0].prevout.hash = InsecureRand256();
            tx.vin[0].scriptSig << OP_1;
            tx.vout.resize(1);
            tx.vout[0].nValue = 1 * CENT;
            tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

            AddOrphanTx(MakeTransactionRef(tx), i);
        }

        // ... and 50 that depend on other orphans:
        for (int i = 0; i < 50; i++)
        {
            CTransactionRef txPrev = RandomOrphan();

            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vin[0].prevout.n = 0;
            tx.vin[0].prevout.hash = txPrev->GetHash();
            tx.vout.resize(1);
            tx.vout[0].nValue = 1 * CENT;
            tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
            SignSignature(keystore, *txPrev, tx, 0, SIGHASH_ALL);

            AddOrphanTx(MakeTransactionRef(tx), i);
        }

        // This really-big orphan should be ignored:
        for (int i = 0; i < 10; i++)
        {
            CTransactionRef txPrev = RandomOrphan();

            CMutableTransaction tx;
            tx.vout.resize(1);
            tx.vout[0].nValue = 1 * CENT;
            tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
            tx.vin.resize(2777);
            for (unsigned int j = 0; j < tx.vin.size(); j++)
            {
                tx.vin[j].prevout.n = j;
                tx.vin[j].prevout.hash = txPrev->GetHash();
            }
            SignSignature(keystore, *txPrev, tx, 0, SIGHASH_ALL);
            // Re-use same signature for other inputs
            // (they don't have to be valid for this test)
            for (unsigned int j = 1; j < tx.vin.size(); j++)
                tx.vin[j].scriptSig = tx.vin[0].scriptSig;

            BOOST_CHECK(!AddOrphanTx(MakeTransactionRef(tx), i));
        }

        // Test EraseOrphansFor:
        for (NodeId i = 0; i < 3; i++)
        {
            size_t sizeBefore = mapOrphanTransactions.size();
            EraseOrphansFor(i);
            BOOST_CHECK(mapOrphanTransactions.size() < sizeBefore);
        }

        // Test LimitOrphanTxSize() function:
        LimitOrphanTxSize(40);
        BOOST_CHECK(mapOrphanTransactions.size() <= 40);
        LimitOrphanTxSize(10);
        BOOST_CHECK(mapOrphanTransactions.size() <= 10);
        LimitOrphanTxSize(0);
        BOOST_CHECK(mapOrphanTransactions.empty());
    }

    BOOST_AUTO_TEST_CASE(orphan_pq_shape_uses_raw_size_limit)
    {
        LimitOrphanTxSize(0);
        BOOST_REQUIRE(mapOrphanTransactions.empty());

        // Keep a full default-sized pool of ordinary small orphans. This
        // independently checks the documented aggregate payload bound.
        for (unsigned int i = 0; i < 100; ++i)
        {
            CMutableTransaction small;
            small.vin.resize(1);
            small.vin[0].prevout = COutPoint(InsecureRand256(), i);
            small.vin[0].scriptSig << OP_1;
            small.vout.resize(1);
            small.vout[0].nValue = CENT;
            small.vout[0].scriptPubKey << OP_TRUE;
            BOOST_REQUIRE(AddOrphanTx(MakeTransactionRef(small), i));
        }

        size_t retainedRawBytes = 0;
        size_t retainedPrevoutReferences = 0;
        for (const auto& orphan : mapOrphanTransactions)
        {
            retainedRawBytes += ::GetSerializeSize(*orphan.second.tx, SER_NETWORK, PROTOCOL_VERSION);
            retainedPrevoutReferences += orphan.second.tx->vin.size();
        }
        BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 100U);
        BOOST_CHECK_LT(retainedRawBytes,
                       100U * (MAX_STANDARD_TX_WEIGHT / WITNESS_SCALE_FACTOR));
        BOOST_CHECK_EQUAL(retainedPrevoutReferences, 100U);

        // Every witness has the ML-DSA-44 shape, but none of the inputs has a
        // known witness-v2 prevout. The structural RIP-25 weight discount
        // makes this ~741-kB transaction appear smaller than 400 kWU.
        CMutableTransaction shaped;
        shaped.vin.resize(196);
        shaped.vout.resize(1);
        shaped.vout[0].nValue = CENT;
        shaped.vout[0].scriptPubKey << OP_TRUE;
        for (unsigned int i = 0; i < shaped.vin.size(); ++i)
        {
            shaped.vin[i].prevout = COutPoint(InsecureRand256(), i);
            shaped.vin[i].scriptWitness.stack.emplace_back(mldsa::SIGNATURE_BYTES, 0x11);
            shaped.vin[i].scriptWitness.stack.emplace_back(mldsa::PUBLICKEY_BYTES, 0x22);
        }

        const CTransactionRef attack = MakeTransactionRef(shaped);
        const size_t attackRawBytes = ::GetSerializeSize(*attack, SER_NETWORK, PROTOCOL_VERSION);
        BOOST_REQUIRE_LT(GetTransactionWeight(*attack), MAX_STANDARD_TX_WEIGHT);
        BOOST_REQUIRE_GE(attackRawBytes,
                         MAX_STANDARD_TX_WEIGHT / WITNESS_SCALE_FACTOR);

        BOOST_CHECK(!AddOrphanTx(attack, 101));
        BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 100U);

        size_t finalRawBytes = 0;
        size_t finalPrevoutReferences = 0;
        for (const auto& orphan : mapOrphanTransactions)
        {
            finalRawBytes += ::GetSerializeSize(*orphan.second.tx, SER_NETWORK, PROTOCOL_VERSION);
            finalPrevoutReferences += orphan.second.tx->vin.size();
        }
        BOOST_CHECK_EQUAL(finalRawBytes, retainedRawBytes);
        BOOST_CHECK_EQUAL(finalPrevoutReferences, retainedPrevoutReferences);

        LimitOrphanTxSize(0);
        BOOST_CHECK(mapOrphanTransactions.empty());
    }

BOOST_AUTO_TEST_SUITE_END()
