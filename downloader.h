#ifndef DOWNLOADER_H
#define DOWNLOADER_H

#include <assert.h>
#include <cstdint>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "../inc/Client.h"
#include "../inc/seeder.h"

class Client;

struct ChunkResult {
    bool ok = false;
    std::vector<uint8_t> data;
    std::string error;
};

class Downloader {
  private:
    // File file;

    // long bytes_download;

    // long total_bytes;

    // download_status status;

    // Seeder seeder[];

    // Network helpers
    static uint64_t htonll(uint64_t x);
    static uint64_t ntohll(uint64_t x);

    static bool send_i32(Client &c, int32_t v);
    static bool send_u32(Client &c, uint32_t v);
    static bool send_u64(Client &c, uint64_t v);
    static bool send_string(Client &c, const std::string &s);

    static bool recv_i32(Client &c, int32_t &out);
    static bool recv_u32(Client &c, uint32_t &out);
    static bool recv_string(Client &c, std::string &out);

  public:
    static constexpr int GET_CHUNK_CHOICE = 2;
    ChunkResult fetch_chunk_on_connection(Client &c, const std::string &file_key, uint64_t offset, uint32_t length);
    // void update_progress();

    // // void set progress()();

    // void is_duplicate();
};
#endif
