#ifndef PDEF_PROTOCOL_H
#define PDEF_PROTOCOL_H

#include "../pdef/pdef_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Protocol Manager ==========
 * Manages loaded protocol definitions and provides the main
 * packet filtering interface.
 */

/**
 * Check if a packet matches any filter rule in the protocol.
 *
 * @param packet        Pointer to packet data
 * @param packet_len    Length of packet in bytes
 * @param port          Port number (for protocol matching)
 * @param proto         Protocol definition
 * @return              true if packet matches any rule, false otherwise
 *
 * This function:
 * 1. Checks if the port matches the protocol (if ports are defined)
 * 2. Iterates through all filter rules
 * 3. Returns true if any rule matches
 */
bool packet_filter_match(const uint8_t* packet, uint32_t packet_len,
                         uint16_t port, const ProtocolDef* proto);

/**
 * Free a protocol definition and all associated resources.
 *
 * @param proto         Protocol definition to free
 */
void protocol_free(ProtocolDef* proto);

/**
 * Find a structure definition by name.
 *
 * @param proto         Protocol definition
 * @param name          Structure name
 * @return              Pointer to StructDef, or NULL if not found
 */
const StructDef* protocol_find_struct(const ProtocolDef* proto, const char* name);

/**
 * Find a constant value by name.
 *
 * @param proto         Protocol definition
 * @param name          Constant name
 * @param value         Output: constant value
 * @return              true if found, false otherwise
 */
bool protocol_find_constant(const ProtocolDef* proto, const char* name, uint64_t* value);

/**
 * Print protocol information (for debugging).
 *
 * @param proto         Protocol definition
 */
void protocol_print(const ProtocolDef* proto);

/**
 * Print filter rule bytecode disassembly (for debugging).
 *
 * @param rule          Filter rule
 */
void filter_rule_disassemble(const FilterRule* rule);

#ifdef __cplusplus
}
#endif

#endif /* PDEF_PROTOCOL_H */
