#include <assert.h>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "../inc/Client.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

Client::Client(int port) {
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0)
        throw std::runtime_error("Socket creation failed");

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
        throw std::runtime_error("Invalid address");

    if (connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        throw std::runtime_error("Connection failed");

    // cout << "Connected to port:" << port << "\n";
}

void Client::send_choice(int choice) {
    int net_choice = htonl(choice);
    send_all(&net_choice, sizeof(net_choice));
}

bool Client::recv_all(int client_socket, void *data, size_t len) {
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

bool Client::send_all(const void *data, size_t len) {
    const char *ptr = static_cast<const char *>(data);
    while (len > 0) {
        ssize_t n = send(client_fd, ptr, len, 0);
        if (n <= 0)
            return false;
        ptr += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool Client::recv_exact(void *data, size_t len) {
    return recv_all(client_fd, data, len);
}

string Client::receive_response() {
    uint32_t net_len = 0;

    // Read exactly 4 bytes length
    if (!recv_all(client_fd, &net_len, sizeof(net_len))) {
        return "";
    }

    uint32_t len = ntohl(net_len);

    string out;
    out.resize(len);

    // Read exactly len bytes payload
    if (len > 0 && !recv_all(client_fd, out.data(), len)) {
        return "";
    }

    return out;
}

void Client::send_string(const std::string &msg) {

    uint32_t len = static_cast<uint32_t>(msg.size());
    uint32_t net_len = htonl(len);

    if (!send_all(&net_len, sizeof(net_len)))
        throw std::runtime_error("send_string: failed to send length");

    if (len > 0 && !send_all(msg.data(), len))
        throw std::runtime_error("send_string: failed to send payload");
}

// [4 - bytes length] "filename\t12345"
void Client::send_have_request(const std::string &name, uint64_t size) {

    std::string payload = name + "\t" + std::to_string(size);
    send_string(payload);
}

// void Client::requets_file_list()
// {
// }

// void Client::request_download()
// {
// }

Client::~Client() {
    close_connection();
}

void Client::close_connection() {
    if (client_fd != -1) {
        close(client_fd);
        client_fd = -1;
    }
}
