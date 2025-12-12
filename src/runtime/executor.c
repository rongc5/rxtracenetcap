#include "executor.h"
#include "../utils/endian.h"
#include <string.h>


#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

bool execute_filter(const uint8_t* packet, uint32_t packet_len, const FilterRule* rule) {
    if (unlikely(!packet || !rule)) {
        return false;
    }


    if (unlikely(packet_len < rule->min_packet_size)) {
        return false;
    }


    return execute_bytecode(packet, packet_len, rule->bytecode, rule->bytecode_len);
}

bool execute_bytecode(const uint8_t* packet, uint32_t packet_len,
                      const Instruction* bytecode, uint32_t bytecode_len) {
    if (unlikely(!packet || !bytecode || bytecode_len == 0)) {
        return false;
    }

    uint32_t ip = 0;
    uint64_t acc = 0;
    bool cmp_result = false;

    while (ip < bytecode_len) {
        const Instruction* ins = &bytecode[ip];

        switch (ins->opcode) {


            case OP_LOAD_U8:

                if (unlikely(ins->offset + 1 > packet_len)) {
                    return false;
                }
                acc = packet[ins->offset];
                break;

            case OP_LOAD_U16_BE:
                if (unlikely(ins->offset + 2 > packet_len)) {
                    return false;
                }
                acc = read_u16_be(packet, ins->offset);
                break;

            case OP_LOAD_U16_LE:
                if (unlikely(ins->offset + 2 > packet_len)) {
                    return false;
                }
                acc = read_u16_le(packet, ins->offset);
                break;

            case OP_LOAD_U32_BE:
                if (unlikely(ins->offset + 4 > packet_len)) {
                    return false;
                }
                acc = read_u32_be(packet, ins->offset);
                break;

            case OP_LOAD_U32_LE:
                if (unlikely(ins->offset + 4 > packet_len)) {
                    return false;
                }
                acc = read_u32_le(packet, ins->offset);
                break;

            case OP_LOAD_U64_BE:
                if (unlikely(ins->offset + 8 > packet_len)) {
                    return false;
                }
                acc = read_u64_be(packet, ins->offset);
                break;

            case OP_LOAD_U64_LE:
                if (unlikely(ins->offset + 8 > packet_len)) {
                    return false;
                }
                acc = read_u64_le(packet, ins->offset);
                break;

            case OP_LOAD_I8:
                if (unlikely(ins->offset + 1 > packet_len)) {
                    return false;
                }
                acc = (uint64_t)(int64_t)read_i8(packet, ins->offset);
                break;

            case OP_LOAD_I16_BE:
                if (unlikely(ins->offset + 2 > packet_len)) {
                    return false;
                }
                acc = (uint64_t)(int64_t)read_i16_be(packet, ins->offset);
                break;

            case OP_LOAD_I16_LE:
                if (unlikely(ins->offset + 2 > packet_len)) {
                    return false;
                }
                acc = (uint64_t)(int64_t)read_i16_le(packet, ins->offset);
                break;

            case OP_LOAD_I32_BE:
                if (unlikely(ins->offset + 4 > packet_len)) {
                    return false;
                }
                acc = (uint64_t)(int64_t)read_i32_be(packet, ins->offset);
                break;

            case OP_LOAD_I32_LE:
                if (unlikely(ins->offset + 4 > packet_len)) {
                    return false;
                }
                acc = (uint64_t)(int64_t)read_i32_le(packet, ins->offset);
                break;

            case OP_LOAD_I64_BE:
                if (unlikely(ins->offset + 8 > packet_len)) {
                    return false;
                }
                acc = (uint64_t)read_i64_be(packet, ins->offset);
                break;

            case OP_LOAD_I64_LE:
                if (unlikely(ins->offset + 8 > packet_len)) {
                    return false;
                }
                acc = (uint64_t)read_i64_le(packet, ins->offset);
                break;



            case OP_CMP_EQ:
                cmp_result = (acc == ins->operand);
                break;

            case OP_CMP_NE:
                cmp_result = (acc != ins->operand);
                break;

            case OP_CMP_GT:
                cmp_result = (acc > ins->operand);
                break;

            case OP_CMP_GE:
                cmp_result = (acc >= ins->operand);
                break;

            case OP_CMP_LT:
                cmp_result = (acc < ins->operand);
                break;

            case OP_CMP_LE:
                cmp_result = (acc <= ins->operand);
                break;

            case OP_CMP_MASK:

                cmp_result = ((acc & ins->operand) == ins->operand2);
                break;



            case OP_JUMP_IF_FALSE:
                if (!cmp_result) {

                    if (unlikely(ins->jump_target >= bytecode_len)) {
                        return false;
                    }
                    ip = ins->jump_target;
                    continue;
                }
                break;

            case OP_JUMP:
                if (unlikely(ins->jump_target >= bytecode_len)) {
                    return false;
                }
                ip = ins->jump_target;
                continue;

            case OP_RETURN_TRUE:
                return true;

            case OP_RETURN_FALSE:
                return false;

            default:

                return false;
        }

        ip++;
    }


    return false;
}
