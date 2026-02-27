
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <list>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../inc/download_manager.h"

using namespace std;

static string state_to_str(DownloadStatus s) {
    switch (s) {
    case DownloadStatus::Idle:
        return "Idle";
    case DownloadStatus::Downloading:
        return "Downloading";
    case DownloadStatus::Stalled:
        return "Stalled";
    case DownloadStatus::Completed:
        return "Completed";
    case DownloadStatus::Failed:
        return "Failed";

    case DownloadStatus::Cancelled:
        return "Cancelled";
    }
    return "Unknown";
};

void download_manager::ensure_dir(const string &path) {
    mkdir(path.c_str(), 0777);
}

bool download_manager::file_exists(const std::string &path) {
    struct stat st{};
    return (stat(path.c_str(), &st) == 0) && S_ISREG(st.st_mode);
}

uint64_t download_manager::file_size(const std::string &path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
        return 0;
    return static_cast<uint64_t>(st.st_size);
}

std::shared_ptr<download> download_manager::find_job_by_key_locked(const std::string &key) const {
    auto it = jobs.find(key);
    if (it == jobs.end())
        return nullptr;
    return it->second;
}

// resolver injection (interface)
// any class that implements seederresolver can be pluggedin
// why virtual, because resolver->resolve will call the derived implementation at runtime
void download_manager::set_seeder_resolver(std::shared_ptr<SeederResolver> r) {
    resolver = std::move(r);
}

bool download_manager::spawn_worker_for(const std::shared_ptr<download> &job, const Seeder &s) {
    // If job is cancelling or failed, don't spawn new worker
    if (job->cancel_requested.load() || job->failed.load()) {
        // std::cout << "[SPAWN] skip seeder=" << s.port << " (cancel/failed)\n";
        return false;
    }

    // bump counters BEFORE starting thread
    job->active_workers.fetch_add(1);
    job->usable_seeder_workers.fetch_add(1);
    job->worker_running.store(true);

    pthread_t tid{};
    auto *wa = new WorkerArg{this, job, s};

    if (pthread_create(&tid, nullptr, &download_manager::worker_entry, wa) != 0) {
        delete wa;

        // rollback counters on failure
        job->active_workers.fetch_sub(1);
        job->usable_seeder_workers.fetch_sub(1);

        // std::cout << "[SPAWN] pthread_create FAILED for seeder=" << s.port << "\n";
        return false;
    }

    // Store the thread id to can join later
    pthread_mutex_lock(&job->m); // use a dedicated workers mutex
    job->workers.push_back(tid);
    pthread_mutex_unlock(&job->m);

    // std::cout << "[SPAWN] worker started for seeder=" << s.port
    //           << " active_workers=" << job->active_workers.load()
    //           << " usable_seeder_workers=" << job->usable_seeder_workers.load()
    //           << "\n";

    // set state back to Downloading when new worker appears
    if (job->state.load() == DownloadStatus::Stalled) {
        job->state.store(DownloadStatus::Downloading);
        // std::cout << "[SPAWN] job state Stalled -> Downloading (resume)\n";
    }

    return true;
}
void download_manager::mark_seeder_dead(const std::shared_ptr<download> &job, uint16_t port) {
    pthread_mutex_lock(&job->seeders_m);

    // remove from seeders vector
    job->seeders.erase(
        std::remove_if(job->seeders.begin(), job->seeders.end(),
                       [&](const Seeder &s) { return s.port == port; }),
        job->seeders.end());

    // allow this port to be re-added in the future
    job->seeder_ports.erase(port);

    pthread_mutex_unlock(&job->seeders_m);

    std::cout << "[SEEDER] marked dead port=" << port << "\n";
}

// when resolver gets used stalled, not cancceled, not failed
// resolver when recovery when progress stops
void download_manager::tick() {

    // allocate copy - free - allocate - copy - free < allocate once - push job ponters - done
    std::vector<std::shared_ptr<download>> snapshot; // copy
    snapshot.reserve(64);                            // avoid repeated memory allocate

    // (old) too agressive refresh kay ginhold lang ang jobs_mtx longer and more often
    // pthread_mutex_lock(&jobs_mtx);
    // for (...) refresh_seeders(job);
    // pthread_mutex_unlock(&jobs_mtx);

    pthread_mutex_lock(&jobs_mtx);
    for (auto &kv : jobs) {
        if (kv.second)
            snapshot.push_back(kv.second);
    }
    pthread_mutex_unlock(&jobs_mtx);

    if (snapshot.empty())
        return;

    for (auto &job : snapshot) {

        if (!job)
            continue;

        // cuurrent value para decide kung morefresh ba (state cancel failed)
        auto st = job->state.load();

        // skip kung completed, failed, cancelled
        if (st == DownloadStatus::Completed || st == DownloadStatus::Failed || st == DownloadStatus::Cancelled) {
            continue;
        }

        if (job->cancel_requested.load()) {
            continue;
        }
        if (job->failed.load()) {
            continue;
        }
        // refresh - stalled -> immediate recovery attempts -downloading -> periodic discovery land sa new seeders
        if (st != DownloadStatus::Stalled && st != DownloadStatus::Downloading) {
            continue;
        }

        // Throttle resolver calls while Downloading to avoid spamming the resolver.
        // For Stalled, refresh immediately (no throttle).
        constexpr time_t RESOLVE_INTERVAL_SEC = 2; // refresh mga kaisa lang sa 2 seconds
        const time_t now = time(nullptr);

        const time_t last = job->last_resolve.load();
        const bool time_to_refresh = (st == DownloadStatus::Stalled) ||
                                     ((now - last) >= RESOLVE_INTERVAL_SEC);

        if (!time_to_refresh) {
            continue;
        }

        // rick -> refresh -> tick -> referesh
        //  Update last_resolve before call sa refresh_seeders() to avoid burst refresh
        //  if tick() is called frequently.
        //
        // refresh_seeders() itself has job->refreshing so even if
        //  multiple threads call tick(), 1 refresh lang at a time per job.
        job->last_resolve.store(now);

        // std::cout << "[TICK] refresh seeders for key="
        //           << job->file_key
        //           << " state=" << state_to_str(st)
        //           << " last_resolve=" << last
        //           << " now=" << now
        //           << "\n";

        refresh_seeders(job);
    }
}

void download_manager::refresh_seeders(const std::shared_ptr<download> &job) {
    if (!resolver) {
        // std::cout << "[REFRESH] No resolver set\n";
        return;
    }

    // call resolver to get live seeders
    // deduplicate seeders by port using job->seeder_[prts
    // spawn new workers for newly added seeders]

    bool expected = false; // 1 refresh at a time per job
    if (!job->refreshing.compare_exchange_strong(expected, true)) {
        // std::cout << "[REFRESH] Already refreshing, skip\n";
        return;
    }

    struct RefreshGuard {
        std::shared_ptr<download> job;
        ~RefreshGuard() { job->refreshing.store(false); }
    } guard{job};

    // std::cout << "[REFRESH] Resolving LIVE seeders for key=" << job->file_key << "\n";

    auto live = resolver->resolve(job->file_key);

    if (live.empty()) {
        // std::cout << "[REFRESH] No live seeders found (still paused or will keep trying)\n";
        return;
    }

    std::vector<Seeder> newly_added;

    pthread_mutex_lock(&job->seeders_m);
    for (const auto &s : live) {
        if (job->seeder_ports.insert(s.port).second) {
            job->seeders.push_back(s);
            newly_added.push_back(s);
        }
    }

    pthread_mutex_unlock(&job->seeders_m);

    // std::cout << "[REFRESH] Newly added seeders: ";
    if (newly_added.empty()) {
        return;
    }

    for (const auto &s : newly_added) {
        if (job->cancel_requested.load()) {
            // std::cout << "[REFRESH] Cancel requested, stop spawning\n";
            break;
        }
        if (job->failed.load())
            break;

        // std::cout << "[REFRESH] Spawning worker for seeder " << s.port << "\n";
        spawn_worker_for(job, s);
    }
}

download_manager::download_manager(uint16_t port) : my_port(port) {
    pthread_mutex_init(&jobs_mtx, nullptr);

    const string base = "./bin/" + to_string(my_port);
    string downloads = base + "/downloads";
    ensure_dir(base);
    ensure_dir(downloads);
}

download_manager::~download_manager() {
    // Snapshot the jobs map so we don't hold jobs_mtx while joining (avoid deadlocks)
    std::unordered_map<std::string, std::shared_ptr<download>> snapshot;

    pthread_mutex_lock(&jobs_mtx);
    snapshot = jobs; // OK: same type; copies shared_ptrs
    pthread_mutex_unlock(&jobs_mtx);

    // Request cancel for all jobs (outside lock)
    for (auto &[id, job] : snapshot) {
        if (job) {
            job->cancel_requested.store(true);
        }
    }

    // Join all worker threads for all jobs (outside lock)
    for (auto &[id, job] : snapshot) {
        if (!job)
            continue;

        for (pthread_t &t : job->workers) {
            pthread_join(t, nullptr);
        }
        job->workers.clear();
        job->worker_running.store(false);
    }

    // Clear original jobs map
    pthread_mutex_lock(&jobs_mtx);
    jobs.clear();
    pthread_mutex_unlock(&jobs_mtx);

    // Destroy manager mutex
    pthread_mutex_destroy(&jobs_mtx);
}
void *download_manager::worker_entry(void *arg) {
    std::unique_ptr<WorkerArg> wa((WorkerArg *)arg); // auto-free
    wa->self->worker_run(wa->job, wa->seeder);
    return nullptr;
}

bool download_manager::add_download(const network_file &nf, const vector<Seeder> &seeders) {

    if (seeders.empty()) {
        // cout << "[ADD] No seeders provided. Returning false.\n";
        return false;
    }

    const File &info = nf.get_info();
    string key = nf.key();

    // cout << "\n[ADD] Request add_download\n"
    //      << "      key=" << key << "\n"
    //      << "      file=" << info.name << " id=" << info.id << " size=" << info.size << "\n"
    //      << "      seeders_in=" << seeders.size() << "\n";

    // Build output path using my_port (member)
    string out_dir = "./bin/" + to_string(my_port) + "/downloads";
    ensure_dir("./bin/" + to_string(my_port));
    ensure_dir(out_dir);

    string final_path = out_dir + "/" + info.name;
    string temp_path = final_path + ".part";

    if (file_exists(final_path)) {
        auto sz = file_size(final_path);
        if (sz == info.size) {
            std::cout << "File: [" << info.id << "]" << info.name << " already exists." << "\n";
            return false;
        } else {
            std::cout << "[ADD] Existing file size mismatch (got=" << sz
                      << " expected=" << info.size << "). Treating as partial/broken.\n";
            // rename and restart
            std::rename(final_path.c_str(), (final_path + ".broken").c_str());
        }
    }

    // If partial file exists, restart (or resume if you implement it)
    //     if (file_exists(temp_path)) {
    //         std::cout << "[ADD] Partial file exists: " << temp_path
    //                   << " (restarting -> renaming to broken)\n";
    //         if (!rename_to_broken(temp_path)) return false;
    //     }

    // Create a new job
    auto job = std::make_shared<download>();

    job->file_key = key;
    job->file_id = info.id;
    job->file_name = info.name;
    job->total_bytes = info.size;
    job->downloaded_bytes.store(0);
    job->state.store(DownloadStatus::Downloading);
    job->seeders = seeders; // store all seeders

    // Reserve/insert into jobs atomically (avoid duplicates)
    pthread_mutex_lock(&jobs_mtx);
    auto existing = find_job_by_key_locked(key);
    if (existing && existing->state.load() == DownloadStatus::Downloading) {
        pthread_mutex_unlock(&jobs_mtx);
        cout << "File: [" << info.id << "] " << info.name << " already started\n";

        // cout << "[ADD] Job already exists and is Downloading. Skipping.\n";
        // cout << "[ADD] Already started.\n";

        return false; // skipped: already downloading
    }
    jobs[key] = job;
    pthread_mutex_unlock(&jobs_mtx);

    job->last_resolve.store(time(nullptr)); // now less spam, avoid immediate resolve call after add

    // cout << "[ADD] Job created. total_bytes=" << job->total_bytes
    //      << " total_seeders=" << job->seeders.size() << "\n";

    pthread_mutex_lock(&job->seeders_m);
    job->seeder_ports.clear();
    for (const auto &s : seeders)
        job->seeder_ports.insert(s.port);
    pthread_mutex_unlock(&job->seeders_m);

    // cout << "[ADD] Seeder ports registered (dedupe set size="
    //      << job->seeder_ports.size() << ")\n";

    // Init shared scheduler + stated
    job->next_offset.store(0);
    job->failed.store(false);
    job->cancel_requested.store(false);

    constexpr uint32_t CHUNK_SIZE = 32;

    job->chunk_size = CHUNK_SIZE;
    job->total_chunks = (job->total_bytes + CHUNK_SIZE - 1) / CHUNK_SIZE;

    job->chunk_state.clear();
    job->chunk_state.resize(job->total_chunks);
    for (auto &cs : job->chunk_state)
        cs.store(0);

    job->chunks_done.store(0);
    job->usable_seeder_workers.store(0);
    job->active_workers.store(0);

    pthread_mutex_lock(&job->m);
    job->output_path = temp_path; // workers write to .part
    job->final_path = final_path; // rename target on completion
    job->error.clear();
    pthread_mutex_unlock(&job->m);

    // cout << "[ADD] Scheduler init: chunk_size=" << job->chunk_size
    //      << " total_chunks=" << job->total_chunks
    //      << " usable_seeder_workers=" << job->usable_seeder_workers.load() << "\n";

    // cout << "[ADD] Job inserted into jobs map. key=" << key << "\n";

    // Create/truncate file now (fail early) = pre-sizw filw
    {

        ofstream out(temp_path, ios::binary | ios::trunc);
        if (!out) {
            job->state.store(DownloadStatus::Failed);
            pthread_mutex_lock(&job->m);
            job->error = "Failed to create: " + temp_path;
            pthread_mutex_unlock(&job->m);

            // remove reservation
            pthread_mutex_lock(&jobs_mtx);
            jobs.erase(key);
            pthread_mutex_unlock(&jobs_mtx);

            // cout << "[ADD] Removed job from jobs map due to failure.\n";
            return false;
        }

        // Pre-size so random writes are safe and file has correct final size
        if (job->total_bytes > 0) {
            out.seekp((std::streamoff)(job->total_bytes - 1));
            char zero = 0;
            out.write(&zero, 1);
        }
    }

    // cout << "[ADD] Output file created + pre-sized to " << job->total_bytes << " bytes.\n";

    // Spawn worers for this job
    job->worker_running.store(true);
    job->workers.clear();
    job->workers.reserve(job->seeders.size()); // intial expected workers

    // cout << "[ADD] Spawning " << job->seeders.size() << " worker(s)...\n";

    int started = 0;
    for (const auto &s : job->seeders) {

        if (spawn_worker_for(job, s)) {
            started++;
        }
    }

    // If none started, treat as failure
    if (started == 0) {
        job->cancel_requested.store(true);
        job->failed.store(true);
        job->state.store(DownloadStatus::Failed);

        pthread_mutex_lock(&job->m);
        job->error = "No workers could be started";
        pthread_mutex_unlock(&job->m);

        pthread_mutex_lock(&jobs_mtx);
        jobs.erase(key);
        pthread_mutex_unlock(&jobs_mtx);

        return false;
    }

    // cout << "[ADD] add_download SUCCESS. Job running.\n";
    return true;
}

static int64_t claim_chunk_static(const std::shared_ptr<download> &job) {
    for (uint64_t i = 0; i < job->total_chunks; ++i) {
        uint8_t expected = 0;
        if (job->chunk_state[i].compare_exchange_strong(
                expected, 1, std::memory_order_acq_rel)) {
            return (int64_t)i;
        }
    }
    return -1;
}

void download_manager::worker_run(std::shared_ptr<download> job, const Seeder &seeder) {

    // Guard: ALWAYS decrement active_workers and finalize if last worker exits.

    struct WorkerExitGuard {
        std::shared_ptr<download> job;

        // If true -> guard will decrement usable_seeder_workers.
        // If false -> we already decremented usable_seeder_workers elsewhere (failure path),
        //            so guard must NOT decrement it again.
        bool counted_usable = true;

        ~WorkerExitGuard() {
            // Decrement usable counter only if we haven't already done so in a failure path.
            if (counted_usable) {
                job->usable_seeder_workers.fetch_sub(1, std::memory_order_acq_rel);
            }

            // Always decrement active worker count.
            const int left = job->active_workers.fetch_sub(1, std::memory_order_acq_rel) - 1;
            // cout << "[WORKER EXIT] one worker ended. active_workers now=" << left
            //      << " usable_seeder_workers=" << job->usable_seeder_workers.load()
            //      << " chunks_done=" << job->chunks_done.load() << "/" << job->total_chunks
            //      << " failed=" << job->failed.load()
            //      << " cancel=" << job->cancel_requested.load()
            //      << "\n";
            // Only the last exiting worker finalizes the job.
            if (left != 0)
                return;

            const bool cancelled = job->cancel_requested.load(std::memory_order_acquire);
            const bool failed = job->failed.load(std::memory_order_acquire);
            const auto done = job->chunks_done.load(std::memory_order_acquire);

            if (cancelled) {
                job->state.store(DownloadStatus::Cancelled, std::memory_order_release);
                job->worker_running.store(false, std::memory_order_release);
                return;
            }

            if (failed) {
                job->state.store(DownloadStatus::Failed, std::memory_order_release);
                job->worker_running.store(false, std::memory_order_release);
                return;
            }

            // Complete only if all chunks were actually done.
            if (done == job->total_chunks) {
                std::string tmp, fin;

                pthread_mutex_lock(&job->m);
                tmp = job->output_path; // .part
                fin = job->final_path;  // final name
                pthread_mutex_unlock(&job->m);

                // Rename FIRST, then mark Completed only on success.
                if (std::rename(tmp.c_str(), fin.c_str()) == 0) {
                    pthread_mutex_lock(&job->m);
                    job->output_path = fin; // optional: reflect final path
                    pthread_mutex_unlock(&job->m);

                    job->state.store(DownloadStatus::Completed, std::memory_order_release);
                } else {
                    job->failed.store(true, std::memory_order_release);
                    job->state.store(DownloadStatus::Failed, std::memory_order_release);

                    pthread_mutex_lock(&job->m);
                    job->error = "Failed to rename " + tmp + " -> " + fin +
                                 " errno=" + std::to_string(errno) +
                                 " (" + std::string(std::strerror(errno)) + ")";
                    pthread_mutex_unlock(&job->m);
                }
            } else {
                // Not cancelled, not failed, not complete -> stalled
                job->state.store(DownloadStatus::Stalled, std::memory_order_release);
            }

            job->worker_running.store(false, std::memory_order_release);
        }
    } guard{job};

    // Copy output path locally (avoid holding mutex during I/O)
    string path;
    pthread_mutex_lock(&job->m);
    path = job->output_path;
    pthread_mutex_unlock(&job->m);

    fstream fout(path, ios::binary | ios::in | ios::out);
    if (!fout) {

        // local unrecoverable error: fail job
        job->failed.store(true);
        job->state.store(DownloadStatus::Failed);

        pthread_mutex_lock(&job->m);
        if (job->error.empty())
            job->error = "Failed to open output file: " + path;
        pthread_mutex_unlock(&job->m);
        return;
    }

    Downloader downloader;
    uint64_t total = job->total_bytes;

    try {                      // one accept() and 1 server thread per download, keep one connection open, connect once then fetch all chunks
        Client c(seeder.port); // one connection per seeder worker

        while (true) {
            if (job->cancel_requested.load(std::memory_order_relaxed))
                return;
            if (job->failed.load(std::memory_order_relaxed))
                return; // true fatal only

            int64_t idx = claim_chunk_static(job);
            if (idx < 0)
                break; // nothing left

            uint64_t offset = (uint64_t)idx * job->chunk_size; // declare offset

            // Should not happen if total_chunks computed correctly; treat as fatal
            if (offset >= total && total != 0) {
                job->failed.store(true);
                job->state.store(DownloadStatus::Failed);

                pthread_mutex_lock(&job->m);
                job->error = "scheduler error: offset out of range (" + std::to_string(offset) +
                             " >= " + std::to_string(total) + ")";
                pthread_mutex_unlock(&job->m);

                return;
            }

            uint32_t want = (uint32_t)std::min<uint64_t>(job->chunk_size, total - offset);

            auto res = downloader.fetch_chunk_on_connection(c, job->file_key, offset, want);
            if (!res.ok) {
                // Return chunk to TODO so another seeder can retry it.
                job->chunk_state[(uint64_t)idx].store(0);

                guard.counted_usable = false;
                // mark this worker as dead
                job->usable_seeder_workers.fetch_sub(1);

                // remove dead seedr to add again later
                mark_seeder_dead(job, seeder.port);

                // cout << "[WORKER " << seeder.port << "] usable_seeder_workers(after dec)="
                //      << job->usable_seeder_workers.load() << "\n";

                // cout << "[WORKER " << seeder.port << "] calling refresh_seeders()\n";

                refresh_seeders(job);

                return; // stop only this worker
            }

            // Protocol sanity: expect exactly want bytes
            if (res.data.size() != want) {
                job->failed.store(true);
                job->state.store(DownloadStatus::Failed);

                pthread_mutex_lock(&job->m);
                job->error = "protocol violation: expected " + std::to_string(want) +
                             " bytes, got " + std::to_string(res.data.size());
                pthread_mutex_unlock(&job->m);

                // return chunk to todo is irrelevant now; job is fatal
                return;
            }

            // synchronized write
            pthread_mutex_lock(&job->io_mtx);
            fout.seekp((std::streamoff)offset);
            fout.write(reinterpret_cast<const char *>(res.data.data()),
                       (std::streamsize)res.data.size());
            bool write_ok = !!fout;
            pthread_mutex_unlock(&job->io_mtx);

            if (!write_ok) {
                job->failed.store(true);
                job->state.store(DownloadStatus::Failed);

                pthread_mutex_lock(&job->m);
                job->error = "file write failed at offset " + std::to_string(offset);
                pthread_mutex_unlock(&job->m);
                return;
            }

            job->downloaded_bytes.fetch_add(res.data.size());

            job->chunk_state[(uint64_t)idx].store(2);
            job->chunks_done.fetch_add(1);
            // cout << "[WORKER " << seeder.port << "] CHUNK OK idx=" << idx
            //      << " bytes=" << res.data.size()
            //      << " progress=" << job->chunks_done.load() << "/" << job->total_chunks
            //      << "\n";
        }

    } catch (const std::exception &e) {
        // Treat exceptions as seeder/connection failure
        guard.counted_usable = false;
        job->usable_seeder_workers.fetch_sub(1);
        mark_seeder_dead(job, seeder.port);

        refresh_seeders(job);

        pthread_mutex_lock(&job->m);
        if (job->error.empty())
            job->error = std::string("Seeder ") + std::to_string(seeder.port) + " exception: " + e.what();
        pthread_mutex_unlock(&job->m);

        return; // stop only this worker
    }
}

static string human_bytes(uint64_t bytes) {

    char buf[64];

    if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%lluB", (unsigned long long)bytes);
        return buf;
    }

    double kb = (double)bytes / 1024.0;
    if (kb < 1024.0) {
        snprintf(buf, sizeof(buf), "%.0fkB", kb);
        return buf;
    }

    double mb = kb / 1024.0;
    snprintf(buf, sizeof(buf), "%.1fMB", mb);
    return buf;
}

std::string download_manager::get_status_string() const {
    // Snapshot jobs first (don’t hold jobs_mtx while formatting)
    std::unordered_map<std::string, std::shared_ptr<download>> snapshot;

    pthread_mutex_lock(&jobs_mtx);
    snapshot = jobs; // copies keys + shared_ptrs
    pthread_mutex_unlock(&jobs_mtx);

    if (snapshot.empty()) {
        return "No active downloads";
    }

    std::string out;
    bool any = false;

    for (const auto &[job_id, job] : snapshot) {
        if (!job)
            continue;

        // --- read atomics without locking ---
        DownloadStatus st = job->state.load(std::memory_order_acquire);
        uint64_t got = job->downloaded_bytes.load(std::memory_order_relaxed);

        // If total_bytes is mutable, make it atomic or read it under job->m.
        uint64_t total = job->total_bytes;

        // --- copy non-atomic fields under job->m ---
        std::string file_name, err, path;
        int file_id = 0;
        std::vector<Seeder> seeders_copy;

        pthread_mutex_lock(&job->m);
        file_id = job->file_id;
        file_name = job->file_name;
        err = job->error;
        path = job->output_path;
        pthread_mutex_unlock(&job->m);

        pthread_mutex_lock(&job->seeders_m);
        seeders_copy = job->seeders; // avoids concurrent vector access
        pthread_mutex_unlock(&job->seeders_m);

        any = true;

        // Compute percent safely
        int pct = 0;
        if (total > 0) {
            pct = static_cast<int>((100.0 * static_cast<double>(got)) / static_cast<double>(total));
            if (pct > 100)
                pct = 100;
            if (pct < 0)
                pct = 0;
        }

        // Build line
        std::string line;
        line += "[" + std::to_string(file_id) + "] ";
        line += file_name.empty() ? "(unnamed)" : file_name;
        line += " ";
        line += human_bytes(got);
        line += "/";
        line += human_bytes(total);
        line += " (" + std::to_string(pct) + "%) ";

        line += "[seeder: ";
        for (size_t i = 0; i < seeders_copy.size(); ++i) {
            line += std::to_string(seeders_copy[i].port);
            if (i + 1 < seeders_copy.size())
                line += ",";
        }
        line += "] ";

        line += state_to_str(st);

        if (st == DownloadStatus::Failed && !err.empty()) {
            line += " ERROR: " + err;
        }
        if (st == DownloadStatus::Completed) {
            line += " DONE";
        }

        if (!out.empty())
            out += "\n";
        out += line;
    }

    return any ? out : "No active downloads";
}

void download_manager::cancel(const std::string &file_key) {
    pthread_mutex_lock(&jobs_mtx);
    auto job = find_job_by_key_locked(file_key);
    pthread_mutex_unlock(&jobs_mtx);

    if (!job)
        return;
    job->cancel_requested.store(true);
}

bool download_manager::has_active_downloads() const {
    pthread_mutex_lock(&jobs_mtx);
    for (auto &kv : jobs) {
        auto &job = kv.second;
        if (!job)
            continue;
        auto st = job->state.load(std::memory_order_relaxed);
        if (st == DownloadStatus::Downloading || st == DownloadStatus::Stalled) {
            pthread_mutex_unlock(&jobs_mtx);
            return true;
        }
    }
    pthread_mutex_unlock(&jobs_mtx);
    return false;
}

bool download_manager::has_downloading() const {
    pthread_mutex_lock(&jobs_mtx);
    for (const auto &kv : jobs) {
        const auto &job = kv.second;
        if (!job)
            continue;
        auto st = job->state.load(std::memory_order_relaxed);
        if (st == DownloadStatus::Downloading) {
            pthread_mutex_unlock(&jobs_mtx);
            return true;
        }
    }
    pthread_mutex_unlock(&jobs_mtx);
    return false;
}

bool download_manager::all_downloads_completed() const {
    pthread_mutex_lock(&jobs_mtx);

    if (jobs.empty()) {
        pthread_mutex_unlock(&jobs_mtx);
        return true; // nothing to wait for
    }

    for (const auto &kv : jobs) {
        const auto &job = kv.second;
        if (!job)
            continue;

        auto st = job->state.load();
        if (st != DownloadStatus::Completed) {
            pthread_mutex_unlock(&jobs_mtx);
            return false;
        }
    }

    pthread_mutex_unlock(&jobs_mtx);
    return true;
}
