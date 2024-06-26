#include <condition_variable>
#include <thread>

#include <gtest/gtest.h>

#include "SRTNet.h"

std::string kValidPsk = "Th1$_is_4n_0pt10N4L_P$k";
std::string kInvalidPsk = "Th1$_is_4_F4k3_P$k";

size_t kMaxMessageSize = SRT_LIVE_MAX_PLSIZE;

namespace {
///
/// @brief Get the bind IP address and port of an SRT socket
/// @param socket The SRT socket to get the bind IP and Port from
/// @return The bind IP address and port of the SRT socket
std::pair<std::string, uint16_t> getBindIpAndPortFromSRTSocket(SRTSOCKET socket) {
    sockaddr_storage address{};
    int32_t addressSize = sizeof(decltype(address));
    EXPECT_EQ(srt_getsockname(socket, reinterpret_cast<sockaddr*>(&address), &addressSize), 0);
    if (address.ss_family == AF_INET) {
        const sockaddr_in* socketAddressV4 = reinterpret_cast<const sockaddr_in*>(&address);
        char ipv4Address[INET_ADDRSTRLEN];
        EXPECT_NE(inet_ntop(AF_INET, &(socketAddressV4->sin_addr), ipv4Address, INET_ADDRSTRLEN), nullptr);
        return {ipv4Address, ntohs(socketAddressV4->sin_port)};
    } else if (address.ss_family == AF_INET6) {
        const sockaddr_in6* socketAddressV6 = reinterpret_cast<const sockaddr_in6*>(&address);
        char ipv6Address[INET6_ADDRSTRLEN];
        EXPECT_NE(inet_ntop(AF_INET, &(socketAddressV6->sin6_addr), ipv6Address, INET6_ADDRSTRLEN), nullptr);
        return {ipv6Address, ntohs(socketAddressV6->sin6_port)};
    }
    return {"Unsupported", 0};
}

bool waitUntil(const std::function<bool()>& function,
               std::chrono::milliseconds timeout,
               std::chrono::milliseconds sleepFor) {
    std::chrono::milliseconds timeLeft = timeout;
    while (timeLeft > std::chrono::milliseconds(0)) {
        if (function()) {
            return true;
        }

        std::this_thread::sleep_for(sleepFor);
        timeLeft -= sleepFor;
    }
    return function();
}

///
/// @brief Get the remote peer IP address and port of an SRT socket
/// @param socket The SRT socket to get the peer IP and Port from
/// @return The peer IP address and port of the SRT socket
std::pair<std::string, uint16_t> getPeerIpAndPortFromSRTSocket(SRTSOCKET socket) {
    sockaddr_storage address{};
    int32_t addressSize = sizeof(decltype(address));
    EXPECT_EQ(srt_getpeername(socket, reinterpret_cast<sockaddr*>(&address), &addressSize), 0);
    if (address.ss_family == AF_INET) {
        const sockaddr_in* socketAddressV4 = reinterpret_cast<const sockaddr_in*>(&address);
        char ipv4Address[INET_ADDRSTRLEN];
        EXPECT_NE(inet_ntop(AF_INET, &(socketAddressV4->sin_addr), ipv4Address, INET_ADDRSTRLEN), nullptr);
        return {ipv4Address, ntohs(socketAddressV4->sin_port)};
    } else if (address.ss_family == AF_INET6) {
        const sockaddr_in6* socketAddressV6 = reinterpret_cast<const sockaddr_in6*>(&address);
        char ipv6Address[INET6_ADDRSTRLEN];
        EXPECT_NE(inet_ntop(AF_INET, &(socketAddressV6->sin6_addr), ipv6Address, INET6_ADDRSTRLEN), nullptr);
        return {ipv6Address, ntohs(socketAddressV6->sin6_port)};
    }
    return {"Unsupported", 0};
}

class TestSRTFixture : public ::testing::Test {
public:
    void SetUp() override {
        mClientCtx->mObject = 42;

        mConnectionCtx->mObject = 1111;

        // notice when client connects to server
        mServer.clientConnected = [&](struct sockaddr& sin, SRTSOCKET newSocket,
                                      std::shared_ptr<SRTNet::NetworkConnection>& ctx,
                                      const SRTNet::ConnectionInformation& connectionInformation) {
            {
                std::lock_guard<std::mutex> lock(mConnectedMutex);
                mConnected = true;
            }
            mConnectedCondition.notify_one();
            EXPECT_NE(connectionInformation.mPeerSrtVersion, SRTNet::ConnectionInformation().mPeerSrtVersion);
            EXPECT_NE(connectionInformation.mNegotiatedLatency, SRTNet::ConnectionInformation().mNegotiatedLatency);
            return mConnectionCtx;
        };

        mServer.clientDisconnected = [&](std::shared_ptr<SRTNet::NetworkConnection>& ctx, SRTSOCKET lSocket) {
            {
                std::lock_guard<std::mutex> lock(mConnectedMutex);
                mConnected = false;
            }
            mConnectedCondition.notify_one();
            EXPECT_EQ(ctx, mConnectionCtx);
        };

        mClient.connectedToServer = [&](std::shared_ptr<SRTNet::NetworkConnection>& ctx,
                SRTSOCKET newSocket,
                const SRTNet::ConnectionInformation& connectionInformation) {
            EXPECT_NE(connectionInformation.mPeerSrtVersion, SRTNet::ConnectionInformation().mPeerSrtVersion);
            EXPECT_NE(connectionInformation.mNegotiatedLatency, SRTNet::ConnectionInformation().mNegotiatedLatency);
        };
    }

    bool waitForClientToConnect(std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mConnectedMutex);
        bool successfulWait = mConnectedCondition.wait_for(lock, timeout, [&]() { return mConnected; });
        return successfulWait;
    }

    bool waitForClientToDisconnect(std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mConnectedMutex);
        bool successfulWait = mConnectedCondition.wait_for(lock, timeout, [&]() { return !mConnected; });
        return successfulWait;
    }

protected:
    const uint16_t kAnyPort = 0;

    SRTNet mServer;
    SRTNet mClient;

    std::shared_ptr<SRTNet::NetworkConnection> mServerCtx = std::make_shared<SRTNet::NetworkConnection>();
    std::shared_ptr<SRTNet::NetworkConnection> mClientCtx = std::make_shared<SRTNet::NetworkConnection>();
    std::shared_ptr<SRTNet::NetworkConnection> mConnectionCtx = std::make_shared<SRTNet::NetworkConnection>();

    std::condition_variable mConnectedCondition;
    std::mutex mConnectedMutex;
    bool mConnected = false;
};

} // namespace

TEST(TestSrt, StartStop) {
    SRTNet server;
    SRTNet client;

    auto serverCtx = std::make_shared<SRTNet::NetworkConnection>();
    EXPECT_FALSE(
        server.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, serverCtx))
        << "Expect to fail without providing clientConnected callback";
    auto clientCtx = std::make_shared<SRTNet::NetworkConnection>();
    clientCtx->mObject = 42;
    EXPECT_TRUE(client.startClient("127.0.0.1", 8009, 16, 1000, 100, clientCtx, SRT_LIVE_MAX_PLSIZE, false, 5000, kValidPsk))
        << "Expect client to start, but not be able to connect with no server started";
    EXPECT_FALSE(client.isConnectedToServer())
        << "Expect to fail with no server started";
    EXPECT_TRUE(client.stop());

    std::condition_variable connectedCondition;
    std::mutex connectedMutex;
    bool connected = false;

    // notice when client connects to server
    server.clientConnected = [&](struct sockaddr& sin, SRTSOCKET newSocket,
                                 std::shared_ptr<SRTNet::NetworkConnection>& ctx,
                                 const SRTNet::ConnectionInformation&) {
        {
            std::lock_guard<std::mutex> lock(connectedMutex);
            connected = true;
        }
        connectedCondition.notify_one();
        auto connectionCtx = std::make_shared<SRTNet::NetworkConnection>();
        connectionCtx->mObject = 1111;
        return connectionCtx;
    };

    ASSERT_TRUE(
        server.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, serverCtx));
    ASSERT_TRUE(client.startClient("127.0.0.1", 8009, 16, 1000, 100, clientCtx, SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    ASSERT_TRUE(client.isConnectedToServer());

    // check for client connecting
    {
        std::unique_lock<std::mutex> lock(connectedMutex);
        bool successfulWait = connectedCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return connected; });
        ASSERT_TRUE(successfulWait) << "Timeout waiting for client to connect";
    }

    ASSERT_TRUE(waitUntil([&]() { return !server.getActiveClientSockets().empty(); },
        std::chrono::seconds(1), std::chrono::milliseconds(10)));

    const auto activeClients = server.getActiveClients();
    size_t nClients = activeClients.size();
    for (const auto& socketNetworkConnectionPair : activeClients) {
        int32_t number = 0;
        EXPECT_NO_THROW(number = std::any_cast<int32_t>(socketNetworkConnectionPair.second->mObject));
        EXPECT_EQ(number, 1111);
    }
    EXPECT_EQ(nClients, 1);

    auto [srtSocket, networkConnection] = client.getConnectedServer();
    EXPECT_NE(networkConnection, nullptr);
    int32_t number = 0;
    EXPECT_NO_THROW(number = std::any_cast<int32_t>(networkConnection->mObject));
    EXPECT_EQ(number, 42);

    // notice when client disconnects
    std::condition_variable disconnectCondition;
    std::mutex disconnectMutex;
    bool disconnected = false;
    server.clientDisconnected = [&](std::shared_ptr<SRTNet::NetworkConnection>& ctx, SRTSOCKET lSocket) {
        {
            std::lock_guard<std::mutex> lock(disconnectMutex);
            disconnected = true;
        }
        disconnectCondition.notify_one();
    };

    EXPECT_TRUE(client.stop());
    {
        std::unique_lock<std::mutex> lock(disconnectMutex);
        bool successfulWait =
            disconnectCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return disconnected; });
        EXPECT_TRUE(successfulWait) << "Timeout waiting for client disconnect";
    }

    // start a new client and stop the server
    connected = false;
    SRTNet client2;
    ASSERT_TRUE(client2.startClient("127.0.0.1", 8009, 16, 1000, 100, clientCtx, SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    ASSERT_TRUE(client2.isConnectedToServer());
    // check for client connecting
    {
        std::unique_lock<std::mutex> lock(connectedMutex);
        bool successfulWait = connectedCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return connected; });
        ASSERT_TRUE(successfulWait) << "Timeout waiting for client2 to connect";
    }

    disconnected = false;
    EXPECT_TRUE(server.stop());
    {
        std::unique_lock<std::mutex> lock(disconnectMutex);
        bool successfulWait =
            disconnectCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return disconnected; });
        EXPECT_TRUE(successfulWait) << "Timeout waiting for client disconnect";
    }
}

TEST(TestSrt, TestPsk) {
    SRTNet server;
    SRTNet client;

    auto ctx = std::make_shared<SRTNet::NetworkConnection>();
    server.clientConnected = [&](struct sockaddr& sin, SRTSOCKET newSocket,
                                 std::shared_ptr<SRTNet::NetworkConnection>& ctx,
                                 const SRTNet::ConnectionInformation&) { return ctx; };
    ASSERT_TRUE(server.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, ctx));
    EXPECT_FALSE(client.startClient("127.0.0.1", 8009, 16, 1000, 100, ctx, SRT_LIVE_MAX_PLSIZE, false, 5000, kInvalidPsk))
        << "Expect to fail when using incorrect PSK";

    ASSERT_TRUE(server.stop());
    ASSERT_TRUE(server.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, ctx));
    EXPECT_TRUE(client.startClient("127.0.0.1", 8009, 16, 1000, 100, ctx, SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    ASSERT_TRUE(client.isConnectedToServer());

    ASSERT_TRUE(server.stop());
    ASSERT_TRUE(client.stop());
    ASSERT_TRUE(server.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, "", false, ctx));
    EXPECT_TRUE(client.startClient("127.0.0.1", 8009, 16, 1000, 100, ctx, SRT_LIVE_MAX_PLSIZE, true));
    ASSERT_TRUE(client.isConnectedToServer());
}

TEST_F(TestSRTFixture, SendReceive) {
    // start server and client
    ASSERT_TRUE(
        mServer.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, mServerCtx));
    ASSERT_TRUE(mClient.startClient("127.0.0.1", 8009, 16, 1000, 100, mClientCtx, SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    ASSERT_TRUE(mClient.isConnectedToServer());

    std::vector<uint8_t> sendBuffer(1000);
    std::condition_variable serverCondition;
    std::mutex serverMutex;
    bool serverGotData = false;
    SRTSOCKET clientSocket;
    mServer.receivedData = [&](std::unique_ptr<std::vector<uint8_t>>& data, SRT_MSGCTRL& msgCtrl,
                              std::shared_ptr<SRTNet::NetworkConnection>& ctx, SRTSOCKET socket) {
        EXPECT_EQ(ctx, mConnectionCtx);
        EXPECT_EQ(*data, sendBuffer);
        clientSocket = socket;
        {
            std::lock_guard<std::mutex> lock(serverMutex);
            serverGotData = true;
        }
        SRT_MSGCTRL msgSendCtrl = srt_msgctrl_default;
        EXPECT_TRUE(mServer.sendData(data->data(), data->size(), &msgSendCtrl, socket));
        serverCondition.notify_one();
        return true;
    };

    std::condition_variable clientCondition;
    std::mutex clientMutex;
    bool clientGotData = false;
    mClient.receivedData = [&](std::unique_ptr<std::vector<uint8_t>>& data, SRT_MSGCTRL& msgCtrl,
                              std::shared_ptr<SRTNet::NetworkConnection>& ctx, SRTSOCKET socket) {
        EXPECT_EQ(ctx, mClientCtx);
        EXPECT_EQ(*data, sendBuffer);
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            clientGotData = true;
        }
        clientCondition.notify_one();
        return true;
    };

    std::fill(sendBuffer.begin(), sendBuffer.end(), 1);
    SRT_MSGCTRL msgCtrl = srt_msgctrl_default;
    EXPECT_TRUE(mClient.sendData(sendBuffer.data(), sendBuffer.size(), &msgCtrl));

    {
        std::unique_lock<std::mutex> lock(serverMutex);
        bool successfulWait = serverCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return serverGotData; });
        EXPECT_TRUE(successfulWait) << "Timeout waiting for receiving data from client";
    }

    {
        std::unique_lock<std::mutex> lock(clientMutex);
        bool successfulWait = clientCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return clientGotData; });
        EXPECT_TRUE(successfulWait) << "Timeout waiting for receiving data from server";
    }

    SRT_TRACEBSTATS clientStats;
    mClient.getStatistics(&clientStats, SRTNetClearStats::no, SRTNetInstant::yes);
    SRT_TRACEBSTATS serverStats;
    mServer.getStatistics(&serverStats, SRTNetClearStats::no, SRTNetInstant::yes, clientSocket);
    EXPECT_EQ(clientStats.pktSentTotal, 1);
    EXPECT_EQ(clientStats.pktRecvTotal, 1);
    EXPECT_EQ(clientStats.pktSentTotal, serverStats.pktRecvTotal);
    EXPECT_EQ(clientStats.pktRecvTotal, serverStats.pktSentTotal);

    // verify that sending to a stopped client fails
    ASSERT_TRUE(mClient.stop());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    SRT_MSGCTRL msgSendCtrl = srt_msgctrl_default;
    EXPECT_FALSE(mServer.sendData(sendBuffer.data(), sendBuffer.size(), &msgSendCtrl, clientSocket));
}

TEST_F(TestSRTFixture, SendReceiveIPv6) {
    // start server and client
    ASSERT_TRUE(mServer.startServer("::", 8020, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, "", true, mServerCtx));
    ASSERT_TRUE(mClient.startClient("::1", 8020, 16, 1000, 100, mClientCtx, SRT_LIVE_MAX_PLSIZE, true, 5000, ""));
    ASSERT_TRUE(mClient.isConnectedToServer());

    std::vector<uint8_t> sendBuffer(1000);
    std::condition_variable serverCondition;
    std::mutex serverMutex;
    bool serverGotData = false;
    SRTSOCKET clientSocket;
    mServer.receivedData = [&](std::unique_ptr<std::vector<uint8_t>>& data, SRT_MSGCTRL& msgCtrl,
                              std::shared_ptr<SRTNet::NetworkConnection>& ctx, SRTSOCKET socket) {
        EXPECT_EQ(ctx, mConnectionCtx);
        EXPECT_EQ(*data, sendBuffer);
        clientSocket = socket;
        {
            std::lock_guard<std::mutex> lock(serverMutex);
            serverGotData = true;
        }
        SRT_MSGCTRL msgSendCtrl = srt_msgctrl_default;
        EXPECT_TRUE(mServer.sendData(data->data(), data->size(), &msgSendCtrl, socket));
        serverCondition.notify_one();
        return true;
    };

    std::condition_variable clientCondition;
    std::mutex clientMutex;
    bool clientGotData = false;
    mClient.receivedData = [&](std::unique_ptr<std::vector<uint8_t>>& data, SRT_MSGCTRL& msgCtrl,
                              std::shared_ptr<SRTNet::NetworkConnection>& ctx, SRTSOCKET socket) {
        EXPECT_EQ(ctx, mClientCtx);
        EXPECT_EQ(*data, sendBuffer);
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            clientGotData = true;
        }
        clientCondition.notify_one();
        return true;
    };

    std::fill(sendBuffer.begin(), sendBuffer.end(), 1);
    SRT_MSGCTRL msgCtrl = srt_msgctrl_default;
    EXPECT_TRUE(mClient.sendData(sendBuffer.data(), sendBuffer.size(), &msgCtrl));

    {
        std::unique_lock<std::mutex> lock(serverMutex);
        bool successfulWait = serverCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return serverGotData; });
        EXPECT_TRUE(successfulWait) << "Timeout waiting for receiving data from client";
    }

    {
        std::unique_lock<std::mutex> lock(clientMutex);
        bool successfulWait = clientCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return clientGotData; });
        EXPECT_TRUE(successfulWait) << "Timeout waiting for receiving data from server";
    }

    SRT_TRACEBSTATS clientStats;
    mClient.getStatistics(&clientStats, SRTNetClearStats::no, SRTNetInstant::yes);
    SRT_TRACEBSTATS serverStats;
    mServer.getStatistics(&serverStats, SRTNetClearStats::no, SRTNetInstant::yes, clientSocket);
    EXPECT_EQ(clientStats.pktSentTotal, 1);
    EXPECT_EQ(clientStats.pktRecvTotal, 1);
    EXPECT_EQ(clientStats.pktSentTotal, serverStats.pktRecvTotal);
    EXPECT_EQ(clientStats.pktRecvTotal, serverStats.pktSentTotal);

    // verify that sending to a stopped client fails
    ASSERT_TRUE(mClient.stop());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    SRT_MSGCTRL msgSendCtrl = srt_msgctrl_default;
    EXPECT_FALSE(mServer.sendData(sendBuffer.data(), sendBuffer.size(), &msgSendCtrl, clientSocket));
}

TEST_F(TestSRTFixture, LargeMessage) {
    // start server and client
    ASSERT_TRUE(
        mServer.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, mServerCtx));
    ASSERT_TRUE(mClient.startClient("127.0.0.1", 8009, 16, 1000, 100, mClientCtx, SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    ASSERT_TRUE(mClient.isConnectedToServer());

    std::vector<uint8_t> sendBuffer(kMaxMessageSize + 1);
    std::fill(sendBuffer.begin(), sendBuffer.end(), 1);
    SRT_MSGCTRL msgCtrl = srt_msgctrl_default;
    EXPECT_FALSE(mClient.sendData(sendBuffer.data(), sendBuffer.size(), &msgCtrl));
}

// TODO Enable test when STAR-238 is fixed
TEST_F(TestSRTFixture, DISABLED_RejectConnection) {
    auto ctx = std::make_shared<SRTNet::NetworkConnection>();
    EXPECT_TRUE(mServer.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, ctx));
    EXPECT_FALSE(mClient.startClient("127.0.0.1", 8009, 16, 1000, 100, ctx, SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk))
        << "Expected client connection rejected";
    
    ASSERT_TRUE(waitForClientToConnect(std::chrono::seconds(2)));

    size_t numberOfClients = mServer.getActiveClientSockets().size();
    EXPECT_EQ(numberOfClients, 0);

    auto [srtSocket, networkConnection] = mClient.getConnectedServer();
    EXPECT_EQ(networkConnection, nullptr);

    std::condition_variable receiveCondition;
    std::mutex receiveMutex;
    size_t receivedBytes = 0;

    mServer.receivedData = [&](std::unique_ptr<std::vector<uint8_t>>& data, SRT_MSGCTRL& msgCtrl,
                              std::shared_ptr<SRTNet::NetworkConnection>& ctx, SRTSOCKET socket) {
        {
            std::lock_guard<std::mutex> lock(receiveMutex);
            receivedBytes = data->size();
        }
        receiveCondition.notify_one();
        return true;
    };

    std::vector<uint8_t> sendBuffer(1000);
    std::fill(sendBuffer.begin(), sendBuffer.end(), 1);
    SRT_MSGCTRL msgCtrl = srt_msgctrl_default;
    EXPECT_FALSE(mClient.sendData(sendBuffer.data(), sendBuffer.size(), &msgCtrl))
        << "Expect to fail sending data from unconnected client";

    {
        std::unique_lock<std::mutex> lock(receiveMutex);
        bool successfulWait =
            receiveCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return receivedBytes == 1000; });
        EXPECT_FALSE(successfulWait) << "Did not expect to receive data from unconnected client";
    }
}

TEST_F(TestSRTFixture, SingleSender) {
    ASSERT_TRUE(
        mServer.startServer("127.0.0.1", 8009, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, true, mServerCtx));
    ASSERT_TRUE(mClient.startClient("127.0.0.1", 8009, 16, 1000, 100, mClientCtx, SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    ASSERT_TRUE(mClient.isConnectedToServer());

    ASSERT_TRUE(waitForClientToConnect(std::chrono::seconds(2)));
    ASSERT_TRUE(waitUntil([&]() { return !mServer.getActiveClientSockets().empty(); },
                          std::chrono::seconds(1), std::chrono::milliseconds(10)));

    auto activeClients = mServer.getActiveClients();
    size_t numberOfClients = activeClients.size();
    for (const auto& socketNetworkConnectionPair : activeClients) {
        int32_t number = 0;
        EXPECT_NO_THROW(number = std::any_cast<int32_t>(socketNetworkConnectionPair.second->mObject));
        EXPECT_EQ(number, 1111);
    }
    EXPECT_EQ(numberOfClients, 1);

    auto [srtSocket, networkConnection] = mClient.getConnectedServer();
    EXPECT_NE(networkConnection, nullptr);
    int32_t number = 0;
    EXPECT_NO_THROW(number = std::any_cast<int32_t>(networkConnection->mObject));
    EXPECT_EQ(number, 42);

    // start a new client, should fail since we only accept one single client
    mConnected = false;
    SRTNet client2;
    ASSERT_FALSE(
        client2.startClient("127.0.0.1", 8009, 16, 1000, 100, mClientCtx, SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    EXPECT_FALSE(client2.isConnectedToServer())
        << "Expect to not be able to connect a second client when server just accepts one client";

    activeClients = mServer.getActiveClients();
    numberOfClients = activeClients.size();
    for (const auto& socketNetworkConnectionPair : activeClients) {
        int32_t contextNumber = 0;
        EXPECT_NO_THROW(contextNumber = std::any_cast<int32_t>(socketNetworkConnectionPair.second->mObject));
        EXPECT_EQ(contextNumber, 1111);
    }
    EXPECT_EQ(numberOfClients, 1);

    EXPECT_TRUE(mServer.stop());
}

TEST_F(TestSRTFixture, BindAddressForCaller) {
    ASSERT_TRUE(
        mServer.startServer("127.0.0.1", 8010, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, mServerCtx));
    ASSERT_TRUE(mClient.startClient("127.0.0.1", 8010, "0.0.0.0", 8011, 16, 1000, 100, mClientCtx, SRT_LIVE_MAX_PLSIZE,
                                   true, 5000, kValidPsk));
    ASSERT_TRUE(mClient.isConnectedToServer());


    ASSERT_TRUE(waitForClientToConnect(std::chrono::seconds(2)));
    ASSERT_TRUE(waitUntil([&]() { return !mServer.getActiveClientSockets().empty(); },
                          std::chrono::seconds(1), std::chrono::milliseconds(10)));

    const auto activeClients = mServer.getActiveClients();
    size_t numberOfClients = activeClients.size();
    for (const auto& socketNetworkConnectionPair : activeClients) {
        std::pair<std::string, uint16_t> peerIPAndPort =
                getPeerIpAndPortFromSRTSocket(socketNetworkConnectionPair.first);
        EXPECT_EQ(peerIPAndPort.first, "127.0.0.1");
        EXPECT_EQ(peerIPAndPort.second, 8011);

        std::pair<std::string, uint16_t> ipAndPort =
                getBindIpAndPortFromSRTSocket(socketNetworkConnectionPair.first);
        EXPECT_EQ(ipAndPort.first, "127.0.0.1");
        EXPECT_EQ(ipAndPort.second, 8010);
    }
    EXPECT_EQ(numberOfClients, 1);

    std::pair<std::string, uint16_t> serverIPAndPort = getBindIpAndPortFromSRTSocket(mServer.getBoundSocket());
    EXPECT_EQ(serverIPAndPort.first, "127.0.0.1");
    EXPECT_EQ(serverIPAndPort.second, 8010);
    EXPECT_EQ(serverIPAndPort.second, mServer.getLocallyBoundPort());


    std::pair<std::string, uint16_t> clientIPAndPort = getBindIpAndPortFromSRTSocket(mClient.getBoundSocket());
    EXPECT_EQ(clientIPAndPort.first, "127.0.0.1");
    EXPECT_EQ(clientIPAndPort.second, 8011);
}

TEST_F(TestSRTFixture, AutomaticPortSelection) {
    ASSERT_TRUE(
        mServer.startServer("0.0.0.0", kAnyPort, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, mServerCtx));

    std::pair<std::string, uint16_t> serverIPAndPort = getBindIpAndPortFromSRTSocket(mServer.getBoundSocket());
    EXPECT_EQ(serverIPAndPort.first, "0.0.0.0");
    EXPECT_GT(serverIPAndPort.second, 1024); // We expect it won't pick a privileged port
    EXPECT_EQ(serverIPAndPort.second, mServer.getLocallyBoundPort());

    ASSERT_TRUE(mClient.startClient("127.0.0.1", serverIPAndPort.second, "0.0.0.0", kAnyPort, 16, 1000, 100, mClientCtx,
                                   SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));

    ASSERT_TRUE(waitForClientToConnect(std::chrono::seconds(2)));

    std::pair<std::string, uint16_t> clientIPAndPort = getBindIpAndPortFromSRTSocket(mClient.getBoundSocket());
    EXPECT_EQ(clientIPAndPort.first, "127.0.0.1");
    EXPECT_GT(clientIPAndPort.second, 1024); // We expect it won't pick a privileged port
    EXPECT_NE(clientIPAndPort.second, serverIPAndPort.second);

    ASSERT_TRUE(waitUntil([&]() { return !mServer.getActiveClientSockets().empty(); },
                          std::chrono::seconds(1), std::chrono::milliseconds(10)));

    const auto activeClients = mServer.getActiveClients();
    size_t nClients = activeClients.size();

    for (const auto& socketNetworkConnectionPair : activeClients) {
        std::pair<std::string, uint16_t> peerIPAndPort =
            getPeerIpAndPortFromSRTSocket(socketNetworkConnectionPair.first);
        EXPECT_EQ(peerIPAndPort.first, "127.0.0.1");
        EXPECT_EQ(peerIPAndPort.second, clientIPAndPort.second);

        std::pair<std::string, uint16_t> ipAndPort =
            getBindIpAndPortFromSRTSocket(socketNetworkConnectionPair.first);
        EXPECT_EQ(ipAndPort.first, "127.0.0.1");
        EXPECT_EQ(ipAndPort.second, serverIPAndPort.second);
    }
    EXPECT_EQ(nClients, 1);
}

TEST_F(TestSRTFixture, AutomaticPortSelectionSingleClient) {
    ASSERT_TRUE(
        mServer.startServer("0.0.0.0", kAnyPort, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, true, mServerCtx));

    std::pair<std::string, uint16_t> serverIPAndPort = getBindIpAndPortFromSRTSocket(mServer.getBoundSocket());
    EXPECT_EQ(serverIPAndPort.first, "0.0.0.0");
    EXPECT_GT(serverIPAndPort.second, 1024); // We expect it won't pick a privileged port
    EXPECT_EQ(serverIPAndPort.second, mServer.getLocallyBoundPort());

    ASSERT_TRUE(mClient.startClient("127.0.0.1", serverIPAndPort.second, "0.0.0.0", kAnyPort, 16, 1000, 100, mClientCtx,
                                    SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));

    ASSERT_TRUE(waitForClientToConnect(std::chrono::seconds(2)));
    ASSERT_TRUE(mClient.isConnectedToServer());
    ASSERT_TRUE(mClient.stop());
    ASSERT_FALSE(mClient.isConnectedToServer());
    ASSERT_TRUE(waitForClientToDisconnect(std::chrono::seconds(2)));

    ASSERT_TRUE(mClient.startClient("127.0.0.1", serverIPAndPort.second, "0.0.0.0", kAnyPort, 16, 1000, 100, mClientCtx,
                                    SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));

    ASSERT_TRUE(waitForClientToConnect(std::chrono::seconds(2)));
    ASSERT_TRUE(mClient.isConnectedToServer());
}

TEST_F(TestSRTFixture, FailToBindWhenLocalIPIsMissing) {
    uint16_t kPort = 8021;
    ASSERT_TRUE(
        mServer.startServer("0.0.0.0", kPort, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, mServerCtx));
    std::string kEmptyIP;
    uint16_t kLocalPort = 8022;
    ASSERT_FALSE(mClient.startClient("127.0.0.1", kPort, kEmptyIP, kLocalPort, 16, 1000, 100, mClientCtx,
                                     SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
}

TEST_F(TestSRTFixture, FailToBindWhenLocalIPIsCorrupt) {
    uint16_t kPort = 8021;
    ASSERT_TRUE(
        mServer.startServer("0.0.0.0", kPort, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, mServerCtx));
    std::string kIllFormattedIP = "123.456.789.012";
    uint16_t kLocalPort = 8022;
    ASSERT_FALSE(mClient.startClient("127.0.0.1", kPort, kIllFormattedIP, kLocalPort, 16, 1000, 100, mClientCtx,
                                     SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
}

TEST_F(TestSRTFixture, FailToConnectWhenRemoteHostnameIsCorrupt) {
    uint16_t kPort = 8023;
    std::string kIllFormattedIP = "thi$i$not_a(host)name.com";
    EXPECT_FALSE(mClient.startClient(kIllFormattedIP, kPort, 16, 1000, 100, mClientCtx,
                                     SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    ASSERT_FALSE(mClient.isConnectedToServer());
}

// Test that when the failOnConnectionError flag is set to true the startClient call fails
// if the initial connection attempt fails (no server is currently listening), and verify that
// the client reports that it is not connected to the server.
TEST_F(TestSRTFixture, FailToStartWhenNoServerListens) {
    uint16_t kPort = 8023;
    EXPECT_FALSE(mClient.startClient("127.0.0.1", kPort, 16, 1000, 100, mClientCtx,
                                     SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk));
    ASSERT_FALSE(mClient.isConnectedToServer());
}

// Test that when the failOnConnectionError flag is set to false the startClient call succeeds,
// even-though the initial connection attempt fails (no server is currently listening), and verify that
// the client reports that it is not connected to the server.
TEST_F(TestSRTFixture, SucceedToStartWhenNoServerListens) {
    uint16_t kPort = 8023;
    EXPECT_TRUE(mClient.startClient("127.0.0.1", kPort, 16, 1000, 100, mClientCtx,
                                     SRT_LIVE_MAX_PLSIZE, false, 5000, kValidPsk));
    ASSERT_FALSE(mClient.isConnectedToServer());
}

TEST_F(TestSRTFixture, GetLocallyBoundPort) {
    // Before started, server and client should return 0
    EXPECT_EQ(mServer.getLocallyBoundPort(), 0);
    EXPECT_EQ(mClient.getLocallyBoundPort(), 0);

    ASSERT_TRUE(
            mServer.startServer("0.0.0.0", kAnyPort, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, mServerCtx));
    // Server should now have picked a random available port
    EXPECT_NE(mServer.getLocallyBoundPort(), 0);

    mServer.stop();

    uint16_t kPort = 8024;
    ASSERT_TRUE(
            mServer.startServer("0.0.0.0", kPort, 16, 1000, 100, SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false, mServerCtx));
    // Server should now have picked the port we specified
    EXPECT_EQ(mServer.getLocallyBoundPort(), kPort);

    ASSERT_TRUE(mClient.startClient("127.0.0.1", mServer.getLocallyBoundPort(), "0.0.0.0", 8025, 16, 1000, 100, mClientCtx, SRT_LIVE_MAX_PLSIZE,
                                    true, 5000, kValidPsk));
    EXPECT_EQ(mClient.getLocallyBoundPort(), 8025);

    mClient.stop();
    ASSERT_TRUE(mClient.startClient("127.0.0.1", mServer.getLocallyBoundPort(), "0.0.0.0", 0, 16, 1000, 100, mClientCtx, SRT_LIVE_MAX_PLSIZE,
                                    true, 5000, kValidPsk));
    EXPECT_NE(mClient.getLocallyBoundPort(), 0);
}

TEST_F(TestSRTFixture, StreamId) {
    SRTNet server;
    SRTNet client;
    const std::string sentStreamId = "An example Stream ID";
    char receivedStreamId[1024];

    auto ctx = std::make_shared<SRTNet::NetworkConnection>();
    server.clientConnected = [&](struct sockaddr &sin, SRTSOCKET newSocket,
                                 std::shared_ptr<SRTNet::NetworkConnection> &ctx,
                                 const SRTNet::ConnectionInformation &) {
        int size = 1024;
        EXPECT_NE(srt_getsockflag(newSocket, SRTO_STREAMID, receivedStreamId, &size), SRT_ERROR);
        EXPECT_EQ(size, sentStreamId.size());
        mConnected = true;
        return ctx;
    };

    ASSERT_TRUE(server.startServer("127.0.0.1", 8009, 16, 1000, 100,
                                   SRT_LIVE_MAX_PLSIZE, 5000, kValidPsk, false,
                                   ctx));
    ASSERT_TRUE(client.startClient("127.0.0.1", 8009, 16, 1000, 100, ctx,
                                   SRT_LIVE_MAX_PLSIZE, true, 5000, kValidPsk,
                                   sentStreamId));
    EXPECT_TRUE(waitForClientToConnect(std::chrono::seconds(2)));
    ASSERT_EQ(sentStreamId, std::string(receivedStreamId));
}
