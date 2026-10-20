#include <ra/Net.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ra::net {
namespace {

constexpr const char* RelayHostDefault = "demo.nats.io";
constexpr std::uint16_t RelayPortDefault = 4222;
constexpr const char* RelayDiscoverySubject = "rtsclicker2d.discovery.v1";
constexpr const char* RelayPeerPrefix = "rtsclicker2d.peer.v1.";
constexpr const char* RelayQueueGroup = "rtsclicker2d-hosts-v1";
constexpr char RelayMagic[4] = {'R', '2', 'N', '1'};
constexpr std::size_t RelayIdLength = 16;
constexpr std::size_t RelayEnvelopeHeader = 4 + RelayIdLength + sizeof(std::uint16_t);

std::string winSockError(const char* prefix)
{
    return std::string(prefix) + " (WSA " + std::to_string(WSAGetLastError()) + ")";
}

std::string makeRelayId()
{
    const auto now = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uint64_t seed = now;
    try {
        std::random_device rd;
        seed ^= (static_cast<std::uint64_t>(rd()) << 32);
        seed ^= static_cast<std::uint64_t>(rd());
    } catch (...) {
        seed ^= 0x9e3779b97f4a7c15ULL;
    }

    std::mt19937_64 rng(seed);
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << rng();
    return out.str();
}

bool validRelayId(const std::string& id)
{
    if (id.size() != RelayIdLength) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

std::uint16_t relayPortFromEnvironment()
{
    const char* value = std::getenv("RTSCLICKER_RELAY_PORT");
    if (!value || !*value) return RelayPortDefault;
    const long parsed = std::strtol(value, nullptr, 10);
    if (parsed <= 0 || parsed > 65535) return RelayPortDefault;
    return static_cast<std::uint16_t>(parsed);
}

std::string relayHostFromEnvironment()
{
    const char* value = std::getenv("RTSCLICKER_RELAY_HOST");
    return (value && *value) ? value : RelayHostDefault;
}

bool relayDisabled()
{
    const char* value = std::getenv("RTSCLICKER_RELAY");
    if (!value) return false;
    return std::strcmp(value, "0") == 0 ||
           std::strcmp(value, "off") == 0 ||
           std::strcmp(value, "OFF") == 0;
}

} // namespace

struct DatagramSocket::Impl {
    struct RelayPacket {
        Endpoint from{};
        std::string data{};
    };

    SOCKET socket{INVALID_SOCKET};
    SOCKET relaySocket{INVALID_SOCKET};
    bool winsockStarted{false};
    std::uint16_t port{0};
    std::string error{};

    std::string relayId{};
    std::string relayRx{};
    std::deque<RelayPacket> relayPackets{};
    bool relayDiscoverySubscribed{false};
    std::string relayError{};
};

DatagramSocket::DatagramSocket()
    : impl_(std::make_unique<Impl>())
{
}

DatagramSocket::~DatagramSocket()
{
    close();
}

DatagramSocket::DatagramSocket(DatagramSocket&&) noexcept = default;
DatagramSocket& DatagramSocket::operator=(DatagramSocket&&) noexcept = default;

namespace {

bool waitForSocket(SOCKET socket, bool write, long timeoutMs)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    const int result = write
        ? select(0, nullptr, &set, nullptr, &timeout)
        : select(0, &set, nullptr, nullptr, &timeout);
    return result > 0;
}

template <typename ImplT>
bool relaySendAll(ImplT& impl, const char* data, std::size_t size)
{
    if (impl.relaySocket == INVALID_SOCKET) return false;

    std::size_t offset = 0;
    while (offset < size) {
        const int sent = ::send(
            impl.relaySocket,
            data + offset,
            static_cast<int>(std::min<std::size_t>(size - offset, 1u << 20)),
            0);

        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }

        if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            if (waitForSocket(impl.relaySocket, true, 100)) continue;
        }

        impl.relayError = winSockError("relay send failed");
        closesocket(impl.relaySocket);
        impl.relaySocket = INVALID_SOCKET;
        return false;
    }
    return true;
}

template <typename ImplT>
bool relaySendAll(ImplT& impl, const std::string& data)
{
    return relaySendAll(impl, data.data(), data.size());
}

template <typename ImplT>
bool relayPublish(ImplT& impl, const std::string& subject, const void* data, std::size_t size)
{
    if (impl.relaySocket == INVALID_SOCKET || !data || size == 0 || size > 1024u * 1024u) return false;

    std::string packet;
    const std::string header = "PUB " + subject + " " + std::to_string(size) + "\r\n";
    packet.reserve(header.size() + size + 2);
    packet += header;
    packet.append(static_cast<const char*>(data), size);
    packet += "\r\n";
    return relaySendAll(impl, packet);
}

template <typename ImplT>
std::string makeRelayEnvelope(const ImplT& impl, const void* data, std::size_t size)
{
    std::string envelope;
    envelope.reserve(RelayEnvelopeHeader + size);
    envelope.append(RelayMagic, sizeof(RelayMagic));
    envelope.append(impl.relayId);
    const std::uint16_t networkPort = htons(impl.port);
    envelope.append(reinterpret_cast<const char*>(&networkPort), sizeof(networkPort));
    envelope.append(static_cast<const char*>(data), size);
    return envelope;
}

template <typename ImplT>
void queueRelayEnvelope(ImplT& impl, const std::string& payload)
{
    if (payload.size() < RelayEnvelopeHeader) return;
    if (std::memcmp(payload.data(), RelayMagic, sizeof(RelayMagic)) != 0) return;

    const std::string senderId(payload.data() + 4, RelayIdLength);
    if (!validRelayId(senderId) || senderId == impl.relayId) return;

    std::uint16_t networkPort = 0;
    std::memcpy(&networkPort, payload.data() + 4 + RelayIdLength, sizeof(networkPort));
    const std::uint16_t senderPort = ntohs(networkPort);
    if (senderPort == 0) return;

    typename ImplT::RelayPacket packet;
    packet.from.address = "nats:" + senderId;
    packet.from.port = senderPort;
    packet.data.assign(payload.data() + RelayEnvelopeHeader, payload.size() - RelayEnvelopeHeader);
    impl.relayPackets.push_back(std::move(packet));
}

template <typename ImplT>
void parseRelayInput(ImplT& impl)
{
    while (!impl.relayRx.empty()) {
        if (impl.relayRx.rfind("PING\r\n", 0) == 0) {
            relaySendAll(impl, "PONG\r\n", 6);
            impl.relayRx.erase(0, 6);
            continue;
        }
        if (impl.relayRx.rfind("PONG\r\n", 0) == 0) {
            impl.relayRx.erase(0, 6);
            continue;
        }
        if (impl.relayRx.rfind("+OK\r\n", 0) == 0) {
            impl.relayRx.erase(0, 5);
            continue;
        }

        const std::size_t lineEnd = impl.relayRx.find("\r\n");
        if (lineEnd == std::string::npos) return;

        const std::string line = impl.relayRx.substr(0, lineEnd);
        if (line.rfind("INFO ", 0) == 0) {
            impl.relayRx.erase(0, lineEnd + 2);
            continue;
        }
        if (line.rfind("-ERR", 0) == 0) {
            impl.relayError = line;
            impl.relayRx.erase(0, lineEnd + 2);
            continue;
        }
        if (line.rfind("MSG ", 0) != 0) {
            impl.relayRx.erase(0, lineEnd + 2);
            continue;
        }

        std::istringstream header(line);
        std::vector<std::string> fields;
        std::string field;
        while (header >> field) fields.push_back(field);
        if (fields.size() < 4) {
            impl.relayRx.erase(0, lineEnd + 2);
            continue;
        }

        char* end = nullptr;
        const unsigned long parsedSize = std::strtoul(fields.back().c_str(), &end, 10);
        if (!end || *end != '\0' || parsedSize > 1024u * 1024u) {
            impl.relayRx.erase(0, lineEnd + 2);
            continue;
        }

        const std::size_t payloadStart = lineEnd + 2;
        const std::size_t payloadSize = static_cast<std::size_t>(parsedSize);
        if (impl.relayRx.size() < payloadStart + payloadSize + 2) return;

        const std::string payload = impl.relayRx.substr(payloadStart, payloadSize);
        queueRelayEnvelope(impl, payload);
        impl.relayRx.erase(0, payloadStart + payloadSize + 2);
    }
}

template <typename ImplT>
void pollRelay(ImplT& impl)
{
    if (impl.relaySocket == INVALID_SOCKET) return;

    char buffer[8192];
    for (;;) {
        const int received = recv(impl.relaySocket, buffer, sizeof(buffer), 0);
        if (received > 0) {
            impl.relayRx.append(buffer, static_cast<std::size_t>(received));
            continue;
        }
        if (received == 0) {
            closesocket(impl.relaySocket);
            impl.relaySocket = INVALID_SOCKET;
            break;
        }

        const int code = WSAGetLastError();
        if (code == WSAEWOULDBLOCK) break;
        impl.relayError = "relay recv failed (WSA " + std::to_string(code) + ")";
        closesocket(impl.relaySocket);
        impl.relaySocket = INVALID_SOCKET;
        break;
    }

    parseRelayInput(impl);
}

template <typename ImplT>
void unsubscribeRelayDiscovery(ImplT& impl)
{
    if (!impl.relayDiscoverySubscribed || impl.relaySocket == INVALID_SOCKET) return;
    relaySendAll(impl, "UNSUB 1\r\n", 9);
    impl.relayDiscoverySubscribed = false;
}

template <typename ImplT>
void tryOpenRelay(ImplT& impl, bool subscribeDiscovery)
{
    if (relayDisabled()) return;

    const std::string host = relayHostFromEnvironment();
    const std::uint16_t port = relayPortFromEnvironment();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &addresses) != 0) {
        impl.relayError = "relay DNS failed: " + host;
        return;
    }

    SOCKET relay = INVALID_SOCKET;
    for (addrinfo* addr = addresses; addr; addr = addr->ai_next) {
        relay = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (relay == INVALID_SOCKET) continue;

        u_long nonBlocking = 1;
        ioctlsocket(relay, FIONBIO, &nonBlocking);

        int result = ::connect(relay, addr->ai_addr, static_cast<int>(addr->ai_addrlen));
        if (result == SOCKET_ERROR) {
            const int code = WSAGetLastError();
            if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS && code != WSAEINVAL) {
                closesocket(relay);
                relay = INVALID_SOCKET;
                continue;
            }

            if (!waitForSocket(relay, true, 900)) {
                closesocket(relay);
                relay = INVALID_SOCKET;
                continue;
            }

            int socketError = 0;
            int socketErrorSize = sizeof(socketError);
            if (getsockopt(relay, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&socketError), &socketErrorSize) != 0 || socketError != 0) {
                closesocket(relay);
                relay = INVALID_SOCKET;
                continue;
            }
        }
        break;
    }
    freeaddrinfo(addresses);

    if (relay == INVALID_SOCKET) {
        impl.relayError = "relay connection failed: " + host + ":" + std::to_string(port);
        return;
    }

    impl.relaySocket = relay;
    impl.relayId = makeRelayId();
    impl.relayRx.clear();
    impl.relayPackets.clear();
    impl.relayDiscoverySubscribed = subscribeDiscovery;

    if (waitForSocket(relay, false, 500)) {
        char info[4096];
        const int received = recv(relay, info, sizeof(info), 0);
        if (received > 0) impl.relayRx.append(info, static_cast<std::size_t>(received));
    }

    std::string setup =
        "CONNECT {\"verbose\":false,\"pedantic\":false,\"name\":\"rtsclicker2d\"}\r\n";
    if (subscribeDiscovery) {
        setup += std::string("SUB ") + RelayDiscoverySubject + " " + RelayQueueGroup + " 1\r\n";
    }
    setup += std::string("SUB ") + RelayPeerPrefix + impl.relayId + " 2\r\n";
    setup += "PING\r\n";

    if (!relaySendAll(impl, setup)) return;
    parseRelayInput(impl);
}

} // namespace

bool DatagramSocket::open(std::uint16_t port, bool allowBroadcast)
{
    close();
    impl_->error.clear();
    impl_->relayError.clear();

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        impl_->error = winSockError("WSAStartup failed");
        return false;
    }
    impl_->winsockStarted = true;

    impl_->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == INVALID_SOCKET) {
        impl_->error = winSockError("socket failed");
        close();
        return false;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(impl_->socket, FIONBIO, &nonBlocking) != 0) {
        impl_->error = winSockError("ioctlsocket failed");
        close();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(impl_->socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    if (allowBroadcast) {
        BOOL broadcast = TRUE;
        if (setsockopt(impl_->socket, SOL_SOCKET, SO_BROADCAST,
                       reinterpret_cast<const char*>(&broadcast), sizeof(broadcast)) != 0) {
            impl_->error = winSockError("setsockopt(SO_BROADCAST) failed");
            close();
            return false;
        }
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);

    if (::bind(impl_->socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        impl_->error = winSockError("bind failed");
        close();
        return false;
    }

    sockaddr_in bound{};
    int boundSize = sizeof(bound);
    if (getsockname(impl_->socket, reinterpret_cast<sockaddr*>(&bound), &boundSize) == 0) {
        impl_->port = ntohs(bound.sin_port);
    } else {
        impl_->port = port;
    }

    // Keep the existing UDP path as the fast path. Every socket also attempts
    // an outbound relay connection so LAN discovery gets an Internet fallback
    // without changing the game protocol or requiring port forwarding.
    tryOpenRelay(*impl_, port != 0);

    return true;
}

void DatagramSocket::close()
{
    if (!impl_) return;

    if (impl_->relaySocket != INVALID_SOCKET) {
        closesocket(impl_->relaySocket);
        impl_->relaySocket = INVALID_SOCKET;
    }
    impl_->relayRx.clear();
    impl_->relayPackets.clear();
    impl_->relayDiscoverySubscribed = false;
    impl_->relayId.clear();

    if (impl_->socket != INVALID_SOCKET) {
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
    }
    impl_->port = 0;

    if (impl_->winsockStarted) {
        WSACleanup();
        impl_->winsockStarted = false;
    }
}

bool DatagramSocket::isOpen() const noexcept
{
    return impl_ && impl_->socket != INVALID_SOCKET;
}

std::uint16_t DatagramSocket::localPort() const noexcept
{
    return impl_ ? impl_->port : 0;
}

const std::string& DatagramSocket::lastError() const noexcept
{
    return impl_->error;
}

bool DatagramSocket::send(const Endpoint& endpoint, const void* data, std::size_t size)
{
    if (!isOpen() || !endpoint || !data || size == 0 || size > 65507) return false;

    if (endpoint.address.rfind("nats:", 0) == 0) {
        const std::string targetId = endpoint.address.substr(5);
        if (!validRelayId(targetId) || impl_->relaySocket == INVALID_SOCKET) return false;

        const std::string envelope = makeRelayEnvelope(*impl_, data, size);
        const bool sent = relayPublish(*impl_, std::string(RelayPeerPrefix) + targetId,
                                       envelope.data(), envelope.size());
        if (sent) unsubscribeRelayDiscovery(*impl_);
        return sent;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(endpoint.port);

    const bool isBroadcast = endpoint.address == "255.255.255.255" || endpoint.address == "<broadcast>";
    if (isBroadcast) {
        remote.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    } else if (inet_pton(AF_INET, endpoint.address.c_str(), &remote.sin_addr) != 1) {
        impl_->error = "invalid IPv4 endpoint: " + endpoint.address;
        return false;
    }

    const int sentBytes = sendto(
        impl_->socket,
        static_cast<const char*>(data),
        static_cast<int>(size),
        0,
        reinterpret_cast<const sockaddr*>(&remote),
        sizeof(remote));

    bool udpSent = sentBytes == static_cast<int>(size);
    if (sentBytes == SOCKET_ERROR) {
        impl_->error = winSockError("sendto failed");
        udpSent = false;
    }

    if (isBroadcast && impl_->relaySocket != INVALID_SOCKET) {
        const std::string envelope = makeRelayEnvelope(*impl_, data, size);
        const bool relaySent = relayPublish(*impl_, RelayDiscoverySubject,
                                            envelope.data(), envelope.size());
        return udpSent || relaySent;
    }

    if (udpSent && !isBroadcast) unsubscribeRelayDiscovery(*impl_);
    return udpSent;
}

int DatagramSocket::receive(void* data, std::size_t capacity, Endpoint& from)
{
    from = {};
    if (!isOpen() || !data || capacity == 0) return -1;

    sockaddr_in remote{};
    int remoteSize = sizeof(remote);
    const int received = recvfrom(
        impl_->socket,
        static_cast<char*>(data),
        static_cast<int>(std::min<std::size_t>(capacity, 65535)),
        0,
        reinterpret_cast<sockaddr*>(&remote),
        &remoteSize);

    if (received != SOCKET_ERROR) {
        char address[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &remote.sin_addr, address, static_cast<DWORD>(sizeof(address)))) {
            impl_->error = winSockError("inet_ntop failed");
            return -1;
        }

        from.address = address;
        from.port = ntohs(remote.sin_port);
        return received;
    }

    const int code = WSAGetLastError();
    if (code != WSAEWOULDBLOCK) {
        impl_->error = "recvfrom failed (WSA " + std::to_string(code) + ")";
        return -1;
    }

    pollRelay(*impl_);
    if (impl_->relayPackets.empty()) return 0;

    Impl::RelayPacket packet = std::move(impl_->relayPackets.front());
    impl_->relayPackets.pop_front();
    if (packet.data.size() > capacity) {
        impl_->error = "relay packet larger than receive buffer";
        return -1;
    }

    std::memcpy(data, packet.data.data(), packet.data.size());
    from = std::move(packet.from);
    return static_cast<int>(packet.data.size());
}

} // namespace ra::net
