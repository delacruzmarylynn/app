#ifndef DOWNLOAD_MANAGER_H
#define DOWNLOAD_MANAGER_H

#include <algorithm>
#include <assert.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../inc/Client.h"
#include "../inc/downloader.h"
#include "../inc/network_file.h"
#include "../inc/seeder_resolver.h"

enum class DownloadStatus { Idle,
                            Downloading,
                            Completed,
                            Stalled,
                            Failed,
                            Cancelled };

struct download {
    string file_key;
    int file_id = 1;
    string file_name;
    uint64_t total_bytes = 0;

    atomic<uint64_t> downloaded_bytes{0};
    atomic<DownloadStatus> state{DownloadStatus::Idle};

    vector<Seeder> seeders;
    atomic<uint64_t> next_offset{0}; // dynamic scheduler
    atomic<bool> failed{false};
    atomic<int> active_workers{0};
    atomic<bool> refreshing{false};
    atomic<time_t> last_resolve{0};

    uint32_t chunk_size = 32;
    uint64_t total_chunks = 0;
    std::deque<std::atomic<uint8_t>> chunk_state;
    std::atomic<uint64_t> chunks_done{0};
    std::atomic<int> usable_seeder_workers{0}; // alive seeders/workers

    string output_path;
    string final_path;
    string error;

    // per download threading state
    vector<pthread_t> workers;
    std::atomic<bool> cancel_requested{false};
    // if 2 threads are the same value and race condition
    // manage traffic
    // sync and read status is sync
    std::atomic<bool> worker_running{false};

    // per download mutex protects output_path/error( and any other non-atomic fields)
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    // separate lock for file writes (recommended)
    pthread_mutex_t io_mtx = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_t seeders_m = PTHREAD_MUTEX_INITIALIZER;
    std::unordered_set<uint16_t> seeder_ports; // dedupe new seeders

    download() {
        pthread_mutex_init(&m, nullptr);
        pthread_mutex_init(&io_mtx, nullptr);
        pthread_mutex_init(&seeders_m, nullptr);
    }

    ~download() {
        pthread_mutex_destroy(&m);
        pthread_mutex_destroy(&io_mtx);
        pthread_mutex_destroy(&seeders_m);
    }

    // non-copyable (mutex members)
    download(const download &) = delete;
    download &operator=(const download &) = delete;
};

class download_manager {
  private:
    uint16_t my_port;

    mutable pthread_mutex_t jobs_mtx; // protect jobs map
    std::unordered_map<std::string, std::shared_ptr<download>> jobs;

    struct WorkerArg {
        download_manager *self;
        std::shared_ptr<download> job;
        Seeder seeder;
    };

    shared_ptr<SeederResolver> resolver;

    static void *worker_entry(void *arg);
    void worker_run(std::shared_ptr<download> job, const Seeder &seeder);

    static void ensure_dir(const std::string &path); // download active_downloads[];

    static bool file_exists(const std::string &path);

    uint64_t file_size(const std::string &path);

    std::shared_ptr<download> find_job_by_key_locked(const std::string &key) const;

    bool spawn_worker_for(const std::shared_ptr<download> &job, const Seeder &s);
    void mark_seeder_dead(const std::shared_ptr<download> &job, uint16_t port);

    void refresh_seeders(const std::shared_ptr<download> &job);

  public:
    explicit download_manager(uint16_t port);
    ~download_manager();

    void set_seeder_resolver(std::shared_ptr<SeederResolver> r);

    void tick();

    bool add_download(const network_file &nf, const vector<Seeder> &seeders);
    void cancel(const std::string &file_key);
    std::string get_status_string() const;

    bool all_downloads_completed() const;
    bool has_active_downloads() const;
    bool has_downloading() const;
};

#endif
