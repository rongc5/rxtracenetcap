#ifndef PDEF_PARSER_H
#define PDEF_PARSER_H

#include "pdef_types.h"

#ifdef __cplusplus
extern "C" {
#endif

















ProtocolDef* pdef_parse_file(const char* filename, char* error_msg, size_t error_size);











ProtocolDef* pdef_parse_string(const char* source, char* error_msg, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
