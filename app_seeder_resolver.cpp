#include "../inc/app_seeder_resolver.h"
#include "../inc/seed_app.h"
#include "../inc/seeder_probe.h"

AppSeederResolver::AppSeederResolver(seed_app &app, std::vector<int> peers, uint16_t my_port)
    : app_(app), peers_(std::move(peers)), my_port_(my_port) {}

std::vector<Seeder> AppSeederResolver::resolve(const std::string &file_key) {
    const auto files = app_.list_unique_files(peers_, my_port_); // discover candidate
    const network_file *nf = app_.find_by_key(files, file_key);  // pick file matches the key,
    if (!nf)
        return {}; // if no found returm empty, consistent with interface

    SeederProbe sp;
    ProbeOptions opt;
    opt.verify_file = true; // validates if they jave actual expected file

    auto pr = sp.probe(*nf, opt);
    return pr.reachable; // what the interface promised the reachable seeders
}

// to discover/list file across peers
// test which seeders rae reachable
// adapter that turn a file_key into list of reachable Seeders