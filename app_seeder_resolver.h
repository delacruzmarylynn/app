#ifndef APP_SEEDER_RESOLVER_H
#define APP_SEEDER_RESOLVER_H

#include <cstdint>
#include <string>
#include <vector>

#include "../inc/seeder_resolver.h"

class seed_app;

class AppSeederResolver : public SeederResolver {
  public:
    AppSeederResolver(seed_app &app, std::vector<int> peers, uint16_t my_port);

    std::vector<Seeder> resolve(const std::string &file_key) override;

  private:
    seed_app &app_;
    std::vector<int> peers_;
    uint16_t my_port_;
};

#endif