#ifndef FILE_H
#define FILE_H

#include <assert.h>
#include <cstdint>
#include <iostream>
#include <list>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "../inc/seeder.h"

using namespace std;

class File {
  public:
    int id{};
    string name;
    uint64_t size{};

    File() = default;
    File(int id_, string name_, uint64_t size_);
    string key() const;
};

#endif
