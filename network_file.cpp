#include "../inc/network_file.h"

const File &network_file::get_info() const {
    return info;
}

const vector<Seeder> &network_file::get_seeders() const {
    return seeders;
}
bool network_file::has_seeders() const {
    return !seeders.empty();
}

string network_file::key() const {
    return info.key();
}

void network_file::set_id(int id_) {
    info.id = id_;
}

void network_file::add_seeder(Seeder s) {
    for (const auto &existing : seeders) {
        if (existing.port == s.port)
            return;
    }
    seeders.push_back(s);
}