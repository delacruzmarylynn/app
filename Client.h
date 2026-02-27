#ifndef CLIENT_H
#define CLIENT_H

#include <assert.h>
#include <iostream>
#include <list>
#include <string>
#include <vector>

using namespace std;

class Client {
  private:
    int client_fd;
    int client_id;

    static bool recv_all(int client_socket, void *data, size_t len);

  public:
    Client(int port);
    void send_choice(int choice);
    bool send_all(const void *data, size_t len);

    bool recv_exact(void *data, size_t len);

    void send_string(const std::string &msg);
    void send_have_request(const std::string &name, uint64_t size);

    string receive_response();
    void close_connection();
    ~Client();
};
#endif
