#ifndef SEEDER_RESOLVER_H
#define SEEDER_RESOLVER_H

#include <string>
#include <vector>

#include "../inc/seeder.h"

class SeederResolver {
  public:
    virtual ~SeederResolver() = default;

    // Given a file_key (name + '\t' + size), return REACHABLE seeders
    virtual std::vector<Seeder>
    resolve(const std::string &file_key) = 0;
};

#endif

// interface where app seeder rsolver can implement
// example usage:
// void downloadFile(SeederResolver& resolver, const std::string& key) {
//     auto seeders = resolver.resolve(key);  // calls the derived class version
// }

// why 0 makes it pur virtual function - the base class becomes abstract
// not r, must implement resolve() in derived class
// every resolver mmsut provide its own way to resolve seeders

// removing virtual from resolve()

// can accept seederrevolver and call resolve without knowing how its implemented
// Downloader/client logic - usees seeder resolver
// AppSeederResolver - uses seed_app + SeederProble to fulfill the contract