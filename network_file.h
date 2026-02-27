#ifndef __NETWORK_FILE_H__
#define __NETWORK_FILE_H__

#include "../inc/File.h"
#include "../inc/seeder.h"

using namespace std;

class network_file {
  private:
    File info;
    vector<Seeder> seeders;

  public:
    network_file() = default;
    explicit network_file(File fi) : info(std::move(fi)) {}

    const File &get_info() const;
    const vector<Seeder> &get_seeders() const;

    bool has_seeders() const;
    string key() const;

    void set_id(int id_);

    void add_seeder(Seeder s);
};

#endif