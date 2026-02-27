#ifndef HOST_H
#define HOST_H

#include <assert.h>
#include <iostream>
#include <list>
#include <netinet/in.h> //for address
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "../inc/download_manager.h"
#include "../inc/downloader.h"
#include "../inc/seed_app.h"

using namespace std;

class Host;

struct PeerThread {
    Host *host;
    int fd;
    int peer_port;
};

struct ClientThread {
    Host *host;
    int client_socket;
};

class Host {
  private:
    int fd;
    uint16_t port;

    vector<int> connected_;

    bool is_running;

    static void *handle_peer_entry(void *arg);
    void handle_peer(int peer_fd);

    static void *handle_client_entry(void *arg);
    void handle_client(int client_socket);

    bool has_file(const std::string &name, std::uint64_t size) const;

  public:
    Host(int socket_fd, uint16_t port_num);
    void accept_clients();
    int connect_to_peer(int peer_port);
    void stop_server();

    const vector<int> &connected_ports() const;

    static bool send_all(int client_socket, const void *data, size_t len);
    static bool recv_all(int client_socket, void *data, size_t len);

    bool send_response(int client_socket, const std::string &reply);
    static bool recv_string(Host *h, int sock, string &out);
    bool send_chunk_response(int client_socket, int32_t status, const std::vector<uint8_t> &data, const std::string &err);
    bool handle_get_chunk(int client_socket);
    vector<int> connect_to_peers(std::initializer_list<int> peers);

    // void remove_client();

    string list_available_files() const;

    // void locate_seeders(int file_id);

    // void send_files(file file);
};

#endif
