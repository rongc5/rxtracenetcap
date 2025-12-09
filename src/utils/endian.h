#ifndef PDEF_ENDIAN_H
#define PDEF_ENDIAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Byte Order Conversion Functions ==========
 * These are performance-critical inline functions for reading
 * multi-byte integers from raw packet data with proper endianness.
 * The compiler will optimize these to single CPU instructions (e.g., bswap).
 */

/* Read uint16 in big-endian format */
static inline uint16_t read_u16_be(const uint8_t* data, uint32_t offset) {
    return ((uint16_t)data[offset] << 8) |
           ((uint16_t)data[offset + 1]);
}

/* Read uint16 in little-endian format */
static inline uint16_t read_u16_le(const uint8_t* data, uint32_t offset) {
    return ((uint16_t)data[offset]) |
           ((uint16_t)data[offset + 1] << 8);
}

/* Read uint32 in big-endian format */
static inline uint32_t read_u32_be(const uint8_t* data, uint32_t offset) {
    return ((uint32_t)data[offset] << 24) |
           ((uint32_t)data[offset + 1] << 16) |
           ((uint32_t)data[offset + 2] << 8) |
           ((uint32_t)data[offset + 3]);
}

/* Read uint32 in little-endian format */
static inline uint32_t read_u32_le(const uint8_t* data, uint32_t offset) {
    return ((uint32_t)data[offset]) |
           ((uint32_t)data[offset + 1] << 8) |
           ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}

/* Read uint64 in big-endian format */
static inline uint64_t read_u64_be(const uint8_t* data, uint32_t offset) {
    return ((uint64_t)data[offset] << 56) |
           ((uint64_t)data[offset + 1] << 48) |
           ((uint64_t)data[offset + 2] << 40) |
           ((uint64_t)data[offset + 3] << 32) |
           ((uint64_t)data[offset + 4] << 24) |
           ((uint64_t)data[offset + 5] << 16) |
           ((uint64_t)data[offset + 6] << 8) |
           ((uint64_t)data[offset + 7]);
}

/* Read uint64 in little-endian format */
static inline uint64_t read_u64_le(const uint8_t* data, uint32_t offset) {
    return ((uint64_t)data[offset]) |
           ((uint64_t)data[offset + 1] << 8) |
           ((uint64_t)data[offset + 2] << 16) |
           ((uint64_t)data[offset + 3] << 24) |
           ((uint64_t)data[offset + 4] << 32) |
           ((uint64_t)data[offset + 5] << 40) |
           ((uint64_t)data[offset + 6] << 48) |
           ((uint64_t)data[offset + 7] << 56);
}

/* Read int8 */
static inline int8_t read_i8(const uint8_t* data, uint32_t offset) {
    return (int8_t)data[offset];
}

/* Read int16 in big-endian format */
static inline int16_t read_i16_be(const uint8_t* data, uint32_t offset) {
    return (int16_t)read_u16_be(data, offset);
}

/* Read int16 in little-endian format */
static inline int16_t read_i16_le(const uint8_t* data, uint32_t offset) {
    return (int16_t)read_u16_le(data, offset);
}

/* Read int32 in big-endian format */
static inline int32_t read_i32_be(const uint8_t* data, uint32_t offset) {
    return (int32_t)read_u32_be(data, offset);
}

/* Read int32 in little-endian format */
static inline int32_t read_i32_le(const uint8_t* data, uint32_t offset) {
    return (int32_t)read_u32_le(data, offset);
}

/* Read int64 in big-endian format */
static inline int64_t read_i64_be(const uint8_t* data, uint32_t offset) {
    return (int64_t)read_u64_be(data, offset);
}

/* Read int64 in little-endian format */
static inline int64_t read_i64_le(const uint8_t* data, uint32_t offset) {
    return (int64_t)read_u64_le(data, offset);
}

#ifdef __cplusplus
}
#endif

#endif /* PDEF_ENDIAN_H */
