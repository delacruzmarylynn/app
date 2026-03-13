#include <cstddef>
#include <cstdint>

struct ReadU16Result {
    bool ok;
    uint16_t value;
};

ReadU16Result read_u16_be(const uint8_t *buf, size_t len, size_t offset) {
    // TODO: R1 - validate buf and bounds for reading 2 bytes

    // TODO: R2 - combine bytes as big-endian

    // TODO: return ok result
}