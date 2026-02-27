#include <arpa/inet.h>
#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <list>
#include <string>
#include <unistd.h>
#include <vector>

#include "../inc/Host.h"
#include "../inc/seed_app.h"

using namespace std;

Host::Host(int socket_fd, uint16_t port_num) : fd(socket_fd), port(port_num) {}

const vector<int> &Host::connected_ports() const { return connected_; }

void Host::accept_clients() {
    is_running = true;
    if (listen(fd, 3) < 0) {
        perror("listen");
        return;
    }

    cout << "Listening on port " << port << "...\n";

    // pthread_create(&accept_thread, nullptr, &Host::accept_loop_entry, this);
    while (is_running) {
        int new_socket = accept(fd, nullptr, nullptr);
        if (new_socket < 0) {
            // perror("accept failed");
            continue;
        }

        // printf("Client connected.\n");

        // printf("%d\n", new_socket);

        ClientThread *arg = new ClientThread;
        arg->host = this;
        arg->client_socket = new_socket;

        pthread_t thread_id;

        if (pthread_create(&thread_id, NULL, Host::handle_client_entry, arg) != 0) {
            perror("pthread_create failed");
            close(new_socket);
            delete arg;
            continue;
        }

        pthread_detach(thread_id);
    }

    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}

vector<int> Host::connect_to_peers(initializer_list<int> peers) {
    vector<int> connected;

    for (int p : peers) {
        if (p == port)
            continue;

        try {
            int fd = connect_to_peer(p);
            if (fd >= 0)
                connected.push_back(p);
        } catch (const exception &e) {
            // cerr << "Connect to " << p << " failed: " << e.what() << '\n';
        }
    }
    return connected;
}

int Host::connect_to_peer(int peer_port) {

    int peer_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (peer_fd < 0)
        throw runtime_error("Socket creation failed");

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(peer_port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(peer_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(peer_fd);
        throw runtime_error("Connection failed");
    }
    // cout << "Peer connected to server " << "port: " << peer_port << " " << peer_fd << "\n";

    pthread_t thread_id;

    PeerThread *arg = new PeerThread;
    arg->host = this;
    arg->fd = peer_fd;
    arg->peer_port = peer_port;

    if (pthread_create(&thread_id, nullptr, Host::handle_peer_entry, arg) != 0) {
        perror("pthread create handle peer failed");
        close(peer_fd);
        return -1;
    }

    pthread_detach(thread_id);

    return peer_fd;
}

void *Host::handle_peer_entry(void *arg) {
    PeerThread *info = static_cast<PeerThread *>(arg);

    Host *host = info->host;
    int peer_fd = info->fd;
    // int peer_port = info->peer_port;

    delete info;

    // cout << "Connected to port: " << peer_port << "(fd=" << peer_fd << ")\n";

    host->handle_peer(peer_fd);

    return NULL;
}

void Host::handle_peer(int peer_fd) {
    char buffer[1024];

    while (true) {
        ssize_t n = recv(peer_fd, buffer, sizeof(buffer) - 1, 0);

        if (n == 0) {
            // cout << "Peer " << peer_fd << " disconnected\n";
            break;
        }

        if (n < 0) {
            perror("recv");
            break;
        }

        buffer[n] = '\0';
        // cout << "Peer" << peer_fd << " " << buffer << "\n";

        ssize_t sent = send(peer_fd, buffer, n, 0);
        if (sent < 0) {
            perror("send");
            break;
        }
    }
    close(peer_fd);
}
bool Host::send_response(int client_socket, const string &reply) {
    bool ret = true;
    const char *stage = "unknown";

    uint32_t len = static_cast<uint32_t>(reply.size());
    uint32_t net_len = htonl(len);

    do {
        stage = "header";
        ret = send_all(client_socket, &net_len, sizeof(net_len));
        if (!ret)
            break;

        if (len > 0) {
            stage = "payload";
            ret = send_all(client_socket, reply.data(), len);
            if (!ret)
                break;
        }
    } while (0);
    if (!ret) {
        fprintf(stderr, "send failed at stage: %s\n", stage);
        perror("send");
    }
    return ret;
}

bool Host::send_all(int client_socket, const void *data, size_t len) {
    const char *ptr = static_cast<const char *>(data);

    while (len > 0) {
        ssize_t n = send(client_socket, ptr, len, 0);

        if (n <= 0) {
            return false;
        }

        ptr += n;
        // len = static_cast<size_t>(n) - len;
        // len += static_cast<size_t>(n);
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool Host::recv_all(int client_socket, void *data, size_t len) {
    char *ptr = static_cast<char *>(data);

    while (len > 0) {
        ssize_t n = recv(client_socket, ptr, len, 0);

        if (n <= 0) {
            // n == 0 => peer closed connection
            // n < 0  => error
            return false;
        }

        ptr += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

void *Host::handle_client_entry(void *arg) {
    ClientThread *info = static_cast<ClientThread *>(arg);
    Host *host = info->host;
    int client_socket = info->client_socket;
    delete info;

    host->handle_client(client_socket);
    return NULL;
}

void Host::handle_client(int client_socket) {
    while (true) {
        int net_choice = 0;

        bool ok = recv_all(client_socket, &net_choice, sizeof(net_choice));

        if (!ok) {
            // cout << "Client disconnected. \n";
            break;
        }

        int choice = ntohl(net_choice);
        // printf("%d\n", choice);
        string reply;

        switch (choice) {
        case 1:
            reply = list_available_files();
            if (!send_response(client_socket, reply)) {
                perror("send_respoins failed");
                break;
            }
            // reply = chunks;
            break;
        case 2:
            if (!handle_get_chunk(client_socket)) {
                perror("chunk handlre failed, closing client.\n");
                break;
            }
            break;
            // handle the get chunk smoething
            // un dwnloader fetch chunk implementation

            // download
            // send
            // choice 2
            // file_key
            // offset
            // len

            // receive
            // status
            // if ok:bytes_return and arw bytes

            // host
            // read file_key, offset, len
            // open file
            // read requested bytes
            // send status, size and bytes
        case 4: {

            std::string payload;

            // call static recv_string with 'this'
            if (!Host::recv_string(this, client_socket, payload)) {
                send_response(client_socket, "NO");
                break;
            }

            // payload: "<name>\t<size>"
            auto tab = payload.find('\t');
            if (tab == std::string::npos) {
                send_response(client_socket, "NO");
                break;
            }

            std::string name = payload.substr(0, tab);

            std::uint64_t size = 0;
            try {
                size = std::stoull(payload.substr(tab + 1));
            } catch (...) {
                send_response(client_socket, "NO");
                break;
            }

            // make has_file a Host member: this->has_file(...)
            bool ok = this->has_file(name, size);

            send_response(client_socket, ok ? "YES" : "NO");
            break;
        }

        default:
            reply = "Invalid choice\n"; // return
            break;
        }
    }
    close(client_socket);
}

bool Host::has_file(const std::string &name, std::uint64_t size) const {
    // Same directory used in list_available_files() and handle_get_chunk()
    std::string dir_path = "./bin/" + std::to_string(port);
    std::string full_path = dir_path + "/" + name;

    struct stat st{};
    if (stat(full_path.c_str(), &st) != 0) {
        return false; // file doesn't exist
    }
    if (!S_ISREG(st.st_mode)) {
        return false; // not a regular file
    }
    return static_cast<std::uint64_t>(st.st_size) == size;
}

string Host::list_available_files() const {

    // open directory
    // entry files
    // name + size

    // error 2 knows index = 0 chunk index
    // 64bytes usa ka file 2 chunks 0 < chunk < total_chunk
    // 100%
    // index 0 and index 1
    // inde x 2 and index 3

    string dir_path = "./bin/" + to_string(port);

    DIR *dir = opendir(dir_path.c_str());
    if (!dir) {
        return "ERR\tNo directory found: " + dir_path + "\n";
    }

    string out;
    struct dirent *entry;

    while ((entry = readdir(dir)) != nullptr) {
        string name = entry->d_name;

        // Skip "." and ".."
        if (name == "." || name == "..")
            continue;

        string full_path = dir_path + "/" + name;

        struct stat st{};
        if (stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            out += name;
            out += "\t";
            out += to_string((long long)st.st_size);
            out += "\n";
        }
    }

    closedir(dir);

    if (out.empty())
        out = "EMPTY\t0\n";
    return out;
}

// // --- 64-bit byte-order helpers---
// static uint64_t htonll(uint64_t x) {
// #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
//     return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(x & 0xFFFFFFFFULL))) << 32) |
//            static_cast<uint64_t>(htonl(static_cast<uint32_t>(x >> 32)));
// #else
//     return x;
// #endif
// }

static uint64_t ntohll(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (static_cast<uint64_t>(ntohl(static_cast<uint32_t>(x & 0xFFFFFFFFULL))) << 32) |
           static_cast<uint64_t>(ntohl(static_cast<uint32_t>(x >> 32)));
#else
    return x;
#endif
}

// --- send primitives ---
static bool send_i32(Host *h, int sock, int32_t v) {
    uint32_t net = htonl(static_cast<uint32_t>(v));
    return h->send_all(sock, &net, sizeof(net));
}

static bool send_u32(Host *h, int sock, uint32_t v) {
    uint32_t net = htonl(v);
    return h->send_all(sock, &net, sizeof(net));
}

// static bool send_u64(Host *h, int sock, uint64_t v) {
//     uint64_t net = htonll(v);
//     return h->send_all(sock, &net, sizeof(net));
// }

// --- recv primitives ---
// static bool recv_i32(Host *h, int sock, int32_t &out) {
//     uint32_t net = 0;
//     if (!h->recv_all(sock, &net, sizeof(net)))
//         return false;
//     out = static_cast<int32_t>(ntohl(net));
//     return true;
// }

static bool recv_u32(Host *h, int sock, uint32_t &out) {
    uint32_t net = 0;
    if (!h->recv_all(sock, &net, sizeof(net)))
        return false;
    out = ntohl(net);
    return true;
}

static bool recv_u64(Host *h, int sock, uint64_t &out) {
    uint64_t net = 0;
    if (!h->recv_all(sock, &net, sizeof(net)))
        return false;
    out = ntohll(net);
    return true;
}

// --- string: [u32 len][bytes...] ---
static bool send_string(Host *h, int sock, const string &s) {
    if (s.size() > UINT32_MAX)
        return false;
    if (!send_u32(h, sock, static_cast<uint32_t>(s.size())))
        return false;
    if (!s.empty() && !h->send_all(sock, s.data(), s.size()))
        return false;
    return true;
}

bool Host::recv_string(Host *h, int sock, string &out) {
    uint32_t len = 0;
    if (!recv_u32(h, sock, len))
        return false;

    out.resize(static_cast<size_t>(len));
    if (len > 0) {
        if (!h->recv_all(sock, &out[0], static_cast<size_t>(len)))
            return false;
    }
    return true;
}

bool Host::send_chunk_response(int client_socket, int32_t status, const vector<uint8_t> &data, const string &err) {
    if (!send_i32(this, client_socket, status))
        return false;

    if (status == 0) {
        if (!send_u32(this, client_socket, static_cast<uint32_t>(data.size())))
            return false;
        if (!data.empty() && !send_all(client_socket, data.data(), data.size()))
            return false;
        return true;
    } else {
        return send_string(this, client_socket, err);
    }
}

bool Host::handle_get_chunk(int client_socket) {
    string file_key;
    uint64_t offset = 0;
    uint32_t length = 0;

    if (!recv_string(this, client_socket, file_key)) {
        send_chunk_response(client_socket, -1, {}, "Failed to read file_key");
        return true; // responded
    }
    if (!recv_u64(this, client_socket, offset)) {
        send_chunk_response(client_socket, -2, {}, "Failed to read offset");
        return true;
    }
    if (!recv_u32(this, client_socket, length)) {
        send_chunk_response(client_socket, -3, {}, "Failed to read length");
        return true;
    }

    // cout << "[HOST] file_key=" << file_key
    //      << " offset=" << offset
    //      << " length=" << length
    //      << "\n";

    constexpr uint32_t CHUNK_SIZE = 32;
    if (length > CHUNK_SIZE)
        length = CHUNK_SIZE;

    // file_key format: "name\t<size>" -> extract filename
    auto tab = file_key.find('\t');
    if (tab == string::npos) {
        send_chunk_response(client_socket, -4, {}, "Bad file_key format");
        return true;
    }

    string filename = file_key.substr(0, tab);

    // Seeder path scheme: ./bin/<port>/<filename>
    string path = "./bin/" + to_string(port) + "/" + filename;

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        send_chunk_response(client_socket, -5, {}, "File not found: " + filename);
        return true;
    }

    // Determine file size (for bounds check)
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        send_chunk_response(client_socket, -6, {}, "Seek end failed");
        return true;
    }

    long file_size_long = ftell(f);
    if (file_size_long < 0) {
        fclose(f);
        send_chunk_response(client_socket, -7, {}, "ftell failed");
        return true;
    }
    uint64_t file_size = static_cast<uint64_t>(file_size_long);

    // Bounds: offset can equal file_size (EOF), but not exceed it
    if (offset > file_size) {
        fclose(f);
        send_chunk_response(client_socket, -8, {}, "Offset beyond EOF");
        return true;
    }

    // Seek to requested offset
    if (fseek(f, static_cast<long>(offset), SEEK_SET) != 0) {
        fclose(f);
        send_chunk_response(client_socket, -9, {}, "Seek failed");
        return true;
    }

    // Read up to 'length' bytes
    vector<uint8_t> buf(length);
    size_t n = fread(buf.data(), 1, length, f);
    fclose(f);

    buf.resize(n); // <= length, can be 0 at EOF
    send_chunk_response(client_socket, 0, buf, "");
    // cout << "[GET_CHUNK] file=" << filename
    //      << " offset=" << offset
    //      << " len=" << length << "\n";
    return true;
}

void Host::stop_server() {
    is_running = false;
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}