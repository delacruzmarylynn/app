#include "../inc/downloader.h"
#include <arpa/inet.h>
#include <assert.h>
#include <iostream>
#include <list>
#include <string>
#include <vector>

uint64_t Downloader::htonll(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(x & 0xFFFFFFFFULL))) << 32) |
           static_cast<uint64_t>(htonl(static_cast<uint32_t>(x >> 32)));
#else
    return x;
#endif
}

uint64_t Downloader::ntohll(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (static_cast<uint64_t>(ntohl(static_cast<uint32_t>(x & 0xFFFFFFFFULL))) << 32) |
           static_cast<uint64_t>(ntohl(static_cast<uint32_t>(x >> 32)));
#else
    return x;
#endif
}

bool Downloader::send_i32(Client &c, int32_t v) {
    uint32_t net = htonl(static_cast<uint32_t>(v));
    return c.send_all(&net, sizeof(net));
}
bool Downloader::send_u32(Client &c, uint32_t v) {
    uint32_t net = htonl(v);
    return c.send_all(&net, sizeof(net));
}
bool Downloader::send_u64(Client &c, uint64_t v) {
    uint64_t net = htonll(v);
    return c.send_all(&net, sizeof(net));
}
bool Downloader::send_string(Client &c, const std::string &s) {
    if (!send_u32(c, static_cast<uint32_t>(s.size())))
        return false;
    if (!s.empty())
        return c.send_all(s.data(), s.size());
    return true;
}

bool Downloader::recv_i32(Client &c, int32_t &out) {
    uint32_t net = 0;
    if (!c.recv_exact(&net, sizeof(net)))
        return false;
    out = static_cast<int32_t>(ntohl(net));
    return true;
}

bool Downloader::recv_u32(Client &c, uint32_t &out) {
    uint32_t net = 0;
    if (!c.recv_exact(&net, sizeof(net)))
        return false;
    out = ntohl(net);
    return true;
}

bool Downloader::recv_string(Client &c, std::string &out) {
    uint32_t len = 0;
    if (!recv_u32(c, len))
        return false;
    out.resize(len);
    if (len > 0 && !c.recv_exact(out.data(), len))
        return false;
    return true;
}

ChunkResult Downloader::fetch_chunk_on_connection(Client &c, const std::string &file_key, uint64_t offset, uint32_t length) {

    ChunkResult cr;

    constexpr uint32_t CHUNK_SIZE = 32;
    if (length > CHUNK_SIZE)
        length = CHUNK_SIZE;

    // Send request
    c.send_choice(GET_CHUNK_CHOICE);

    if (!send_string(c, file_key)) {
        cr.error = "send key failed";
        return cr;
    }
    if (!send_u64(c, offset)) {
        cr.error = "send offset failed";
        return cr;
    }
    if (!send_u32(c, length)) {
        cr.error = "send length failed";
        return cr;
    }

    // Receive response: [i32 status] then either [u32 got][bytes] or [string err]
    int32_t status = 0;
    if (!recv_i32(c, status)) {
        cr.error = "recv status failed";
        return cr;
    }

    if (status != 0) {
        std::string err;
        if (!recv_string(c, err))
            err = "server error (recv err failed)";
        cr.error = err;
        return cr;
    }

    uint32_t got = 0;
    if (!recv_u32(c, got)) {
        cr.error = "recv data length failed";
        return cr;
    }

    // defensive
    if (got > CHUNK_SIZE) {
        cr.error = "protocol violation: server sent >32 bytes";
        return cr;
    }

    cr.data.resize(got);
    if (got > 0 && !c.recv_exact(cr.data.data(), got)) {
        cr.error = "recv data failed";
        cr.data.clear();
        return cr;
    }

    cr.ok = true;
    return cr;
}
// void download::start() {
// }

// void download::update_progress() {
// }

// // void download::set progress()()
// // {
// // }

// void download::is_duplicate() {
// }