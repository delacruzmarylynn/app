#ifndef SEEDER_H
#define SEEDER_H

#include <assert.h>
#include <iostream>
#include <list>
#include <string>
#include <vector>

class Seeder {
  public:
    uint16_t port{};

    Seeder() = default;
    // receves choice 2 and sends back bytes

    explicit Seeder(uint16_t p) : port(p) {};
};

#endif
