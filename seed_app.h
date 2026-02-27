#ifndef SEED_APP_H
#define SEED_APP_H

#include <algorithm>
#include <assert.h>
#include <dirent.h>
#include <iostream>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <sstream>
#include <stdbool.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "../inc/Client.h"
#include "../inc/File.h"
#include "../inc/Host.h"
#include "../inc/download_manager.h"
#include "../inc/network_file.h"
#include "../inc/port_manager.h"
#include "../inc/seeder.h"
#include "../inc/seeder_probe.h"
#include "../inc/seeder_resolver.h"

using namespace std;

class AppSeederResolver;
class seed_app {
  private:
    bool is_running{false};

    port_manager pm;
    network_file nf;

    int fd;
    uint16_t chosen_port;

    optional<download_manager> dm;

    std::unique_ptr<AppSeederResolver> resolver_;

  public:
    seed_app();

    void start();

    int get_fd() const;
    int get_port() const;

    void display_menu();
    static void *server_entry(void *arg);

    void set_seeder_resolver(std::shared_ptr<SeederResolver> r);

    static vector<network_file> list_unique_files(const vector<int> &peers, int port);

    static const network_file *find_by_key(const std::vector<network_file> &files, const string &key);

    void tick_downloads();
    bool start_download(const network_file &nf, const vector<Seeder> &seeders);
    string download_status() const;

    bool downloads_completed() const;
    bool has_downloads() const;

    void exit();

    ~seed_app();
};

#endif
