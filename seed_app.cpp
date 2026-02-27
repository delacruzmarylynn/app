#include <assert.h>
#include <iostream>
#include <list>
#include <string>
#include <sys/select.h>
#include <termios.h>
#include <vector>

#include "../inc/app_seeder_resolver.h"
#include "../inc/seed_app.h"

using namespace std;

seed_app::seed_app() = default;
seed_app::~seed_app() = default;

void seed_app::start() {
    is_running = true;

    printf("Finding available ports... ");

    chosen_port = 0;
    fd = -1;

    for (uint16_t port = 9000; port < 9005; ++port) {
        fd = pm.reserve_port(port);
        if (fd != -1) {
            chosen_port = port;
            break;
        }
    }
    if (fd != -1) {
        cout << "Found port " << chosen_port << "\n";
        dm.emplace(chosen_port); // initializes download_manager now that port is known
    } else {
        cout << "No available ports found from 9000-9005.\n";
    }
}

int seed_app::get_fd() const {
    return fd;
}

int seed_app::get_port() const {
    return chosen_port;
}
// if (!host.start_server(chosen_port)) {
//     cout << "Host failed. Release port.\n";
//     pm.release_port(chosen_port);
//     return;
// } else
//     display_menu();

// host.accept_clients();

void seed_app::display_menu() {
    cout << "\n\nSeed App\n"
         << "[1] Download file.\n"
         << "[2] Download status\n"
         << "[3] Exit.\n\n";
}

void *seed_app::server_entry(void *arg) {
    Host *host = static_cast<Host *>(arg); // no copy
    host->accept_clients();
    return NULL;
}

int read_int(const std::string &prompt) {
    while (true) {
        std::cout << prompt;

        std::string line;
        if (!std::getline(std::cin, line)) {
            return -1; //
        }

        try {
            size_t pos = 0;
            int value = std::stoi(line, &pos);

            while (pos < line.size() && isspace(static_cast<unsigned char>(line[pos])))
                pos++;
            if (pos != line.size())
                throw std::invalid_argument("junk");

            return value;
        } catch (...) {
            std::cout << "Invalid input. Please enter a number.\n";
        }
    }
}

vector<network_file> seed_app::list_unique_files(const vector<int> &peers, int port) {
    // remove duplication by file key which is the filename and size.
    // so reply should be parse line by line format: filename size
    // remove duplication key and inser the new

    // map since filename and size

    // key -> network_file (File info + seeders)
    unordered_map<string, network_file> unique;

    for (int p : peers) {
        if (p == port)
            continue;

        try {
            Client client(p);
            client.send_choice(1);
            string reply = client.receive_response();
            client.close_connection();

            istringstream iss(reply);
            string line;

            while (getline(iss, line)) {
                if (line.empty())
                    continue;
                if (line.rfind("ERR\t", 0) == 0)
                    continue;
                if (line == "EMPTY\t0")
                    continue;

                // Parse: nameTABsize

                auto tab = line.find('\t');
                if (tab == string::npos)
                    continue;

                string name = line.substr(0, tab);
                uint64_t size = 0;

                try {
                    size = static_cast<uint64_t>(stoull(line.substr(tab + 1)));
                } catch (...) {
                    continue;
                }

                File info;
                info.id = 0;
                info.name = move(name);
                info.size = size;

                string key = info.key();
                auto it = unique.find(key);
                if (it == unique.end()) {
                    it = unique.emplace(key, network_file(info)).first;
                }
                // this now is the current peer as a seeder
                it->second.add_seeder(Seeder{static_cast<uint16_t>(p)});
            }

        } catch (const exception &e) {
            // cerr << "Peer " << p << " failed: " << e.what() << "\n";
        }
    }

    // but map to vector for printing, index by idno ne
    // sort by name, then size
    // display

    // Move into vector
    vector<network_file> files;
    files.reserve(unique.size());
    for (auto &kv : unique) {
        if (kv.second.has_seeders()) {
            files.push_back(move(kv.second));
        }
    }

    //  Sort then assign IDs
    sort(files.begin(), files.end(),
         [](const network_file &a, const network_file &b) {
             const auto &ai = a.get_info();
             const auto &bi = b.get_info();
             if (ai.name != bi.name)
                 return ai.name < bi.name;
             return ai.size < bi.size;
         });

    int id = 1;
    for (auto &nf : files) {
        nf.set_id(id++);
    }

    return files;
}
void seed_app::tick_downloads() {
    if (dm) {
        dm->tick();
    }
}

bool seed_app::start_download(const network_file &file, const vector<Seeder> &seeders) {
    if (!dm) {
        std::cout << "Download manager not initialized (no reserved port).\n";
        return false;
    }
    return dm->add_download(file, seeders);
}

std::string seed_app::download_status() const {
    if (!dm)
        return "Download manager not initialized.\n";
    return dm->get_status_string();
}

const network_file *seed_app::find_by_key(const std::vector<network_file> &files, const string &key) {
    for (const auto &nf : files) {
        if (nf.key() == key)
            return &nf;
    }
    return nullptr;
}

bool seed_app::downloads_completed() const {
    if (!dm)
        return true;
    return dm->all_downloads_completed();
}

bool seed_app::has_downloads() const {
    if (!dm)
        return false;
    return dm->has_active_downloads();
}

void seed_app::exit() {
    if (fd != -1) {
        close(fd);
        pm.release_port(chosen_port);
        fd = -1;
        chosen_port = 0;
        is_running = false;

        dm.reset();

        // std::cout << "Stopped and released port.\n";
    }
}

void seed_app::set_seeder_resolver(std::shared_ptr<SeederResolver> r) {
    if (dm)
        dm->set_seeder_resolver(std::move(r));
}

struct TickArg {
    seed_app *app;
    std::atomic<bool> *running;
};

void *tick_entry(void *arg) {
    auto *ta = static_cast<TickArg *>(arg);
    while (ta->running->load()) {
        ta->app->tick_downloads();
        sleep(2); // retry every 2 seconds
    }
    return nullptr;
}

static void clear_screen() {
    std::cout << "\033[2J\033[H" << std::flush; // ANSI clear + home
}

struct TermiosGuard {
    termios oldt{};
    bool ok{false};

    TermiosGuard() {
        if (tcgetattr(STDIN_FILENO, &oldt) == 0) {
            termios newt = oldt;
            newt.c_lflag &= ~(ICANON | ECHO); // immediate input, no echo
            tcsetattr(STDIN_FILENO, TCSANOW, &newt);
            ok = true;
        }
    }
    ~TermiosGuard() {
        if (ok)
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
};

// returns true if a key was pressed
static bool key_pressed() {
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);

    // select returns >0 if input is available
    return select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv) > 0;
}

// consume one character (so it doesn’t affect next menu input)
static void consume_key() {
    char c;
    (void)read(STDIN_FILENO, &c, 1);
}

int main() {

    seed_app app;

    app.start();

    int fd = app.get_fd();
    uint16_t port = app.get_port();
    if (fd != -1) {
        Host host(fd, port);
        // why stuckzz
        //  spawn thread coz server run bg so the main thread run something else which acts as the client and showing the menu
        pthread_t server_thread;
        pthread_create(&server_thread, NULL, seed_app::server_entry, &host);

        atomic<bool> tick_running{true};
        TickArg ta{&app, &tick_running};
        pthread_t tick_thread{};
        pthread_create(&tick_thread, nullptr, tick_entry, &ta);

        sleep(5);

        // display menu herez
        bool is_running = true;
        vector<network_file> available_files;

        while (is_running) {
            app.display_menu();

            int choice = read_int("Enter choice: ");
            if (choice == -1) {
                std::cout << "\nEOF detected. Exiting...\n";
                break;
            }

            switch (choice) {
            case 1: {
                cout << "\n\nSearching for files....";

                vector<int> peers = {9000, 9001, 9002, 9003, 9004};

                auto resolver = std::make_shared<AppSeederResolver>(app, peers, port);
                app.set_seeder_resolver(resolver);

                available_files = app.list_unique_files(peers, port);

                cout << "done\n";

                if (available_files.empty()) {
                    cout << "No files found\n";
                    break;
                } else {
                    cout << "Files available\n";

                    for (const auto &nf : available_files) {
                        const File &info = nf.get_info();
                        cout << "[" << info.id << "]" << info.name << "\n";
                    }
                }

                int file_id = read_int("\nEnter file ID: ");

                if (file_id == -1) {
                    break;
                }

                if (file_id < 1 || file_id > (int)available_files.size()) {
                    cout << "Invalid file ID. Enter a number between 1 and "
                         << available_files.size() << ".\n";
                    break;
                }

                const network_file &snapshot = available_files[file_id - 1];
                const File &selected = snapshot.get_info();

                // cout << "Selected: " << selected.name << ": " << selected.size << " bytes" << "\n";
                cout << "Locating seeders..";

                vector<Seeder> live_seeders;

                live_seeders = resolver->resolve(snapshot.key());

                if (live_seeders.empty()) {
                    cout << "\nNo seeders for file ID: " << selected.id << "\n";
                    break;
                }

                cout << "Found " << live_seeders.size() << " seeders.\n";

                // user gets back to the menu

                if (!app.start_download(snapshot, live_seeders)) {
                    // cout << "Failed to start download\n";
                    break;
                } else {
                    cout << "Download started. File:  [" << selected.id << "] " << selected.name << "\n";
                }

                break;
            }

            case 2: {
                TermiosGuard tg; // enable "press any key" behavior while in this screen

                while (true) {
                    clear_screen();
                    cout << "Download status: \n";

                    if (!app.has_downloads()) {
                        cout << "No active downloads\n\n";
                    } else {
                        cout << app.download_status() << "\n";
                    }
                    cout << "\n\nPress any key to return to menu.\n\n";
                    cout << "\n\n"
                         << flush;

                    // Exit immediately if user pressed any key
                    if (key_pressed()) {
                        consume_key();
                        break; // return to menu
                    }

                    sleep(1);
                }

                // After breaking out, execution continues and your menu loop prints the menu again
                break;
            }

            case 3:
                cout << "Closing server port...closed\n";
                is_running = false;
                break;

            default:
                cout << "Invalid input\n";
                break;
            }
        }
        host.stop_server();
        pthread_join(server_thread, NULL);

        // sleep(10;)
        app.exit();
        tick_running.store(false);
        pthread_join(tick_thread, nullptr);
        return 0;
    }
}