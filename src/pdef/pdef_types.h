#ifndef PDEF_TYPES_H
#define PDEF_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif









typedef enum {
    FIELD_TYPE_UINT8,
    FIELD_TYPE_UINT16,
    FIELD_TYPE_UINT32,
    FIELD_TYPE_UINT64,
    FIELD_TYPE_INT8,
    FIELD_TYPE_INT16,
    FIELD_TYPE_INT32,
    FIELD_TYPE_INT64,
    FIELD_TYPE_BYTES,
    FIELD_TYPE_STRING,
    FIELD_TYPE_VARBYTES,
    FIELD_TYPE_NESTED,
} FieldType;

typedef enum {
    ENDIAN_BIG,
    ENDIAN_LITTLE,
} Endian;

typedef enum {
    ENDIAN_MODE_BIG = 0,
    ENDIAN_MODE_LITTLE,
    ENDIAN_MODE_AUTO,
} EndianMode;

typedef enum {
    ENDIAN_TYPE_UNKNOWN = 0,
    ENDIAN_TYPE_BIG,
    ENDIAN_TYPE_LITTLE,
} EndianType;



typedef struct {
    char        name[128];
    FieldType   type;
    uint32_t    offset;
    uint32_t    size;
    Endian      endian;
    bool        is_variable;
    bool        is_array;
    uint32_t    array_size;
} Field;



typedef struct {
    char        name[64];
    Field*      fields;
    uint32_t    field_count;
    uint32_t    min_size;
    bool        has_variable;
} StructDef;



typedef enum {

    OP_LOAD_U8,
    OP_LOAD_U16_BE,
    OP_LOAD_U16_LE,
    OP_LOAD_U32_BE,
    OP_LOAD_U32_LE,
    OP_LOAD_U64_BE,
    OP_LOAD_U64_LE,
    OP_LOAD_I8,
    OP_LOAD_I16_BE,
    OP_LOAD_I16_LE,
    OP_LOAD_I32_BE,
    OP_LOAD_I32_LE,
    OP_LOAD_I64_BE,
    OP_LOAD_I64_LE,


    OP_CMP_EQ,
    OP_CMP_NE,
    OP_CMP_GT,
    OP_CMP_GE,
    OP_CMP_LT,
    OP_CMP_LE,
    OP_CMP_MASK,


    OP_JUMP_IF_FALSE,
    OP_JUMP,
    OP_RETURN_TRUE,
    OP_RETURN_FALSE,
} OpCode;

typedef struct {
    OpCode      opcode;
    uint32_t    offset;
    uint64_t    operand;
    uint64_t    operand2;
    uint32_t    jump_target;
} Instruction;



typedef struct {
    char            name[64];
    char            struct_name[64];
    Instruction*    bytecode;
    uint32_t        bytecode_len;
    Instruction*    bytecode_be;
    uint32_t        bytecode_be_len;
    Instruction*    bytecode_le;
    uint32_t        bytecode_le_len;
    uint32_t        min_packet_size;
    bool            sliding_window;
    uint32_t        sliding_max_offset;
} FilterRule;



typedef struct {
    char            name[64];
    uint16_t*       ports;
    uint32_t        port_count;
    Endian          default_endian;

    StructDef*      structs;
    uint32_t        struct_count;

    FilterRule*     filters;
    uint32_t        filter_count;


    struct ConstantEntry* constants;
    uint32_t        constant_count;


    EndianMode      endian_mode;
    volatile int    detected_endian;
    volatile int    endian_writeback_done;
    char            pdef_file_path[256];
} ProtocolDef;



typedef struct ConstantEntry {
    char                    name[64];
    uint64_t                value;
    struct ConstantEntry*   next;
} ConstantEntry;




uint32_t field_type_size(FieldType type);


const char* field_type_name(FieldType type);


const char* opcode_name(OpCode opcode);

#ifdef __cplusplus
}
#endif

#endif
