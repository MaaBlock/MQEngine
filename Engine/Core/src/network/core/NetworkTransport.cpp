#include "NetworkTransport.h"
#include "../../thirdparty/AsioBackend.h"

namespace MQEngine {

    namespace {
        using Tcp = boost::asio::ip::tcp;
        using Udp = boost::asio::ip::udp;

        double querySteadyClockSec() noexcept {
            static const auto s_epoch = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(now - s_epoch).count();
        }

        std::shared_ptr<std::vector<uint8_t>> buildWireFrame(
            NetworkPacketType type,
            uint32_t sequence,
            uint32_t senderClientId,
            const uint8_t* payload,
            size_t payloadSize) {
            NetworkPacketHeader header;
            header.magic = NetworkWireConstants::kMagic;
            header.version = NetworkWireConstants::kProtocolVersion;
            header.type = type;
            header.flags = 0;
            header.sequence = sequence;
            header.senderClientId = senderClientId;
            header.payloadSize = static_cast<uint32_t>(payloadSize);

            NetworkByteWriter writer(NetworkWireConstants::kHeaderBytes + payloadSize);
            writer.writeHeader(header);
            if (payload && payloadSize > 0) {
                writer.writeBytes(payload, payloadSize);
            }
            return std::make_shared<std::vector<uint8_t>>(writer.takeBuffer());
        }

        bool isSequenceNewer(uint32_t incoming, uint32_t lastSeen) noexcept {
            if (lastSeen == 0) {
                return true;
            }
            return static_cast<int32_t>(incoming - lastSeen) > 0;
        }
    }

    struct NetworkTransport::Impl {
        struct TcpSession : public std::enable_shared_from_this<TcpSession> {
            explicit TcpSession(boost::asio::io_context& io)
                : socket(io) {}

            Tcp::socket socket;
            int clientId = 0;
            uint64_t sessionToken = 0;
            bool handshakeDone = false;
            bool closed = false;
            bool hasUdpEndpoint = false;
            Udp::endpoint udpEndpoint;
            double lastActiveTimeSec = 0.0;
            uint32_t lastUdpRecvSeq = 0;
            std::array<uint8_t, NetworkWireConstants::kHeaderBytes> headerBuf{};
            std::vector<uint8_t> payloadBuf;
            std::deque<std::shared_ptr<std::vector<uint8_t>>> writeQueue;
        };

        struct UdpDatagramTask {
            Udp::endpoint target;
            std::shared_ptr<std::vector<uint8_t>> data;
        };

        std::atomic<NetworkRole> role{NetworkRole::Offline};
        std::atomic<bool> running{false};
        std::atomic<bool> connected{false};
        std::atomic<bool> udpReady{false};
        std::atomic<int> localClientId{0};
        std::atomic<uint32_t> nextPacketSeq{1};

        std::atomic<float> rttMs{0.0f};
        std::atomic<double> clockOffsetSec{0.0};
        std::atomic<uint64_t> totalBytesSent{0};
        std::atomic<uint64_t> totalBytesReceived{0};
        std::atomic<uint64_t> bytesSentPerSec{0};
        std::atomic<uint64_t> bytesReceivedPerSec{0};

        uint64_t prevSampleBytesSent = 0;
        uint64_t prevSampleBytesReceived = 0;
        double lastRateSampleTimeSec = 0.0;
        double lastHeartbeatSentSec = 0.0;
        double lastUdpBindSentSec = 0.0;

        uint64_t clientSessionToken = 0;
        int nextAssignedClientId = NetworkWireConstants::kFirstRemoteClientId;
        uint64_t tokenSeed = 0x9E3779B97F4A7C15ull;

        UniquePtr<boost::asio::io_context> ioContext;
        UniquePtr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> workGuard;
        UniquePtr<Tcp::acceptor> tcpAcceptor;
        UniquePtr<Udp::socket> udpSocket;
        UniquePtr<boost::asio::steady_timer> maintenanceTimer;
        std::thread ioThread;

        std::shared_ptr<TcpSession> serverConnSession;
        Udp::endpoint serverUdpEndpoint;

        mutable std::mutex sessionMutex;
        std::unordered_map<int, std::shared_ptr<TcpSession>> clientSessions;
        std::vector<std::shared_ptr<TcpSession>> pendingSessions;

        std::array<uint8_t, 2048> udpRecvBuffer{};
        Udp::endpoint udpSenderEndpoint;
        std::deque<UdpDatagramTask> udpSendQueue;

        std::mutex inboundMutex;
        std::vector<InboundNetworkPacket> inboundPackets;

        std::mutex eventMutex;
        std::vector<NetworkConnectionEvent> connectionEvents;

        ~Impl() {
            stop();
        }

        void pushEvent(NetworkConnectionEventType type, int cid, std::string reason = {}) {
            std::lock_guard<std::mutex> lock(eventMutex);
            connectionEvents.push_back(NetworkConnectionEvent{type, cid, std::move(reason)});
        }

        void pushInboundPacket(InboundNetworkPacket&& pkt) {
            std::lock_guard<std::mutex> lock(inboundMutex);
            inboundPackets.push_back(std::move(pkt));
        }

        uint32_t allocateSequence() noexcept {
            return nextPacketSeq.fetch_add(1, std::memory_order_relaxed);
        }

        uint64_t generateSessionToken() noexcept {
            tokenSeed ^= (tokenSeed << 13u);
            tokenSeed ^= (tokenSeed >> 7u);
            tokenSeed ^= (tokenSeed << 17u);
            const uint64_t timeBits = static_cast<uint64_t>(querySteadyClockSec() * 1000000.0);
            return tokenSeed ^ (timeBits * 0xBF58476D1CE4E5B9ull);
        }

        void resetIoState() {
            ioContext = makeUnique<boost::asio::io_context>();
            workGuard = makeUnique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
                boost::asio::make_work_guard(*ioContext));
            maintenanceTimer = makeUnique<boost::asio::steady_timer>(*ioContext);

            rttMs.store(0.0f, std::memory_order_relaxed);
            clockOffsetSec.store(0.0, std::memory_order_relaxed);
            bytesSentPerSec.store(0, std::memory_order_relaxed);
            bytesReceivedPerSec.store(0, std::memory_order_relaxed);
            prevSampleBytesSent = totalBytesSent.load(std::memory_order_relaxed);
            prevSampleBytesReceived = totalBytesReceived.load(std::memory_order_relaxed);
            lastRateSampleTimeSec = querySteadyClockSec();
            lastHeartbeatSentSec = 0.0;
            lastUdpBindSentSec = 0.0;
            clientSessionToken = 0;
            udpSendQueue.clear();
        }

        void stop() {
            const bool wasRunning = running.exchange(false, std::memory_order_acq_rel);
            const NetworkRole prevRole = role.exchange(NetworkRole::Offline, std::memory_order_acq_rel);
            const bool wasConnected = connected.exchange(false, std::memory_order_acq_rel);
            udpReady.store(false, std::memory_order_release);

            if (ioContext) {
                boost::system::error_code ec;
                if (maintenanceTimer) {
                    maintenanceTimer->cancel();
                }
                if (tcpAcceptor && tcpAcceptor->is_open()) {
                    tcpAcceptor->close(ec);
                }
                if (udpSocket && udpSocket->is_open()) {
                    udpSocket->close(ec);
                }
                {
                    std::lock_guard<std::mutex> lock(sessionMutex);
                    for (auto& [cid, sess] : clientSessions) {
                        if (sess && sess->socket.is_open()) {
                            sess->closed = true;
                            sess->socket.close(ec);
                        }
                    }
                    for (auto& sess : pendingSessions) {
                        if (sess && sess->socket.is_open()) {
                            sess->closed = true;
                            sess->socket.close(ec);
                        }
                    }
                    clientSessions.clear();
                    pendingSessions.clear();
                }
                if (serverConnSession && serverConnSession->socket.is_open()) {
                    serverConnSession->closed = true;
                    serverConnSession->socket.close(ec);
                }
                serverConnSession.reset();

                if (workGuard) {
                    workGuard->reset();
                }
                ioContext->stop();
            }

            if (ioThread.joinable()) {
                ioThread.join();
            }

            tcpAcceptor.reset();
            udpSocket.reset();
            maintenanceTimer.reset();
            workGuard.reset();
            ioContext.reset();
            localClientId.store(0, std::memory_order_release);

            if (wasRunning && wasConnected && prevRole == NetworkRole::Client) {
                pushEvent(NetworkConnectionEventType::DisconnectedFromServer, 0, "Local stop");
            }
        }

        Status startServer(uint16_t port, bool hostMode) {
            stop();
            resetIoState();

            try {
                Tcp::endpoint tcpEp(Tcp::v4(), port);
                tcpAcceptor = makeUnique<Tcp::acceptor>(*ioContext);
                tcpAcceptor->open(tcpEp.protocol());
                tcpAcceptor->set_option(Tcp::acceptor::reuse_address(true));
                tcpAcceptor->bind(tcpEp);
                tcpAcceptor->listen(boost::asio::socket_base::max_listen_connections);

                Udp::endpoint udpEp(Udp::v4(), port);
                udpSocket = makeUnique<Udp::socket>(*ioContext);
                udpSocket->open(udpEp.protocol());
                udpSocket->set_option(Udp::socket::reuse_address(true));
                udpSocket->bind(udpEp);
            } catch (const std::exception& e) {
                stop();
                return InternalError(StrCat("启动网络服务端监听端口 ", port, " 失败: ", e.what()));
            }

            role.store(hostMode ? NetworkRole::Host : NetworkRole::Server, std::memory_order_release);
            localClientId.store(
                hostMode ? NetworkWireConstants::kHostLocalClientId : NetworkWireConstants::kServerClientId,
                std::memory_order_release);
            nextAssignedClientId = NetworkWireConstants::kFirstRemoteClientId;
            running.store(true, std::memory_order_release);
            connected.store(true, std::memory_order_release);
            udpReady.store(true, std::memory_order_release);

            startAcceptLoop();
            startUdpReceiveLoop();
            scheduleMaintenanceTimer();

            ioThread = std::thread([this]() {
                FCT::SetThreadName("MQ_NetIO_Server");
                ioContext->run();
            });

            spdlog::info("网络传输层已启动 [{}] 端口: {}", hostMode ? "Host" : "Server", port);
            return OkStatus();
        }

        Status connectClient(const std::string& host, uint16_t port) {
            if (host.empty() || port == 0) {
                return InvalidArgumentError("客户端连接地址或端口无效");
            }
            stop();
            resetIoState();

            Tcp::resolver::results_type tcpEndpoints;
            try {
                Tcp::resolver tcpResolver(*ioContext);
                tcpEndpoints = tcpResolver.resolve(host, std::to_string(port));

                Udp::resolver udpResolver(*ioContext);
                auto udpEndpoints = udpResolver.resolve(Udp::v4(), host, std::to_string(port));
                serverUdpEndpoint = *udpEndpoints.begin();

                udpSocket = makeUnique<Udp::socket>(*ioContext);
                udpSocket->open(Udp::v4());
                udpSocket->bind(Udp::endpoint(Udp::v4(), 0));
            } catch (const std::exception& e) {
                stop();
                return InternalError(StrCat("解析服务端地址 [", host, ":", port, "] 失败: ", e.what()));
            }

            role.store(NetworkRole::Client, std::memory_order_release);
            localClientId.store(0, std::memory_order_release);
            running.store(true, std::memory_order_release);
            connected.store(false, std::memory_order_release);
            udpReady.store(false, std::memory_order_release);

            serverConnSession = std::make_shared<TcpSession>(*ioContext);
            serverConnSession->lastActiveTimeSec = querySteadyClockSec();

            auto session = serverConnSession;
            boost::asio::async_connect(
                session->socket,
                tcpEndpoints,
                [this, session](const boost::system::error_code& ec, const Tcp::endpoint&) {
                    if (ec || !running.load(std::memory_order_acquire)) {
                        handleSessionDisconnect(session, ec ? ec.message() : "Cancelled");
                        return;
                    }
                    boost::system::error_code optEc;
                    session->socket.set_option(Tcp::no_delay(true), optEc);
                    session->lastActiveTimeSec = querySteadyClockSec();

                    sendClientHandshakeReq(session);
                    beginSessionReadHeader(session);
                });

            startUdpReceiveLoop();
            scheduleMaintenanceTimer();

            ioThread = std::thread([this]() {
                FCT::SetThreadName("MQ_NetIO_Client");
                ioContext->run();
            });

            spdlog::info("网络客户端正在连接 [{}:{}]...", host, port);
            return OkStatus();
        }

        void startAcceptLoop() {
            if (!running.load(std::memory_order_acquire) || !tcpAcceptor || !tcpAcceptor->is_open()) {
                return;
            }
            auto session = std::make_shared<TcpSession>(*ioContext);
            tcpAcceptor->async_accept(
                session->socket,
                [this, session](const boost::system::error_code& ec) {
                    if (!running.load(std::memory_order_acquire)) {
                        return;
                    }
                    if (!ec) {
                        boost::system::error_code optEc;
                        session->socket.set_option(Tcp::no_delay(true), optEc);
                        session->lastActiveTimeSec = querySteadyClockSec();
                        {
                            std::lock_guard<std::mutex> lock(sessionMutex);
                            pendingSessions.push_back(session);
                        }
                        beginSessionReadHeader(session);
                    }
                    startAcceptLoop();
                });
        }

        void beginSessionReadHeader(const std::shared_ptr<TcpSession>& session) {
            if (!session || session->closed || !running.load(std::memory_order_acquire)) {
                return;
            }
            boost::asio::async_read(
                session->socket,
                boost::asio::buffer(session->headerBuf),
                [this, session](const boost::system::error_code& ec, size_t bytesRead) {
                    if (ec || bytesRead != NetworkWireConstants::kHeaderBytes) {
                        handleSessionDisconnect(session, ec ? ec.message() : "Incomplete header");
                        return;
                    }
                    totalBytesReceived.fetch_add(bytesRead, std::memory_order_relaxed);
                    session->lastActiveTimeSec = querySteadyClockSec();

                    NetworkByteReader reader(session->headerBuf.data(), session->headerBuf.size());
                    NetworkPacketHeader header;
                    if (!reader.readHeader(header) ||
                        header.payloadSize > NetworkWireConstants::kMaxTcpPayloadBytes) {
                        handleSessionDisconnect(session, "Invalid packet header");
                        return;
                    }

                    if (header.payloadSize == 0) {
                        session->payloadBuf.clear();
                        dispatchReceivedTcpPacket(session, header);
                        beginSessionReadHeader(session);
                    } else {
                        beginSessionReadPayload(session, header);
                    }
                });
        }

        void beginSessionReadPayload(
            const std::shared_ptr<TcpSession>& session,
            const NetworkPacketHeader& header) {
            session->payloadBuf.resize(header.payloadSize);
            boost::asio::async_read(
                session->socket,
                boost::asio::buffer(session->payloadBuf),
                [this, session, header](const boost::system::error_code& ec, size_t bytesRead) {
                    if (ec || bytesRead != header.payloadSize) {
                        handleSessionDisconnect(session, ec ? ec.message() : "Incomplete payload");
                        return;
                    }
                    totalBytesReceived.fetch_add(bytesRead, std::memory_order_relaxed);
                    session->lastActiveTimeSec = querySteadyClockSec();

                    dispatchReceivedTcpPacket(session, header);
                    beginSessionReadHeader(session);
                });
        }

        void enqueueSessionTcpWrite(
            const std::shared_ptr<TcpSession>& session,
            std::shared_ptr<std::vector<uint8_t>> frame) {
            if (!session || !frame || !ioContext || !running.load(std::memory_order_acquire)) {
                return;
            }
            boost::asio::post(*ioContext, [this, session, frame = std::move(frame)]() mutable {
                if (session->closed || !session->socket.is_open()) {
                    return;
                }
                const bool writeInFlight = !session->writeQueue.empty();
                session->writeQueue.push_back(std::move(frame));
                if (!writeInFlight) {
                    pumpSessionTcpWrite(session);
                }
            });
        }

        void pumpSessionTcpWrite(const std::shared_ptr<TcpSession>& session) {
            if (!session || session->closed || session->writeQueue.empty()) {
                return;
            }
            auto frontFrame = session->writeQueue.front();
            boost::asio::async_write(
                session->socket,
                boost::asio::buffer(*frontFrame),
                [this, session, frontFrame](const boost::system::error_code& ec, size_t bytesWritten) {
                    if (ec) {
                        handleSessionDisconnect(session, ec.message());
                        return;
                    }
                    totalBytesSent.fetch_add(bytesWritten, std::memory_order_relaxed);
                    if (!session->writeQueue.empty()) {
                        session->writeQueue.pop_front();
                    }
                    if (!session->writeQueue.empty()) {
                        pumpSessionTcpWrite(session);
                    }
                });
        }

        void handleSessionDisconnect(const std::shared_ptr<TcpSession>& session, const std::string& reason) {
            if (!session || session->closed) {
                return;
            }
            session->closed = true;
            boost::system::error_code ec;
            if (session->socket.is_open()) {
                session->socket.close(ec);
            }
            session->writeQueue.clear();

            const NetworkRole curRole = role.load(std::memory_order_acquire);
            if (curRole == NetworkRole::Client) {
                const bool wasConn = connected.exchange(false, std::memory_order_acq_rel);
                udpReady.store(false, std::memory_order_release);
                if (wasConn || running.load(std::memory_order_acquire)) {
                    pushEvent(NetworkConnectionEventType::DisconnectedFromServer, localClientId.load(), reason);
                }
            } else if (curRole == NetworkRole::Server || curRole == NetworkRole::Host) {
                int removedClientId = 0;
                {
                    std::lock_guard<std::mutex> lock(sessionMutex);
                    pendingSessions.erase(
                        std::remove(pendingSessions.begin(), pendingSessions.end(), session),
                        pendingSessions.end());
                    if (session->clientId > 0) {
                        auto it = clientSessions.find(session->clientId);
                        if (it != clientSessions.end() && it->second == session) {
                            removedClientId = session->clientId;
                            clientSessions.erase(it);
                        }
                    }
                }
                if (removedClientId > 0) {
                    pushEvent(NetworkConnectionEventType::RemoteClientDisconnected, removedClientId, reason);
                }
            }
        }

        void startUdpReceiveLoop() {
            if (!running.load(std::memory_order_acquire) || !udpSocket || !udpSocket->is_open()) {
                return;
            }
            udpSocket->async_receive_from(
                boost::asio::buffer(udpRecvBuffer),
                udpSenderEndpoint,
                [this](const boost::system::error_code& ec, size_t bytesReceived) {
                    if (!running.load(std::memory_order_acquire)) {
                        return;
                    }
                    if (!ec && bytesReceived >= NetworkWireConstants::kHeaderBytes) {
                        totalBytesReceived.fetch_add(bytesReceived, std::memory_order_relaxed);
                        handleUdpDatagram(bytesReceived);
                    }
                    startUdpReceiveLoop();
                });
        }

        void enqueueUdpSend(const Udp::endpoint& target, std::shared_ptr<std::vector<uint8_t>> datagram) {
            if (!datagram || !ioContext || !running.load(std::memory_order_acquire)) {
                return;
            }
            boost::asio::post(*ioContext, [this, target, datagram = std::move(datagram)]() mutable {
                if (!udpSocket || !udpSocket->is_open()) {
                    return;
                }
                const bool sendInFlight = !udpSendQueue.empty();
                udpSendQueue.push_back(UdpDatagramTask{target, std::move(datagram)});
                if (!sendInFlight) {
                    pumpUdpSendQueue();
                }
            });
        }

        void pumpUdpSendQueue() {
            if (!udpSocket || !udpSocket->is_open() || udpSendQueue.empty()) {
                return;
            }
            const auto task = udpSendQueue.front();
            udpSocket->async_send_to(
                boost::asio::buffer(*task.data),
                task.target,
                [this, dataKeepAlive = task.data](const boost::system::error_code& ec, size_t bytesSent) {
                    (void)dataKeepAlive;
                    if (!ec) {
                        totalBytesSent.fetch_add(bytesSent, std::memory_order_relaxed);
                    }
                    if (!udpSendQueue.empty()) {
                        udpSendQueue.pop_front();
                    }
                    if (!udpSendQueue.empty()) {
                        pumpUdpSendQueue();
                    }
                });
        }

        void sendClientHandshakeReq(const std::shared_ptr<TcpSession>& session) {
            NetworkByteWriter writer(16);
            writer.writeU32(NetworkWireConstants::kProtocolVersion);
            writer.writeF64(querySteadyClockSec());
            auto frame = buildWireFrame(
                NetworkPacketType::HandshakeReq,
                allocateSequence(),
                0,
                writer.data(),
                writer.size());
            enqueueSessionTcpWrite(session, std::move(frame));
        }

        void sendClientUdpBind() {
            const int cid = localClientId.load(std::memory_order_acquire);
            if (cid <= 0 || clientSessionToken == 0) {
                return;
            }
            NetworkByteWriter writer(16);
            writer.writeI32(cid);
            writer.writeU64(clientSessionToken);
            auto datagram = buildWireFrame(
                NetworkPacketType::UdpBind,
                allocateSequence(),
                static_cast<uint32_t>(cid),
                writer.data(),
                writer.size());
            enqueueUdpSend(serverUdpEndpoint, std::move(datagram));
            lastUdpBindSentSec = querySteadyClockSec();
        }

        void sendClientHeartbeat() {
            const int cid = localClientId.load(std::memory_order_acquire);
            if (cid <= 0) {
                return;
            }
            const double nowSec = querySteadyClockSec();
            NetworkByteWriter writer(16);
            writer.writeF64(nowSec);
            auto frame = buildWireFrame(
                NetworkPacketType::Heartbeat,
                allocateSequence(),
                static_cast<uint32_t>(cid),
                writer.data(),
                writer.size());

            if (udpReady.load(std::memory_order_acquire)) {
                enqueueUdpSend(serverUdpEndpoint, std::move(frame));
            } else if (serverConnSession) {
                enqueueSessionTcpWrite(serverConnSession, std::move(frame));
            }
            lastHeartbeatSentSec = nowSec;
        }

        void dispatchReceivedTcpPacket(
            const std::shared_ptr<TcpSession>& session,
            const NetworkPacketHeader& header) {
            const NetworkRole curRole = role.load(std::memory_order_acquire);
            const double nowSec = querySteadyClockSec();

            if (curRole == NetworkRole::Server || curRole == NetworkRole::Host) {
                if (header.type == NetworkPacketType::HandshakeReq) {
                    handleServerHandshakeReq(session);
                    return;
                }
                if (!session->handshakeDone || session->clientId <= 0) {
                    handleSessionDisconnect(session, "Unauthenticated packet before handshake");
                    return;
                }
                if (header.type == NetworkPacketType::Heartbeat) {
                    handleServerHeartbeat(session->clientId, session, nullptr, session->payloadBuf);
                    return;
                }
                if (header.type == NetworkPacketType::Disconnect) {
                    handleSessionDisconnect(session, "Peer requested disconnect");
                    return;
                }

                InboundNetworkPacket pkt;
                pkt.header = header;
                pkt.channel = NetworkChannel::ReliableTcp;
                pkt.sourceClientId = session->clientId;
                pkt.receiveLocalTime = nowSec;
                pkt.payload = session->payloadBuf;
                pushInboundPacket(std::move(pkt));
                return;
            }

            if (curRole == NetworkRole::Client) {
                if (header.type == NetworkPacketType::HandshakeAck) {
                    handleClientHandshakeAck(session->payloadBuf, nowSec);
                    return;
                }
                if (header.type == NetworkPacketType::HeartbeatAck) {
                    handleClientHeartbeatAck(session->payloadBuf, nowSec);
                    return;
                }
                if (header.type == NetworkPacketType::Disconnect) {
                    handleSessionDisconnect(session, "Server closed connection");
                    return;
                }

                InboundNetworkPacket pkt;
                pkt.header = header;
                pkt.channel = NetworkChannel::ReliableTcp;
                pkt.sourceClientId = NetworkWireConstants::kServerClientId;
                pkt.receiveLocalTime = nowSec;
                pkt.payload = session->payloadBuf;
                pushInboundPacket(std::move(pkt));
            }
        }

        void handleServerHandshakeReq(const std::shared_ptr<TcpSession>& session) {
            if (session->handshakeDone) {
                return;
            }
            const int assignedId = nextAssignedClientId++;
            const uint64_t token = generateSessionToken();
            session->clientId = assignedId;
            session->sessionToken = token;
            session->handshakeDone = true;

            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                pendingSessions.erase(
                    std::remove(pendingSessions.begin(), pendingSessions.end(), session),
                    pendingSessions.end());
                clientSessions[assignedId] = session;
            }

            NetworkByteWriter writer(32);
            writer.writeI32(assignedId);
            writer.writeU64(token);
            writer.writeF64(querySteadyClockSec());

            auto ackFrame = buildWireFrame(
                NetworkPacketType::HandshakeAck,
                allocateSequence(),
                static_cast<uint32_t>(localClientId.load(std::memory_order_relaxed)),
                writer.data(),
                writer.size());
            enqueueSessionTcpWrite(session, std::move(ackFrame));

            pushEvent(NetworkConnectionEventType::RemoteClientConnected, assignedId, "Handshake OK");
        }

        void handleClientHandshakeAck(const std::vector<uint8_t>& payload, double nowSec) {
            NetworkByteReader reader(payload);
            int32_t assignedId = 0;
            uint64_t token = 0;
            double serverNow = 0.0;
            if (!reader.readI32(assignedId) || !reader.readU64(token) || !reader.readF64(serverNow)) {
                return;
            }
            localClientId.store(assignedId, std::memory_order_release);
            clientSessionToken = token;
            clockOffsetSec.store(serverNow - nowSec, std::memory_order_relaxed);
            connected.store(true, std::memory_order_release);

            sendClientUdpBind();
            sendClientHeartbeat();
            pushEvent(NetworkConnectionEventType::ConnectedToServer, assignedId, "Connected");
        }

        void handleServerHeartbeat(
            int clientId,
            const std::shared_ptr<TcpSession>& tcpSess,
            const Udp::endpoint* udpEp,
            const std::vector<uint8_t>& payload) {
            NetworkByteReader reader(payload);
            double clientSentTime = 0.0;
            if (!reader.readF64(clientSentTime)) {
                return;
            }

            NetworkByteWriter writer(16);
            writer.writeF64(clientSentTime);
            writer.writeF64(querySteadyClockSec());

            auto ack = buildWireFrame(
                NetworkPacketType::HeartbeatAck,
                allocateSequence(),
                static_cast<uint32_t>(localClientId.load(std::memory_order_relaxed)),
                writer.data(),
                writer.size());

            if (udpEp) {
                enqueueUdpSend(*udpEp, std::move(ack));
            } else if (tcpSess) {
                enqueueSessionTcpWrite(tcpSess, std::move(ack));
            } else {
                std::shared_ptr<TcpSession> found;
                {
                    std::lock_guard<std::mutex> lock(sessionMutex);
                    auto it = clientSessions.find(clientId);
                    if (it != clientSessions.end()) {
                        found = it->second;
                    }
                }
                if (found) {
                    enqueueSessionTcpWrite(found, std::move(ack));
                }
            }
        }

        void handleClientHeartbeatAck(const std::vector<uint8_t>& payload, double nowSec) {
            NetworkByteReader reader(payload);
            double clientSentTime = 0.0;
            double serverNow = 0.0;
            if (!reader.readF64(clientSentTime) || !reader.readF64(serverNow)) {
                return;
            }
            const double sampleRttSec = std::max(0.0, nowSec - clientSentTime);
            const float sampleRttMs = static_cast<float>(sampleRttSec * 1000.0);
            const float prevRtt = rttMs.load(std::memory_order_relaxed);
            const float smoothedRtt = (prevRtt <= 0.01f) ? sampleRttMs : (prevRtt * 0.8f + sampleRttMs * 0.2f);
            rttMs.store(smoothedRtt, std::memory_order_relaxed);

            const double sampleOffset = serverNow - (clientSentTime + sampleRttSec * 0.5);
            const double prevOffset = clockOffsetSec.load(std::memory_order_relaxed);
            clockOffsetSec.store(prevOffset * 0.8 + sampleOffset * 0.2, std::memory_order_relaxed);
        }

        void handleUdpDatagram(size_t bytesReceived) {
            NetworkByteReader reader(udpRecvBuffer.data(), bytesReceived);
            NetworkPacketHeader header;
            if (!reader.readHeader(header)) {
                return;
            }
            if (header.payloadSize != reader.remaining()) {
                return;
            }

            const double nowSec = querySteadyClockSec();
            const NetworkRole curRole = role.load(std::memory_order_acquire);
            std::vector<uint8_t> payload(
                udpRecvBuffer.data() + NetworkWireConstants::kHeaderBytes,
                udpRecvBuffer.data() + bytesReceived);

            if (curRole == NetworkRole::Server || curRole == NetworkRole::Host) {
                if (header.type == NetworkPacketType::UdpBind) {
                    handleServerUdpBind(udpSenderEndpoint, payload, nowSec);
                    return;
                }

                std::shared_ptr<TcpSession> matchedSession;
                {
                    std::lock_guard<std::mutex> lock(sessionMutex);
                    auto it = clientSessions.find(static_cast<int>(header.senderClientId));
                    if (it != clientSessions.end() &&
                        it->second &&
                        it->second->hasUdpEndpoint &&
                        it->second->udpEndpoint == udpSenderEndpoint) {
                        matchedSession = it->second;
                    }
                }
                if (!matchedSession) {
                    return;
                }
                matchedSession->lastActiveTimeSec = nowSec;

                if (header.type == NetworkPacketType::Heartbeat) {
                    handleServerHeartbeat(matchedSession->clientId, matchedSession, &udpSenderEndpoint, payload);
                    return;
                }

                if (!isSequenceNewer(header.sequence, matchedSession->lastUdpRecvSeq)) {
                    return;
                }
                matchedSession->lastUdpRecvSeq = header.sequence;

                InboundNetworkPacket pkt;
                pkt.header = header;
                pkt.channel = NetworkChannel::UnreliableUdp;
                pkt.sourceClientId = matchedSession->clientId;
                pkt.receiveLocalTime = nowSec;
                pkt.payload = std::move(payload);
                pushInboundPacket(std::move(pkt));
                return;
            }

            if (curRole == NetworkRole::Client) {
                if (!connected.load(std::memory_order_acquire)) {
                    return;
                }
                if (serverConnSession) {
                    serverConnSession->lastActiveTimeSec = nowSec;
                }
                if (header.type == NetworkPacketType::UdpBindAck) {
                    udpReady.store(true, std::memory_order_release);
                    return;
                }
                if (header.type == NetworkPacketType::HeartbeatAck) {
                    handleClientHeartbeatAck(payload, nowSec);
                    return;
                }

                InboundNetworkPacket pkt;
                pkt.header = header;
                pkt.channel = NetworkChannel::UnreliableUdp;
                pkt.sourceClientId = NetworkWireConstants::kServerClientId;
                pkt.receiveLocalTime = nowSec;
                pkt.payload = std::move(payload);
                pushInboundPacket(std::move(pkt));
            }
        }

        void handleServerUdpBind(
            const Udp::endpoint& senderEp,
            const std::vector<uint8_t>& payload,
            double nowSec) {
            NetworkByteReader reader(payload);
            int32_t cid = 0;
            uint64_t token = 0;
            if (!reader.readI32(cid) || !reader.readU64(token)) {
                return;
            }

            bool boundOk = false;
            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                auto it = clientSessions.find(cid);
                if (it != clientSessions.end() && it->second && it->second->sessionToken == token) {
                    it->second->udpEndpoint = senderEp;
                    it->second->hasUdpEndpoint = true;
                    it->second->lastActiveTimeSec = nowSec;
                    boundOk = true;
                }
            }
            if (!boundOk) {
                return;
            }

            auto ack = buildWireFrame(
                NetworkPacketType::UdpBindAck,
                allocateSequence(),
                static_cast<uint32_t>(localClientId.load(std::memory_order_relaxed)),
                nullptr,
                0);
            enqueueUdpSend(senderEp, std::move(ack));
        }

        void scheduleMaintenanceTimer() {
            if (!running.load(std::memory_order_acquire) || !maintenanceTimer) {
                return;
            }
            maintenanceTimer->expires_after(std::chrono::milliseconds(250));
            maintenanceTimer->async_wait([this](const boost::system::error_code& ec) {
                if (ec || !running.load(std::memory_order_acquire)) {
                    return;
                }
                runMaintenanceStep();
                scheduleMaintenanceTimer();
            });
        }

        void runMaintenanceStep() {
            const double nowSec = querySteadyClockSec();

            const double rateElapsed = nowSec - lastRateSampleTimeSec;
            if (rateElapsed >= 1.0) {
                const uint64_t curSent = totalBytesSent.load(std::memory_order_relaxed);
                const uint64_t curRecv = totalBytesReceived.load(std::memory_order_relaxed);
                bytesSentPerSec.store(
                    static_cast<uint64_t>(static_cast<double>(curSent - prevSampleBytesSent) / rateElapsed),
                    std::memory_order_relaxed);
                bytesReceivedPerSec.store(
                    static_cast<uint64_t>(static_cast<double>(curRecv - prevSampleBytesReceived) / rateElapsed),
                    std::memory_order_relaxed);
                prevSampleBytesSent = curSent;
                prevSampleBytesReceived = curRecv;
                lastRateSampleTimeSec = nowSec;
            }

            const NetworkRole curRole = role.load(std::memory_order_acquire);
            if (curRole == NetworkRole::Client && connected.load(std::memory_order_acquire)) {
                if (!udpReady.load(std::memory_order_acquire) && (nowSec - lastUdpBindSentSec >= 0.5)) {
                    sendClientUdpBind();
                }
                if (nowSec - lastHeartbeatSentSec >= 1.0) {
                    sendClientHeartbeat();
                }
            } else if (curRole == NetworkRole::Server || curRole == NetworkRole::Host) {
                std::vector<std::shared_ptr<TcpSession>> timedOut;
                {
                    std::lock_guard<std::mutex> lock(sessionMutex);
                    for (const auto& [cid, sess] : clientSessions) {
                        if (sess && (nowSec - sess->lastActiveTimeSec > 15.0)) {
                            timedOut.push_back(sess);
                        }
                    }
                }
                for (const auto& sess : timedOut) {
                    handleSessionDisconnect(sess, "Heartbeat timeout");
                }
            }
        }
    };

    NetworkTransport::NetworkTransport()
        : m_impl(makeUnique<Impl>()) {}

    NetworkTransport::~NetworkTransport() {
        stop();
    }

    Status NetworkTransport::startServer(uint16_t port, bool isHostMode) {
        return m_impl->startServer(port, isHostMode);
    }

    Status NetworkTransport::connectClient(const std::string& host, uint16_t port) {
        return m_impl->connectClient(host, port);
    }

    void NetworkTransport::stop() {
        if (m_impl) {
            m_impl->stop();
        }
    }

    NetworkRole NetworkTransport::getRole() const noexcept {
        return m_impl->role.load(std::memory_order_acquire);
    }

    bool NetworkTransport::isConnected() const noexcept {
        return m_impl->connected.load(std::memory_order_acquire);
    }

    bool NetworkTransport::isUdpReady() const noexcept {
        return m_impl->udpReady.load(std::memory_order_acquire);
    }

    int NetworkTransport::getLocalClientId() const noexcept {
        return m_impl->localClientId.load(std::memory_order_acquire);
    }

    std::vector<int> NetworkTransport::getConnectedClientIds() const {
        std::vector<int> ids;
        std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
        ids.reserve(m_impl->clientSessions.size());
        for (const auto& [cid, sess] : m_impl->clientSessions) {
            if (sess && !sess->closed) {
                ids.push_back(cid);
            }
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    size_t NetworkTransport::getConnectedClientCount() const {
        std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
        return m_impl->clientSessions.size();
    }

    Status NetworkTransport::sendToServer(
        NetworkPacketType type,
        NetworkChannel channel,
        const uint8_t* payload,
        size_t payloadSize) {
        if (getRole() != NetworkRole::Client || !isConnected()) {
            return InvalidArgumentError("当前未以客户端身份连接到服务器");
        }
        if (payloadSize > NetworkWireConstants::kMaxTcpPayloadBytes) {
            return InvalidArgumentError("网络数据包超过最大长度限制");
        }

        auto frame = buildWireFrame(
            type,
            m_impl->allocateSequence(),
            static_cast<uint32_t>(getLocalClientId()),
            payload,
            payloadSize);

        if (channel == NetworkChannel::UnreliableUdp &&
            isUdpReady() &&
            frame->size() <= NetworkWireConstants::kMaxUdpDatagramBytes) {
            m_impl->enqueueUdpSend(m_impl->serverUdpEndpoint, std::move(frame));
        } else {
            m_impl->enqueueSessionTcpWrite(m_impl->serverConnSession, std::move(frame));
        }
        return OkStatus();
    }

    Status NetworkTransport::sendToClient(
        int clientId,
        NetworkPacketType type,
        NetworkChannel channel,
        const uint8_t* payload,
        size_t payloadSize) {
        const NetworkRole curRole = getRole();
        if (curRole != NetworkRole::Server && curRole != NetworkRole::Host) {
            return InvalidArgumentError("仅服务端或 Host 可向指定客户端发送数据包");
        }
        if (payloadSize > NetworkWireConstants::kMaxTcpPayloadBytes) {
            return InvalidArgumentError("网络数据包超过最大长度限制");
        }

        std::shared_ptr<Impl::TcpSession> targetSession;
        {
            std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
            auto it = m_impl->clientSessions.find(clientId);
            if (it != m_impl->clientSessions.end()) {
                targetSession = it->second;
            }
        }
        if (!targetSession || targetSession->closed) {
            return NotFoundError(StrCat("目标客户端未连接: ", clientId));
        }

        auto frame = buildWireFrame(
            type,
            m_impl->allocateSequence(),
            static_cast<uint32_t>(getLocalClientId()),
            payload,
            payloadSize);

        if (channel == NetworkChannel::UnreliableUdp &&
            targetSession->hasUdpEndpoint &&
            frame->size() <= NetworkWireConstants::kMaxUdpDatagramBytes) {
            m_impl->enqueueUdpSend(targetSession->udpEndpoint, std::move(frame));
        } else {
            m_impl->enqueueSessionTcpWrite(targetSession, std::move(frame));
        }
        return OkStatus();
    }

    Status NetworkTransport::broadcast(
        NetworkPacketType type,
        NetworkChannel channel,
        const uint8_t* payload,
        size_t payloadSize,
        int excludeClientId) {
        const NetworkRole curRole = getRole();
        if (curRole != NetworkRole::Server && curRole != NetworkRole::Host) {
            return InvalidArgumentError("仅服务端或 Host 可广播数据包");
        }
        if (payloadSize > NetworkWireConstants::kMaxTcpPayloadBytes) {
            return InvalidArgumentError("广播数据包超过最大长度限制");
        }

        std::vector<std::shared_ptr<Impl::TcpSession>> targets;
        {
            std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
            targets.reserve(m_impl->clientSessions.size());
            for (const auto& [cid, sess] : m_impl->clientSessions) {
                if (cid != excludeClientId && sess && !sess->closed) {
                    targets.push_back(sess);
                }
            }
        }
        if (targets.empty()) {
            return OkStatus();
        }

        auto sharedFrame = buildWireFrame(
            type,
            m_impl->allocateSequence(),
            static_cast<uint32_t>(getLocalClientId()),
            payload,
            payloadSize);

        for (const auto& sess : targets) {
            if (channel == NetworkChannel::UnreliableUdp &&
                sess->hasUdpEndpoint &&
                sharedFrame->size() <= NetworkWireConstants::kMaxUdpDatagramBytes) {
                m_impl->enqueueUdpSend(sess->udpEndpoint, sharedFrame);
            } else {
                m_impl->enqueueSessionTcpWrite(sess, sharedFrame);
            }
        }
        return OkStatus();
    }

    void NetworkTransport::pollPackets(std::vector<InboundNetworkPacket>& outPackets) {
        std::lock_guard<std::mutex> lock(m_impl->inboundMutex);
        if (outPackets.empty()) {
            outPackets.swap(m_impl->inboundPackets);
        } else {
            outPackets.insert(
                outPackets.end(),
                std::make_move_iterator(m_impl->inboundPackets.begin()),
                std::make_move_iterator(m_impl->inboundPackets.end()));
            m_impl->inboundPackets.clear();
        }
    }

    void NetworkTransport::pollEvents(std::vector<NetworkConnectionEvent>& outEvents) {
        std::lock_guard<std::mutex> lock(m_impl->eventMutex);
        if (outEvents.empty()) {
            outEvents.swap(m_impl->connectionEvents);
        } else {
            outEvents.insert(
                outEvents.end(),
                std::make_move_iterator(m_impl->connectionEvents.begin()),
                std::make_move_iterator(m_impl->connectionEvents.end()));
            m_impl->connectionEvents.clear();
        }
    }

    double NetworkTransport::getLocalTimeSec() const noexcept {
        return querySteadyClockSec();
    }

    double NetworkTransport::getEstimatedServerTimeSec() const noexcept {
        const double localNow = querySteadyClockSec();
        if (getRole() == NetworkRole::Client) {
            return localNow + m_impl->clockOffsetSec.load(std::memory_order_relaxed);
        }
        return localNow;
    }

    float NetworkTransport::getRttMs() const noexcept {
        return m_impl->rttMs.load(std::memory_order_relaxed);
    }

    uint64_t NetworkTransport::getBytesSentPerSec() const noexcept {
        return m_impl->bytesSentPerSec.load(std::memory_order_relaxed);
    }

    uint64_t NetworkTransport::getBytesReceivedPerSec() const noexcept {
        return m_impl->bytesReceivedPerSec.load(std::memory_order_relaxed);
    }

    uint64_t NetworkTransport::getTotalBytesSent() const noexcept {
        return m_impl->totalBytesSent.load(std::memory_order_relaxed);
    }

    uint64_t NetworkTransport::getTotalBytesReceived() const noexcept {
        return m_impl->totalBytesReceived.load(std::memory_order_relaxed);
    }

} // namespace MQEngine
