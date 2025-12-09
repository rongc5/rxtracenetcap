#include "protocol.h"
#include "executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper function: try to match packet with endian selection */
static bool try_match_with_endian(
    const uint8_t* packet, uint32_t packet_len,
    const FilterRule* rule,
    ProtocolDef* mutable_proto)
{
    if (!packet || !rule || !mutable_proto) {
        return false;
    }

    const Instruction* code_be = rule->bytecode_be ? rule->bytecode_be : rule->bytecode;
    uint32_t len_be = rule->bytecode_be_len ? rule->bytecode_be_len : rule->bytecode_len;
    const Instruction* code_le = rule->bytecode_le ? rule->bytecode_le : rule->bytecode;
    uint32_t len_le = rule->bytecode_le_len ? rule->bytecode_le_len : rule->bytecode_len;

    switch (mutable_proto->endian_mode) {
        case ENDIAN_MODE_BIG:
            /* Force big-endian */
            return execute_bytecode(packet, packet_len, code_be, len_be);

        case ENDIAN_MODE_LITTLE:
            /* Force little-endian */
            return execute_bytecode(packet, packet_len, code_le, len_le);

        case ENDIAN_MODE_AUTO:
        default: {
            /* Auto-detection mode */
            /* Use GCC built-in for atomic read (C++98 compatible) */
            __sync_synchronize();  /* Memory barrier */
            int detected = mutable_proto->detected_endian;

            if (detected == ENDIAN_TYPE_BIG) {
                /* Already detected as big-endian */
                return execute_bytecode(packet, packet_len, code_be, len_be);
            }

            if (detected == ENDIAN_TYPE_LITTLE) {
                /* Already detected as little-endian */
                return execute_bytecode(packet, packet_len, code_le, len_le);
            }

            /* Not yet detected (ENDIAN_TYPE_UNKNOWN) - try both */

            /* Try big-endian first */
            if (execute_bytecode(packet, packet_len, code_be, len_be)) {
                /* Success with big-endian! Use CAS to update atomically */
                /* __sync_val_compare_and_swap returns old value */
                int old_val = __sync_val_compare_and_swap(&mutable_proto->detected_endian,
                                                           ENDIAN_TYPE_UNKNOWN,
                                                           ENDIAN_TYPE_BIG);
                if (old_val == ENDIAN_TYPE_UNKNOWN) {
                    /* We are the first thread to detect it */
                    fprintf(stderr, "[PDEF] Auto-detected endian: big-endian for %s\n",
                            mutable_proto->name);
                }
                return true;
            }

            /* Try little-endian */
            if (execute_bytecode(packet, packet_len, code_le, len_le)) {
                /* Success with little-endian! Use CAS to update atomically */
                int old_val = __sync_val_compare_and_swap(&mutable_proto->detected_endian,
                                                           ENDIAN_TYPE_UNKNOWN,
                                                           ENDIAN_TYPE_LITTLE);
                if (old_val == ENDIAN_TYPE_UNKNOWN) {
                    /* We are the first thread to detect it */
                    fprintf(stderr, "[PDEF] Auto-detected endian: little-endian for %s\n",
                            mutable_proto->name);
                }
                return true;
            }

            /* No match with either endian */
            return false;
        }
    }
}

bool packet_filter_match(const uint8_t* packet, uint32_t packet_len,
                         uint16_t port, const ProtocolDef* proto) {
    if (!packet || !proto) {
        return false;
    }

    /* Check if port matches (if ports are defined) */
    if (proto->port_count > 0) {
        bool port_match = false;
        for (uint32_t i = 0; i < proto->port_count; i++) {
            if (proto->ports[i] == port) {
                port_match = true;
                break;
            }
        }
        if (!port_match) {
            return false;
        }
    }

    /* Cast to mutable for atomic endian detection updates */
    ProtocolDef* mutable_proto = (ProtocolDef*)proto;

    /* Try each filter rule */
    for (uint32_t i = 0; i < proto->filter_count; i++) {
        const FilterRule* rule = &proto->filters[i];

        /* Sliding window logic */
        if (rule->sliding_window) {
            uint32_t search_limit = packet_len;
            if (rule->sliding_max_offset > 0 && rule->sliding_max_offset < packet_len) {
                search_limit = rule->sliding_max_offset;
            }

            for (uint32_t offset = 0; offset < search_limit; offset++) {
                uint32_t remaining = packet_len - offset;

                if (remaining < rule->min_packet_size) {
                    break;
                }

                if (try_match_with_endian(packet + offset, remaining, rule, mutable_proto)) {
                    return true;
                }
            }
        } else {
            /* Non-sliding window: match from start */
            if (try_match_with_endian(packet, packet_len, rule, mutable_proto)) {
                return true;
            }
        }
    }

    return false;  /* No rules matched */
}

void protocol_free(ProtocolDef* proto) {
    if (!proto) {
        return;
    }

    /* Free ports */
    if (proto->ports) {
        free(proto->ports);
    }

    /* Free structures */
    if (proto->structs) {
        for (uint32_t i = 0; i < proto->struct_count; i++) {
            if (proto->structs[i].fields) {
                free(proto->structs[i].fields);
            }
        }
        free(proto->structs);
    }

    /* Free filter rules */
    if (proto->filters) {
        for (uint32_t i = 0; i < proto->filter_count; i++) {
            if (proto->filters[i].bytecode) {
                free(proto->filters[i].bytecode);
            }
            if (proto->filters[i].bytecode_le) {
                free(proto->filters[i].bytecode_le);
            }
        }
        free(proto->filters);
    }

    /* Free constants */
    if (proto->constants) {
        ConstantEntry* entry = proto->constants;
        while (entry) {
            ConstantEntry* next = entry->next;
            free(entry);
            entry = next;
        }
    }

    free(proto);
}

const StructDef* protocol_find_struct(const ProtocolDef* proto, const char* name) {
    if (!proto || !name) {
        return NULL;
    }

    for (uint32_t i = 0; i < proto->struct_count; i++) {
        if (strcmp(proto->structs[i].name, name) == 0) {
            return &proto->structs[i];
        }
    }

    return NULL;
}

bool protocol_find_constant(const ProtocolDef* proto, const char* name, uint64_t* value) {
    if (!proto || !name || !value) {
        return false;
    }

    /* Simple linear search (could use hash table for large constant sets) */
    ConstantEntry* entry = proto->constants;
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            *value = entry->value;
            return true;
        }
        entry = entry->next;
    }

    return false;
}

void protocol_print(const ProtocolDef* proto) {
    if (!proto) {
        return;
    }

    printf("Protocol: %s\n", proto->name);
    printf("Default endian: %s\n", proto->default_endian == ENDIAN_BIG ? "big" : "little");

    /* Print ports */
    if (proto->port_count > 0) {
        printf("Ports: ");
        for (uint32_t i = 0; i < proto->port_count; i++) {
            printf("%u", proto->ports[i]);
            if (i < proto->port_count - 1) {
                printf(", ");
            }
        }
        printf("\n");
    }

    /* Print constants */
    if (proto->constant_count > 0) {
        printf("\nConstants (%u):\n", proto->constant_count);
        ConstantEntry* entry = proto->constants;
        while (entry) {
            printf("  %s = 0x%lx (%lu)\n", entry->name, entry->value, entry->value);
            entry = entry->next;
        }
    }

    /* Print structures */
    printf("\nStructures (%u):\n", proto->struct_count);
    for (uint32_t i = 0; i < proto->struct_count; i++) {
        const StructDef* s = &proto->structs[i];
        printf("  %s (min_size=%u, has_variable=%d)\n",
               s->name, s->min_size, s->has_variable);
        for (uint32_t j = 0; j < s->field_count; j++) {
            const Field* f = &s->fields[j];
            printf("    [%4u] %-20s %s (size=%u, endian=%s)\n",
                   f->offset, f->name, field_type_name(f->type), f->size,
                   f->endian == ENDIAN_BIG ? "big" : "little");
        }
    }

    /* Print filter rules */
    printf("\nFilter Rules (%u):\n", proto->filter_count);
    for (uint32_t i = 0; i < proto->filter_count; i++) {
        const FilterRule* r = &proto->filters[i];
        printf("  %s (struct=%s, min_size=%u, bytecode_len=%u)\n",
               r->name, r->struct_name, r->min_packet_size, r->bytecode_len);
    }
}

void filter_rule_disassemble(const FilterRule* rule) {
    if (!rule) {
        return;
    }

    printf("Filter: %s\n", rule->name);
    printf("Structure: %s\n", rule->struct_name);
    printf("Min packet size: %u\n", rule->min_packet_size);
    printf("Bytecode (%u instructions):\n", rule->bytecode_len);

    for (uint32_t i = 0; i < rule->bytecode_len; i++) {
        const Instruction* ins = &rule->bytecode[i];
        printf("  %4u: %-16s", i, opcode_name(ins->opcode));

        switch (ins->opcode) {
            case OP_LOAD_U8:
            case OP_LOAD_U16_BE:
            case OP_LOAD_U16_LE:
            case OP_LOAD_U32_BE:
            case OP_LOAD_U32_LE:
            case OP_LOAD_U64_BE:
            case OP_LOAD_U64_LE:
            case OP_LOAD_I8:
            case OP_LOAD_I16_BE:
            case OP_LOAD_I16_LE:
            case OP_LOAD_I32_BE:
            case OP_LOAD_I32_LE:
            case OP_LOAD_I64_BE:
            case OP_LOAD_I64_LE:
                printf("offset=%u", ins->offset);
                break;

            case OP_CMP_EQ:
            case OP_CMP_NE:
            case OP_CMP_GT:
            case OP_CMP_GE:
            case OP_CMP_LT:
            case OP_CMP_LE:
                printf("value=0x%lx (%ld)", ins->operand, (int64_t)ins->operand);
                break;

            case OP_CMP_MASK:
                printf("mask=0x%lx, expected=0x%lx", ins->operand, ins->operand2);
                break;

            case OP_JUMP_IF_FALSE:
            case OP_JUMP:
                printf("target=%u", ins->jump_target);
                break;

            default:
                break;
        }

        printf("\n");
    }
}
