#ifndef PDEF_EXECUTOR_H
#define PDEF_EXECUTOR_H

#include "../pdef/pdef_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Bytecode Executor ==========
 * High-performance bytecode execution engine for packet filtering.
 * This is a performance-critical component that executes compiled
 * filter rules against raw packet data.
 */

/**
 * Execute a filter rule's bytecode against a packet.
 *
 * @param packet        Pointer to packet data
 * @param packet_len    Length of packet in bytes
 * @param rule          Filter rule to execute
 * @return              true if packet matches the filter, false otherwise
 *
 * Performance: This function is designed to execute in < 100ns for typical rules.
 * - Uses zero-copy access to packet data
 * - Inline endian conversion functions
 * - Branch prediction friendly
 * - No dynamic memory allocation
 */
bool execute_filter(const uint8_t* packet, uint32_t packet_len, const FilterRule* rule);

/**
 * Execute bytecode instructions directly (low-level interface).
 *
 * @param packet        Pointer to packet data
 * @param packet_len    Length of packet in bytes
 * @param bytecode      Array of bytecode instructions
 * @param bytecode_len  Number of instructions
 * @return              true if execution returns true, false otherwise
 */
bool execute_bytecode(const uint8_t* packet, uint32_t packet_len,
                      const Instruction* bytecode, uint32_t bytecode_len);

#ifdef __cplusplus
}
#endif

#endif /* PDEF_EXECUTOR_H */
