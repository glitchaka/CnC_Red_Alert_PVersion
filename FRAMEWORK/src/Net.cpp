#include <ra/Net.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace ra::net {
namespace {

std::string winSockError(const char* prefix)
{
    return std::string(prefix) + " (WSA " + std::to_string(WSAGetLastError()) + ")";
}

} // namespace

struct DatagramSocket::Impl {
    SOCKET socket{INVALID_SOCKET};
    bool winsockStarted{false};
    std::uint16_t port{0};
    std::string error{};
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

bool DatagramSocket::open(std::uint16_t port, bool allowBroadcast)
{
    close();
    impl_->error.clear();

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

    return true;
}

void DatagramSocket::close()
{
    if (!impl_) return;

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

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(endpoint.port);

    if (endpoint.address == "255.255.255.255" || endpoint.address == "<broadcast>") {
        remote.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    } else if (inet_pton(AF_INET, endpoint.address.c_str(), &remote.sin_addr) != 1) {
        impl_->error = "invalid IPv4 endpoint: " + endpoint.address;
        return false;
    }

    const int sent = sendto(
        impl_->socket,
        static_cast<const char*>(data),
        static_cast<int>(size),
        0,
        reinterpret_cast<const sockaddr*>(&remote),
        sizeof(remote));

    if (sent == SOCKET_ERROR) {
        impl_->error = winSockError("sendto failed");
        return false;
    }

    return sent == static_cast<int>(size);
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

    if (received == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        if (code == WSAEWOULDBLOCK) return 0;
        impl_->error = "recvfrom failed (WSA " + std::to_string(code) + ")";
        return -1;
    }

    char address[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &remote.sin_addr, address, static_cast<DWORD>(sizeof(address)))) {
        impl_->error = winSockError("inet_ntop failed");
        return -1;
    }

    from.address = address;
    from.port = ntohs(remote.sin_port);
    return received;
}

} // namespace ra::net
