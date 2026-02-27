#include <assert.h>
#include <iostream>
#include <list>
#include <string>
#include <vector>

using namespace std;

#include "../inc/port_manager.h"

port_manager::port_manager(uint16_t base_port, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        ports.push_back(base_port + i);
        used_ports.push_back(false);
    }
}

int port_manager::reserve_port(uint16_t port) {
    // Check if port is in our list and already used
    for (size_t i = 0; i < ports.size(); ++i) {
        if (ports[i] == port) {
            if (used_ports[i]) {
                std::cerr << "[PortManager] Port " << port << " already in use (tracked)\n";
                return -1;
            }

            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                perror("socket failed");
                return -1;
            }

            std::memset(&address, 0, sizeof(address));
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port);

            // int opt = 1;
            // setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
                perror("bind failed");
                close(fd);
                return -1;
            }

            used_ports[i] = true;
            reserved_fd = fd;
            return fd;
        }
    }

    std::cerr << "[PortManager] Port " << port << " not in managed range\n";
    return -1;
}

void port_manager::release_port(uint16_t port) {

    for (size_t i = 0; ports.size(); i++) {
        if (ports[i] == port) {
            used_ports[i] = false;
            break;
        }
    }
}
