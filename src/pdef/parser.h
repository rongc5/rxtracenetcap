#ifndef PDEF_PARSER_H
#define PDEF_PARSER_H

#include "pdef_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== PDEF Parser ==========
 * Parses PDEF files and generates compiled ProtocolDef structures.
 * This combines lexing, parsing, semantic analysis, struct flattening,
 * and bytecode compilation into a single interface.
 */

/**
 * Parse a PDEF file and generate a compiled ProtocolDef.
 *
 * @param filename      Path to PDEF file
 * @param error_msg     Output: error message buffer (can be NULL)
 * @param error_size    Size of error message buffer
 * @return              Pointer to ProtocolDef on success, NULL on error
 *
 * The returned ProtocolDef must be freed with protocol_free().
 */
ProtocolDef* pdef_parse_file(const char* filename, char* error_msg, size_t error_size);

/**
 * Parse PDEF source code string and generate a compiled ProtocolDef.
 *
 * @param source        PDEF source code string
 * @param error_msg     Output: error message buffer (can be NULL)
 * @param error_size    Size of error message buffer
 * @return              Pointer to ProtocolDef on success, NULL on error
 *
 * The returned ProtocolDef must be freed with protocol_free().
 */
ProtocolDef* pdef_parse_string(const char* source, char* error_msg, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* PDEF_PARSER_H */
