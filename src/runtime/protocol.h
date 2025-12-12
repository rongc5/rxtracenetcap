#ifndef PDEF_PROTOCOL_H
#define PDEF_PROTOCOL_H

#include "../pdef/pdef_types.h"

#ifdef __cplusplus
extern "C" {
#endif






















bool packet_filter_match(const uint8_t* packet, uint32_t packet_len,
                         uint16_t port, const ProtocolDef* proto);






void protocol_free(ProtocolDef* proto);








const StructDef* protocol_find_struct(const ProtocolDef* proto, const char* name);









bool protocol_find_constant(const ProtocolDef* proto, const char* name, uint64_t* value);






void protocol_print(const ProtocolDef* proto);






void filter_rule_disassemble(const FilterRule* rule);

#ifdef __cplusplus
}
#endif

#endif
