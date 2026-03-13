#include <cstdint>

void set_flag(uint32_t &v, uint32_t mask) {
    v |= mask;
}

void clear_flag(uint32_t &v, uint32_t mask) {
    v &= ~mask;
}

bool is_flag_set(uint32_t v, uint32_t mask) {
    return (v & mask) == mask;
}
