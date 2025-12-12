#ifndef PDEF_EXECUTOR_H
#define PDEF_EXECUTOR_H

#include "../pdef/pdef_types.h"

#ifdef __cplusplus
extern "C" {
#endif





















bool execute_filter(const uint8_t* packet, uint32_t packet_len, const FilterRule* rule);










bool execute_bytecode(const uint8_t* packet, uint32_t packet_len,
                      const Instruction* bytecode, uint32_t bytecode_len);

#ifdef __cplusplus
}
#endif

#endif
