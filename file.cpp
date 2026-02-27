
#include "../inc/file.h"

using namespace std;

File::File(int id_, string name_, uint64_t size_) : id(id_), name(move(name_)), size(size_) {}

string File::key() const {
    return name + "\t" + to_string(size);
}
