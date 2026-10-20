#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ra::net {

struct Endpoint {
    std::string address{};
    std::uint16_t port{0};

    explicit operator bool() const noexcept { return !address.empty() && port != 0; }
};

class DatagramSocket {
public:
    DatagramSocket();
    ~DatagramSocket();

    DatagramSocket(const DatagramSocket&) = delete;
    DatagramSocket& operator=(const DatagramSocket&) = delete;
    DatagramSocket(DatagramSocket&&) noexcept;
    DatagramSocket& operator=(DatagramSocket&&) noexcept;

    bool open(std::uint16_t port = 0, bool allowBroadcast = true);
    void close();

    bool isOpen() const noexcept;
    std::uint16_t localPort() const noexcept;
    const std::string& lastError() const noexcept;

    bool send(const Endpoint& endpoint, const void* data, std::size_t size);
    int receive(void* data, std::size_t capacity, Endpoint& from);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ra::net
