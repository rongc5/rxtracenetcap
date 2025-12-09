#ifndef PDEF_TYPES_H
#define PDEF_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Note: We use plain int/bool types for endian fields.
 * Atomic operations are performed using GCC built-in functions
 * (__sync_val_compare_and_swap, etc.) for C++98 compatibility.
 */

/* ========== Field Types ========== */

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
    FIELD_TYPE_NESTED,      /* For nested struct references */
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

/* ========== Field Definition ========== */

typedef struct {
    char        name[128];      /* Field name (with nested path, e.g., "header.magic") */
    FieldType   type;           /* Field type */
    uint32_t    offset;         /* Absolute offset in bytes (after flattening) */
    uint32_t    size;           /* Field size in bytes */
    Endian      endian;         /* Byte order */
    bool        is_variable;    /* Is this a variable-length field? */
    bool        is_array;       /* Is this field an array (Type field[N])? */
    uint32_t    array_size;     /* Number of elements for arrays (1 for non-array) */
} Field;

/* ========== Structure Definition ========== */

typedef struct {
    char        name[64];       /* Structure name */
    Field*      fields;         /* Array of fields (flattened) */
    uint32_t    field_count;    /* Number of fields */
    uint32_t    min_size;       /* Minimum packet size (excluding variable-length part) */
    bool        has_variable;   /* Contains variable-length field? */
} StructDef;

/* ========== Bytecode Instructions ========== */

typedef enum {
    /* Load instructions */
    OP_LOAD_U8,         /* Load uint8 from packet */
    OP_LOAD_U16_BE,     /* Load uint16 (big-endian) */
    OP_LOAD_U16_LE,     /* Load uint16 (little-endian) */
    OP_LOAD_U32_BE,     /* Load uint32 (big-endian) */
    OP_LOAD_U32_LE,     /* Load uint32 (little-endian) */
    OP_LOAD_U64_BE,     /* Load uint64 (big-endian) */
    OP_LOAD_U64_LE,     /* Load uint64 (little-endian) */
    OP_LOAD_I8,         /* Load int8 */
    OP_LOAD_I16_BE,     /* Load int16 (big-endian) */
    OP_LOAD_I16_LE,     /* Load int16 (little-endian) */
    OP_LOAD_I32_BE,     /* Load int32 (big-endian) */
    OP_LOAD_I32_LE,     /* Load int32 (little-endian) */
    OP_LOAD_I64_BE,     /* Load int64 (big-endian) */
    OP_LOAD_I64_LE,     /* Load int64 (little-endian) */

    /* Comparison instructions */
    OP_CMP_EQ,          /* Compare equal (==) */
    OP_CMP_NE,          /* Compare not equal (!=) */
    OP_CMP_GT,          /* Greater than (>) */
    OP_CMP_GE,          /* Greater or equal (>=) */
    OP_CMP_LT,          /* Less than (<) */
    OP_CMP_LE,          /* Less or equal (<=) */
    OP_CMP_MASK,        /* Mask comparison (value & mask == expected) */

    /* Control flow */
    OP_JUMP_IF_FALSE,   /* Conditional jump (if accumulator is false) */
    OP_JUMP,            /* Unconditional jump */
    OP_RETURN_TRUE,     /* Return match success */
    OP_RETURN_FALSE,    /* Return match failure */
} OpCode;

typedef struct {
    OpCode      opcode;         /* Operation code */
    uint32_t    offset;         /* Data offset for LOAD instructions */
    uint64_t    operand;        /* Operand (comparison value, mask, etc.) */
    uint64_t    operand2;       /* Second operand (for OP_CMP_MASK: expected value after masking) */
    uint32_t    jump_target;    /* Jump target (instruction index) */
} Instruction;

/* ========== Filter Rule ========== */

typedef struct {
    char            name[64];           /* Rule name */
    char            struct_name[64];    /* Associated structure name */
    Instruction*    bytecode;           /* Bytecode instruction sequence */
    uint32_t        bytecode_len;       /* Number of instructions */
    Instruction*    bytecode_be;        /* Big-endian bytecode */
    uint32_t        bytecode_be_len;    /* Big-endian bytecode length */
    Instruction*    bytecode_le;        /* Little-endian bytecode */
    uint32_t        bytecode_le_len;    /* Little-endian bytecode length */
    uint32_t        min_packet_size;    /* Minimum packet size requirement */
    bool            sliding_window;     /* Enable sliding window matching */
    uint32_t        sliding_max_offset; /* Maximum search offset (0 = unlimited) */
} FilterRule;

/* ========== Protocol Definition ========== */

typedef struct {
    char            name[64];           /* Protocol name */
    uint16_t*       ports;              /* Port list */
    uint32_t        port_count;         /* Number of ports */
    Endian          default_endian;     /* Default byte order */

    StructDef*      structs;            /* Array of structures */
    uint32_t        struct_count;       /* Number of structures */

    FilterRule*     filters;            /* Array of filter rules */
    uint32_t        filter_count;       /* Number of filter rules */

    /* Constants table (name -> value mapping) */
    struct ConstantEntry* constants;    /* Hash table for constants */
    uint32_t        constant_count;     /* Number of constants */

    /* Endian auto-detection support */
    EndianMode      endian_mode;           /* Configured endian mode */
    volatile int    detected_endian;       /* Runtime-detected endian (for AUTO) - use atomic ops */
    volatile int    endian_writeback_done; /* Whether writeback message has been sent - use atomic ops */
    char            pdef_file_path[256];   /* Source path (set by pdef_parse_file) */
} ProtocolDef;

/* ========== Constant Entry ========== */

typedef struct ConstantEntry {
    char                    name[64];   /* Constant name */
    uint64_t                value;      /* Constant value */
    struct ConstantEntry*   next;       /* For hash table chaining */
} ConstantEntry;

/* ========== Helper Functions ========== */

/* Get field type size in bytes (0 for variable-length types) */
uint32_t field_type_size(FieldType type);

/* Get field type name as string */
const char* field_type_name(FieldType type);

/* Get opcode name as string (for debugging) */
const char* opcode_name(OpCode opcode);

#ifdef __cplusplus
}
#endif

#endif /* PDEF_TYPES_H */
