// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "addrman.h"
#include "test/test_raven.h"
#include <string>
#include <boost/test/unit_test.hpp>
#include "hash.h"
#include "serialize.h"
#include "streams.h"
#include "net.h"
#include "netbase.h"
#include "chainparams.h"
#include "util.h"

#include <atomic>
#include <thread>

static std::unique_ptr<CNode> MakeTestNode(NodeId id, CNetMessageBuffer& recvBuffer, bool fInbound = true)
{
    return std::unique_ptr<CNode>(new CNode(id, NODE_NETWORK, 0, INVALID_SOCKET,
                                            CAddress(), 0, 0, CAddress(), recvBuffer,
                                            "", fInbound));
}

static void ReceiveHeader(CNode& node, unsigned int nMessageSize)
{
    CDataStream header(SER_NETWORK, INIT_PROTO_VERSION);
    header << CMessageHeader(GetParams().MessageStart(), NetMsgType::BLOCK, nMessageSize);
    bool complete = false;
    BOOST_REQUIRE(node.ReceiveMsgBytes(header.data(), static_cast<unsigned int>(header.size()), complete));
    BOOST_CHECK(!complete);
}

static bool ReceivePayload(CNode& node, size_t nBytes, size_t nChunkSize, bool& complete)
{
    std::vector<char> chunk(nChunkSize, 0);
    complete = false;
    while (nBytes != 0) {
        const size_t nNow = std::min(nBytes, chunk.size());
        if (!node.ReceiveMsgBytes(chunk.data(), static_cast<unsigned int>(nNow), complete)) {
            return false;
        }
        nBytes -= nNow;
    }
    return true;
}

static bool ReceiveEmptyMessage(CNode& node, bool& complete)
{
    CDataStream header(SER_NETWORK, INIT_PROTO_VERSION);
    CMessageHeader message(GetParams().MessageStart(), NetMsgType::VERACK, 0);
    CDataStream emptyPayload(SER_NETWORK, INIT_PROTO_VERSION);
    const uint256 payloadHash = Hash(emptyPayload.begin(), emptyPayload.end());
    memcpy(message.pchChecksum, payloadHash.begin(), CMessageHeader::CHECKSUM_SIZE);
    header << message;
    complete = false;
    return node.ReceiveMsgBytes(header.data(), static_cast<unsigned int>(header.size()), complete);
}

static void ClearProcessQueue(CNode& node)
{
    LOCK(node.cs_vProcessMsg);
    node.vProcessMsg.clear();
    node.nProcessQueueSize = 0;
    node.fPauseRecv = false;
}

class CAddrManSerializationMock : public CAddrMan
{
public:
    virtual void Serialize(CDataStream &s) const = 0;

    //! Ensure that bucket placement is always the same for testing purposes.
    void MakeDeterministic()
    {
        nKey.SetNull();
        insecure_rand = FastRandomContext(true);
    }
};

class CAddrManUncorrupted : public CAddrManSerializationMock
{
public:
    void Serialize(CDataStream &s) const override
    {
        CAddrMan::Serialize(s);
    }
};

class CAddrManCorrupted : public CAddrManSerializationMock
{
public:
    void Serialize(CDataStream &s) const override
    {
        // Produces corrupt output that claims addrman has 20 addrs when it only has one addr.
        unsigned char nVersion = 1;
        s << nVersion;
        s << ((unsigned char) 32);
        s << nKey;
        s << 10; // nNew
        s << 10; // nTried

        int nUBuckets = ADDRMAN_NEW_BUCKET_COUNT ^(1 << 30);
        s << nUBuckets;

        CService serv;
        Lookup("252.1.1.1", serv, 7777, false);
        CAddress addr = CAddress(serv, NODE_NONE);
        CNetAddr resolved;
        LookupHost("252.2.2.2", resolved, false);
        CAddrInfo info = CAddrInfo(addr, resolved);
        s << info;
    }
};

CDataStream AddrmanToStream(CAddrManSerializationMock &_addrman)
{
    CDataStream ssPeersIn(SER_DISK, CLIENT_VERSION);
    ssPeersIn << FLATDATA(GetParams().MessageStart());
    ssPeersIn << _addrman;
    std::string str = ssPeersIn.str();
    std::vector<unsigned char> vchData(str.begin(), str.end());
    return CDataStream(vchData, SER_DISK, CLIENT_VERSION);
}

BOOST_FIXTURE_TEST_SUITE(net_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(cnode_listen_port_test)
    {
        BOOST_TEST_MESSAGE("Running cNode Listen Port Test");

        // test default
        unsigned short port = GetListenPort();
        BOOST_CHECK(port == GetParams().GetDefaultPort());
        // test set port
        unsigned short altPort = 12345;
        gArgs.SoftSetArg("-port", std::to_string(altPort));
        port = GetListenPort();
        BOOST_CHECK(port == altPort);
    }

    BOOST_AUTO_TEST_CASE(caddrdb_read_test)
    {
        BOOST_TEST_MESSAGE("Running cAddrDB Read Test");

        CAddrManUncorrupted addrmanUncorrupted;
        addrmanUncorrupted.MakeDeterministic();

        CService addr1, addr2, addr3;
        Lookup("250.7.1.1", addr1, 8767, false);
        Lookup("250.7.2.2", addr2, 9999, false);
        Lookup("250.7.3.3", addr3, 9999, false);

        // Add three addresses to new table.
        CService source;
        Lookup("252.5.1.1", source, 8767, false);
        addrmanUncorrupted.Add(CAddress(addr1, NODE_NONE), source);
        addrmanUncorrupted.Add(CAddress(addr2, NODE_NONE), source);
        addrmanUncorrupted.Add(CAddress(addr3, NODE_NONE), source);

        // Test that the de-serialization does not throw an exception.
        CDataStream ssPeers1 = AddrmanToStream(addrmanUncorrupted);
        bool exceptionThrown = false;
        CAddrMan addrman1;

        BOOST_CHECK(addrman1.size() == 0);
        try
        {
            unsigned char pchMsgTmp[4];
            ssPeers1 >> FLATDATA(pchMsgTmp);
            ssPeers1 >> addrman1;
        } catch (const std::exception &e)
        {
            exceptionThrown = true;
        }

        BOOST_CHECK(addrman1.size() == 3);
        BOOST_CHECK(exceptionThrown == false);

        // Test that CAddrDB::Read creates an addrman with the correct number of addrs.
        CDataStream ssPeers2 = AddrmanToStream(addrmanUncorrupted);

        CAddrMan addrman2;
        CAddrDB adb;
        BOOST_CHECK(addrman2.size() == 0);
        adb.Read(addrman2, ssPeers2);
        BOOST_CHECK(addrman2.size() == 3);
    }


    BOOST_AUTO_TEST_CASE(caddrdb_read_corrupted_test)
    {
        BOOST_TEST_MESSAGE("Running cAddrDB Read Corrupted Test");

        CAddrManCorrupted addrmanCorrupted;
        addrmanCorrupted.MakeDeterministic();

        // Test that the de-serialization of corrupted addrman throws an exception.
        CDataStream ssPeers1 = AddrmanToStream(addrmanCorrupted);
        bool exceptionThrown = false;
        CAddrMan addrman1;
        BOOST_CHECK(addrman1.size() == 0);
        try
        {
            unsigned char pchMsgTmp[4];
            ssPeers1 >> FLATDATA(pchMsgTmp);
            ssPeers1 >> addrman1;
        } catch (const std::exception &e)
        {
            exceptionThrown = true;
        }
        // Even through de-serialization failed addrman is not left in a clean state.
        BOOST_CHECK(addrman1.size() == 1);
        BOOST_CHECK(exceptionThrown);

        // Test that CAddrDB::Read leaves addrman in a clean state if de-serialization fails.
        CDataStream ssPeers2 = AddrmanToStream(addrmanCorrupted);

        CAddrMan addrman2;
        CAddrDB adb;
        BOOST_CHECK(addrman2.size() == 0);
        adb.Read(addrman2, ssPeers2);
        BOOST_CHECK(addrman2.size() == 0);
    }

    BOOST_AUTO_TEST_CASE(cnode_simple_test)
    {
        BOOST_TEST_MESSAGE("Running cNode Simple Test");

        SOCKET hSocket = INVALID_SOCKET;
        NodeId id = 0;
        int height = 0;

        in_addr ipv4Addr;
        ipv4Addr.s_addr = 0xa0b0c001;

        CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
        std::string pszDest = "";
        bool fInboundIn = false;

        // Test that fFeeler is false by default.
        CNetMessageBuffer recvBuffer(MAX_PROTOCOL_MESSAGE_LENGTH);
        std::unique_ptr<CNode> pnode1(new CNode(id++, NODE_NETWORK, height, hSocket, addr, 0, 0, CAddress(), recvBuffer, pszDest, fInboundIn));
        BOOST_CHECK(pnode1->fInbound == false);
        BOOST_CHECK(pnode1->fFeeler == false);

        fInboundIn = true;
        std::unique_ptr<CNode> pnode2(new CNode(id++, NODE_NETWORK, height, hSocket, addr, 1, 1, CAddress(), recvBuffer, pszDest, fInboundIn));
        BOOST_CHECK(pnode2->fInbound == true);
        BOOST_CHECK(pnode2->fFeeler == false);
    }

    BOOST_AUTO_TEST_CASE(incomplete_message_buffer_concurrent_global_limit)
    {
        static const size_t NORMAL_BULK_LIMIT = 512 * 1024;
        static const size_t OWNER_HEADROOM = 64 * 1024;
        CNetMessageBuffer recvBuffer(NORMAL_BULK_LIMIT,
                                     MAX_PROTOCOL_MESSAGE_LENGTH,
                                     OWNER_HEADROOM);

        // Peer A retains an incomplete maximum-sized message and consumes most
        // of the shared inbound bulk pool.
        auto attacker = MakeTestNode(0, recvBuffer, true);
        ReceiveHeader(*attacker, MAX_PROTOCOL_MESSAGE_LENGTH);
        bool attackerComplete = false;
        BOOST_REQUIRE(ReceivePayload(*attacker, 5 * 64 * 1024, 64 * 1024,
                                     attackerComplete));
        BOOST_CHECK(!attackerComplete);
        BOOST_CHECK_GT(recvBuffer.NormalBulkSize(), 3 * NORMAL_BULK_LIMIT / 4);
        const size_t attackerBulkUsage = recvBuffer.NormalBulkSize();

        // An unrelated inbound peer stays within its guaranteed headroom, and
        // a protected outbound peer can concurrently receive a maximum-sized
        // message without contending for A's class-wide pool.
        auto smallInbound = MakeTestNode(1, recvBuffer, true);
        auto protectedOutbound = MakeTestNode(2, recvBuffer, false);
        ReceiveHeader(*smallInbound, 16 * 1024);
        ReceiveHeader(*protectedOutbound, MAX_PROTOCOL_MESSAGE_LENGTH);
        std::atomic<bool> start(false);
        bool smallResult = false;
        bool smallComplete = false;
        bool protectedResult = false;
        bool protectedComplete = false;
        std::thread smallThread([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            smallResult = ReceivePayload(*smallInbound, 16 * 1024, 16 * 1024,
                                         smallComplete);
        });
        std::thread protectedThread([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            protectedResult = ReceivePayload(*protectedOutbound,
                                              MAX_PROTOCOL_MESSAGE_LENGTH,
                                              64 * 1024, protectedComplete);
        });
        start.store(true, std::memory_order_release);
        smallThread.join();
        protectedThread.join();

        BOOST_REQUIRE(smallResult);
        BOOST_CHECK(smallComplete);
        BOOST_REQUIRE(protectedResult);
        BOOST_CHECK(protectedComplete);
        BOOST_CHECK_EQUAL(recvBuffer.NormalBulkSize(), attackerBulkUsage);
        BOOST_CHECK_GT(recvBuffer.ProtectedBulkSize(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.Size(),
                          recvBuffer.SizeForOwner(0) +
                              recvBuffer.SizeForOwner(1) +
                              recvBuffer.SizeForOwner(2));
        BOOST_CHECK_LE(recvBuffer.Size(),
                       2 * NORMAL_BULK_LIMIT +
                           2 * MAX_PROTOCOL_MESSAGE_LENGTH);

        const size_t retainedBeforeSplice = recvBuffer.Size();
        BOOST_REQUIRE(smallInbound->MoveCompletedMessagesToProcessQueue(5 * 1000 * 1000));
        BOOST_REQUIRE(protectedOutbound->MoveCompletedMessagesToProcessQueue(5 * 1000 * 1000));
        BOOST_CHECK_EQUAL(recvBuffer.Size(), retainedBeforeSplice);
        BOOST_CHECK_EQUAL(smallInbound->nProcessQueueSize, recvBuffer.SizeForOwner(1));
        BOOST_CHECK_EQUAL(protectedOutbound->nProcessQueueSize, recvBuffer.SizeForOwner(2));

        ClearProcessQueue(*smallInbound);
        ClearProcessQueue(*protectedOutbound);
        BOOST_CHECK_EQUAL(recvBuffer.SizeForOwner(1), 0);
        BOOST_CHECK_EQUAL(recvBuffer.SizeForOwner(2), 0);
        attacker.reset();
        BOOST_CHECK_EQUAL(recvBuffer.Size(), 0);
    }

    BOOST_AUTO_TEST_CASE(incomplete_message_buffer_releases_reservations)
    {
        static const size_t MESSAGE_SIZE = 300 * 1024;
        CNetMessageBuffer recvBuffer(MESSAGE_SIZE);
        bool complete = false;

        auto completed = MakeTestNode(0, recvBuffer);
        ReceiveHeader(*completed, MESSAGE_SIZE);
        BOOST_REQUIRE(ReceivePayload(*completed, MESSAGE_SIZE, 64 * 1024, complete));
        BOOST_CHECK(complete);
        BOOST_CHECK_GT(recvBuffer.Size(), MESSAGE_SIZE);
        const size_t completedUsage = recvBuffer.Size();
        BOOST_REQUIRE(completed->MoveCompletedMessagesToProcessQueue(MESSAGE_SIZE * 2));
        BOOST_CHECK_EQUAL(completed->nProcessQueueSize, completedUsage);
        BOOST_CHECK_EQUAL(recvBuffer.Size(), completedUsage);
        ClearProcessQueue(*completed);
        BOOST_CHECK_EQUAL(recvBuffer.Size(), 0);

        auto incomplete = MakeTestNode(1, recvBuffer);
        ReceiveHeader(*incomplete, MESSAGE_SIZE);
        BOOST_REQUIRE(ReceivePayload(*incomplete, 1, 1, complete));
        BOOST_CHECK(!complete);
        BOOST_CHECK_GT(recvBuffer.Size(), 0);
        incomplete.reset();
        BOOST_CHECK_EQUAL(recvBuffer.Size(), 0);
    }

    BOOST_AUTO_TEST_CASE(maximum_message_completes_with_global_buffer_limit)
    {
        CNetMessageBuffer recvBuffer(MAX_PROTOCOL_MESSAGE_LENGTH);
        auto node = MakeTestNode(0, recvBuffer);
        ReceiveHeader(*node, MAX_PROTOCOL_MESSAGE_LENGTH);

        bool complete = false;
        BOOST_REQUIRE(ReceivePayload(*node, MAX_PROTOCOL_MESSAGE_LENGTH, 64 * 1024, complete));
        BOOST_CHECK(complete);
        BOOST_CHECK_GT(recvBuffer.Size(), MAX_PROTOCOL_MESSAGE_LENGTH);
        BOOST_REQUIRE(node->MoveCompletedMessagesToProcessQueue(MAX_PROTOCOL_MESSAGE_LENGTH * 2));
        BOOST_CHECK_GT(recvBuffer.Size(), MAX_PROTOCOL_MESSAGE_LENGTH);
        ClearProcessQueue(*node);
        BOOST_CHECK_EQUAL(recvBuffer.Size(), 0);
    }

    BOOST_AUTO_TEST_CASE(header_only_messages_are_globally_accounted)
    {
        static const size_t BUFFER_LIMIT = 64 * 1024;
        CNetMessageBuffer recvBuffer(BUFFER_LIMIT, BUFFER_LIMIT, 0);
        auto node = MakeTestNode(0, recvBuffer, true);

        size_t messageCount = 0;
        size_t oneMessageUsage = 0;
        bool complete = false;
        while (ReceiveEmptyMessage(*node, complete)) {
            BOOST_REQUIRE(complete);
            ++messageCount;
            if (messageCount == 1) {
                oneMessageUsage = recvBuffer.Size();
                BOOST_REQUIRE_GT(oneMessageUsage, CMessageHeader::HEADER_SIZE);
            }
            BOOST_CHECK_EQUAL(recvBuffer.Size(), messageCount * oneMessageUsage);
            BOOST_REQUIRE_LT(messageCount, 1000);
        }

        BOOST_REQUIRE_GT(messageCount, 0);
        BOOST_CHECK_EQUAL(messageCount, BUFFER_LIMIT / oneMessageUsage);
        const size_t retainedBeforeSplice = recvBuffer.Size();
        BOOST_REQUIRE(node->MoveCompletedMessagesToProcessQueue(BUFFER_LIMIT * 2));
        BOOST_CHECK_EQUAL(node->nProcessQueueSize, retainedBeforeSplice);
        BOOST_CHECK_EQUAL(recvBuffer.Size(), retainedBeforeSplice);
        ClearProcessQueue(*node);
        BOOST_CHECK_EQUAL(recvBuffer.Size(), 0);
    }

BOOST_AUTO_TEST_SUITE_END()
