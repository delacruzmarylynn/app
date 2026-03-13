// test_main.cpp
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

// EXPECT_TRUE cond
// - Checks cond
// - If cond is false:
//     -prints: FAIL file:line: condition
//     -exits the program with std::exit(1)
#define EXPECT_TRUE(cond)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

// Just a shorthand for EXPECT_TRUE(!(cond)).
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

// Evaluates a and b once (stores into _a and _b)
// Compares _a == _b
// If not equal, prints both values (got … vs …) and exits.
// This “assert-and-print” behavior is the same style you’ll see in real unit test frameworks
#define EXPECT_EQ(a, b)                                                                                                         \
    do {                                                                                                                        \
        auto _a = (a);                                                                                                          \
        auto _b = (b);                                                                                                          \
        if (!(_a == _b)) {                                                                                                      \
            std::printf("FAIL %s:%d: %s != %s (got %lld vs %lld)\n", __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); \
            std::exit(1);                                                                                                       \
        }                                                                                                                       \
    } while (0)

// --- YOUR APIs (declare; implement in your .cpp/.h) ---
void set_flag(uint32_t &v, uint32_t mask);
void clear_flag(uint32_t &v, uint32_t mask);
bool is_flag_set(uint32_t v, uint32_t mask);

// The “result structs” (U16Result, AddResult)
// These are simple “return both status + value” structs:

//  Example result type for parsing + safe_add (adjust to your design)
struct ReadU16Result {
    bool ok;
    uint16_t value;
};
ReadU16Result read_u16_be(const uint8_t *buf, size_t len, size_t offset);

struct AddResult {
    bool ok;
    int value;
};
AddResult safe_add(int a, int b);

// ------------------- Day A Tests -------------------
// Tests your bit-flag helpers.

// Start with v = 0
// set_flag(v, F1) should set bit(s) in F1
// is_flag_set(v, F1) must be true
// is_flag_set(v, F2) must be false
// After clear_flag(v, F1), F1 must be false
// After setting BOTH flags, is_flag_set(v, BOTH) must be true (means “all bits in BOTH are set”)
// After clearing just F2, is_flag_set(v, BOTH) must become false

// So your is_flag_set(v, mask) is expected to behave like:

// return true only if all bits in mask are present in v
static void test_bitops_set_clear_multi() {
    // Inside test_bitops_set_clear_multi():

    // Start with v = 0
    // Set F1
    // Verify F1 is set and F2 is not
    // Clear F1, verify it’s gone
    // Set BOTH flags, verify BOTH are set
    // Clear only F2, verify “BOTH” condition fails

    // So the test checks:

    // you can set one flag
    // you can clear one flag
    // you can set multiple flags at once
    // your “is set” function checks ALL bits, not “any bit”
    const uint32_t F1 = 0x01;
    const uint32_t F2 = 0x04;
    const uint32_t BOTH = F1 | F2;

    uint32_t v = 0;
    set_flag(v, F1);
    EXPECT_TRUE(is_flag_set(v, F1));
    EXPECT_FALSE(is_flag_set(v, F2));

    clear_flag(v, F1);
    EXPECT_FALSE(is_flag_set(v, F1));

    set_flag(v, BOTH);
    EXPECT_TRUE(is_flag_set(v, BOTH)); // all bits in BOTH must be set
    clear_flag(v, F2);
    EXPECT_FALSE(is_flag_set(v, BOTH)); // now missing one bit
}

// ------------------- Day B Tests -------------------
// Tests parsing a 16-bit big-endian value from a byte buffer.
// Expectations:

// offset 0 => 0x1234
// offset 2 => 0xABCD (the last valid offset for 2 bytes in a 4-byte array)
// offset 3 => invalid because you’d need bytes at 3 and 4, but 4 doesn’t exist → ok == false

// So read_u16_be must:

// // bounds-check offset + 1 < len
// // combine bytes like: (buf[offset] << 8) | buf[offset+1]
static void test_read_u16_be_valid_edge_invalid() {
    const uint8_t buf[] = {0x12, 0x34, 0xAB, 0xCD};

    auto r1 = read_u16_be(buf, sizeof(buf), 0);
    EXPECT_TRUE(r1.ok);
    EXPECT_EQ(r1.value, 0x1234);

    auto r2 = read_u16_be(buf, sizeof(buf), 2); // last valid offset for u16
    EXPECT_TRUE(r2.ok);
    EXPECT_EQ(r2.value, 0xABCD);

    auto r3 = read_u16_be(buf, sizeof(buf), 3); // invalid (needs 2 bytes)
    EXPECT_FALSE(r3.ok);
}

// // ------------------- Day C Tests -------------------
// // Test integer addition with overflow detections
// // Expectation:
// // safe_add(10, 20) => ok true, value 30
// // safe_add(INT_MAX, 1) => ok false (overflow)
// // safe_add(INT_MIN, -1) => ok false (underflow)

// // So safe_add must detect overflow/underflow before doing the addition (or do it in a wider type and check range).
// static void test_safe_add_normal_and_boundaries() {
//     auto n = safe_add(10, 20);
//     EXPECT_TRUE(n.ok);
//     EXPECT_EQ(n.value, 30);

//     auto p = safe_add(std::numeric_limits<int>::max(), 1);
//     EXPECT_FALSE(p.ok); // overflow expected

//     auto m = safe_add(std::numeric_limits<int>::min(), -1);
//     EXPECT_FALSE(m.ok); // underflow expected
// }

// // ------------------- Day D Tests -------------------
// // This is a demonstration / practice block using STL std::vector operations:

// // Insert 9 at index 1: {1, 9, 2, 3}
// // Remove value 2 using erase-remove idiom: {1, 9, 3}
// // Move 9 to end: {1, 3, 9}

// static void test_vector_ops_final_order() {
//     // You can wrap these as functions too, but here’s a direct skeleton:
//     std::vector<int> v = {1, 2, 3};

//     // insert at index 1 -> {1, 9, 2, 3}
//     v.insert(v.begin() + 1, 9);
//     EXPECT_EQ(v[0], 1);
//     EXPECT_EQ(v[1], 9);
//     EXPECT_EQ(v[2], 2);
//     EXPECT_EQ(v[3], 3);

//     // remove by value 2 -> {1, 9, 3}
//     v.erase(std::remove(v.begin(), v.end(), 2), v.end());
//     EXPECT_EQ(v.size(), 3u);
//     EXPECT_EQ(v[0], 1);
//     EXPECT_EQ(v[1], 9);
//     EXPECT_EQ(v[2], 3);

//     // move item: move '9' to end -> {1, 3, 9}
//     auto it = std::find(v.begin(), v.end(), 9);
//     int temp = *it;
//     v.erase(it);
//     v.push_back(temp);
//     EXPECT_EQ(v[0], 1);
//     EXPECT_EQ(v[1], 3);
//     EXPECT_EQ(v[2], 9);
// }

// // ------------------- State Machine Tests -------------------
// // Then tests next_state:

// // Idle + Ok → Editing
// // Editing + Ok → Confirm
// // Confirm + Ok → Done
// // Editing + Fail → Error
// // Done + Ok → stays Done (or Error depending on your intended rules; the comment says pick one)

// // So you implement next_state() to encode your transition rules (usually a switch).
// enum class State { Idle,
//                    Editing,
//                    Confirm,
//                    Done,
//                    Error };
// enum class Event { Ok,
//                    Cancel,
//                    Timeout,
//                    Fail };
// State next_state(State s, Event e);

// static void test_state_machine_basic() {
//     EXPECT_EQ((int)next_state(State::Idle, Event::Ok), (int)State::Editing);
//     EXPECT_EQ((int)next_state(State::Editing, Event::Ok), (int)State::Confirm);s
//     EXPECT_EQ((int)next_state(State::Confirm, Event::Ok), (int)State::Done);
//     EXPECT_EQ((int)next_state(State::Editing, Event::Fail), (int)State::Error);

//     // unexpected transition example
//     EXPECT_EQ((int)next_state(State::Done, Event::Ok), (int)State::Done); // or Error, depending on your rules
// }

int main() {
    test_bitops_set_clear_multi();
    test_read_u16_be_valid_edge_invalid();
    // test_safe_add_normal_and_boundaries();
    // test_vector_ops_final_order();
    // test_state_machine_basic();
    std::puts("ALL TESTS PASSED");
    return 0;
}

// To practice:
//  Fast feedback loop  (write function -> run tests -> see pass/fail)
// minimal unit-test patten similar in spirit to the EXPECT_* assertions used in full unit test environments
// test-driver/assertion approach
