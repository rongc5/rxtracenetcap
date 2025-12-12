#include "pdef_types.h"
#include <string.h>

uint32_t field_type_size(FieldType type) {
    switch (type) {
        case FIELD_TYPE_UINT8:
        case FIELD_TYPE_INT8:
            return 1;
        case FIELD_TYPE_UINT16:
        case FIELD_TYPE_INT16:
            return 2;
        case FIELD_TYPE_UINT32:
        case FIELD_TYPE_INT32:
            return 4;
        case FIELD_TYPE_UINT64:
        case FIELD_TYPE_INT64:
            return 8;
        case FIELD_TYPE_BYTES:
        case FIELD_TYPE_STRING:
        case FIELD_TYPE_VARBYTES:
        case FIELD_TYPE_NESTED:
            return 0;
        default:
            return 0;
    }
}

const char* field_type_name(FieldType type) {
    switch (type) {
        case FIELD_TYPE_UINT8:    return "uint8";
        case FIELD_TYPE_UINT16:   return "uint16";
        case FIELD_TYPE_UINT32:   return "uint32";
        case FIELD_TYPE_UINT64:   return "uint64";
        case FIELD_TYPE_INT8:     return "int8";
        case FIELD_TYPE_INT16:    return "int16";
        case FIELD_TYPE_INT32:    return "int32";
        case FIELD_TYPE_INT64:    return "int64";
        case FIELD_TYPE_BYTES:    return "bytes";
        case FIELD_TYPE_STRING:   return "string";
        case FIELD_TYPE_VARBYTES: return "varbytes";
        case FIELD_TYPE_NESTED:   return "nested";
        default:                  return "unknown";
    }
}

const char* opcode_name(OpCode opcode) {
    switch (opcode) {
        case OP_LOAD_U8:         return "LOAD_U8";
        case OP_LOAD_U16_BE:     return "LOAD_U16_BE";
        case OP_LOAD_U16_LE:     return "LOAD_U16_LE";
        case OP_LOAD_U32_BE:     return "LOAD_U32_BE";
        case OP_LOAD_U32_LE:     return "LOAD_U32_LE";
        case OP_LOAD_U64_BE:     return "LOAD_U64_BE";
        case OP_LOAD_U64_LE:     return "LOAD_U64_LE";
        case OP_LOAD_I8:         return "LOAD_I8";
        case OP_LOAD_I16_BE:     return "LOAD_I16_BE";
        case OP_LOAD_I16_LE:     return "LOAD_I16_LE";
        case OP_LOAD_I32_BE:     return "LOAD_I32_BE";
        case OP_LOAD_I32_LE:     return "LOAD_I32_LE";
        case OP_LOAD_I64_BE:     return "LOAD_I64_BE";
        case OP_LOAD_I64_LE:     return "LOAD_I64_LE";
        case OP_CMP_EQ:          return "CMP_EQ";
        case OP_CMP_NE:          return "CMP_NE";
        case OP_CMP_GT:          return "CMP_GT";
        case OP_CMP_GE:          return "CMP_GE";
        case OP_CMP_LT:          return "CMP_LT";
        case OP_CMP_LE:          return "CMP_LE";
        case OP_CMP_MASK:        return "CMP_MASK";
        case OP_JUMP_IF_FALSE:   return "JUMP_IF_FALSE";
        case OP_JUMP:            return "JUMP";
        case OP_RETURN_TRUE:     return "RETURN_TRUE";
        case OP_RETURN_FALSE:    return "RETURN_FALSE";
        default:                 return "UNKNOWN";
    }
}
