#ifndef PORT_MANAGER_H
#define PORT_MANAGER_H

#include <assert.h>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

class port_manager {
  private:
    vector<uint16_t> ports;
    vector<bool> used_ports;

    struct sockaddr_in address;
    int reserved_fd = -1;

  public:
    port_manager(uint16_t base_port = 9000, size_t count = 5);
    int reserve_port(uint16_t port);

    void release_port(uint16_t port);
    // void find_available_ports(); //does not reserve immediately just probing others might get the port
    //  bool is_port_available(uint16_t port);
};
#endif
