#include "parser.h"
#include "lexer.h"
#include "../runtime/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>


typedef enum {
    COND_EQ,
    COND_NE,
    COND_GT,
    COND_GE,
    COND_LT,
    COND_LE,
    COND_MASK,
    COND_IN,
    COND_NOT_IN,
} ConditionOp;

typedef struct {
    char            field_name[128];
    ConditionOp     op;
    uint64_t        value;
    uint64_t        mask;
    uint64_t*       values;
    uint32_t        value_count;
} FilterCondition;

typedef struct {
    char                name[64];
    char                struct_name[64];
    FilterCondition*    conditions;
    uint32_t            cond_count;
    bool                sliding_window;
    uint32_t            sliding_max_offset;
} TempFilterRule;


typedef struct {
    Lexer       lexer;
    Token       current_token;
    Token       peek_token;
    char        error_msg[512];


    ProtocolDef*    proto;
    StructDef*      temp_structs;
    uint32_t        temp_struct_cap;
    FilterRule*     temp_filters;
    uint32_t        temp_filter_cap;
    TempFilterRule* temp_filter_rules;
    uint32_t        temp_filter_rule_cap;
    uint32_t        temp_filter_rule_count;


    bool            endian_set;
} Parser;


static bool parse_protocol_block(Parser* p);
static bool parse_const_block(Parser* p);
static bool parse_struct_def(Parser* p);
static bool parse_filter_block(Parser* p);
static bool flatten_structs(Parser* p);
static bool compile_filter_rules(Parser* p);


static void parser_error(Parser* p, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(p->error_msg, sizeof(p->error_msg), format, args);
    va_end(args);
}

static bool parser_next_token(Parser* p) {
    p->current_token = p->peek_token;
    return lexer_next_token(&p->lexer, &p->peek_token);
}

static bool parser_expect(Parser* p, TokenType type) {
    if (p->current_token.type != type) {
        parser_error(p, "Expected token type %d, got %d at line %u",
                     type, p->current_token.type, p->current_token.line);
        return false;
    }
    return parser_next_token(p);
}


static bool parse_nested_field_names(Parser* p, const Field* nested_field,
                                     char* type_name, size_t type_len,
                                     char* field_name, size_t field_len) {
    const char* dot = strchr(nested_field->name, '.');
    if (!dot) {
        parser_error(p, "Invalid nested field name: %s", nested_field->name);
        return false;
    }

    size_t prefix_len = (size_t)(dot - nested_field->name);
    if (prefix_len >= type_len) {
        parser_error(p, "Nested type name too long: %s", nested_field->name);
        return false;
    }
    memcpy(type_name, nested_field->name, prefix_len);
    type_name[prefix_len] = '\0';

    if (strlen(dot + 1) >= field_len) {
        parser_error(p, "Nested field name too long: %s", nested_field->name);
        return false;
    }
    strncpy(field_name, dot + 1, field_len - 1);
    field_name[field_len - 1] = '\0';
    return true;
}

static bool append_token(Parser* p, char* out, size_t out_size, const char* token) {
    size_t len_out = strlen(out);
    size_t len_token = strlen(token);
    if (len_out + len_token >= out_size) {
        parser_error(p, "Field path too long");
        return false;
    }
    memcpy(out + len_out, token, len_token);
    out[len_out + len_token] = '\0';
    return true;
}


static bool parse_field_path(Parser* p, char* out, size_t out_size) {
    if (p->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(p, "Expected field name");
        return false;
    }

    out[0] = '\0';
    while (true) {

        if (!append_token(p, out, out_size, p->current_token.text)) {
            return false;
        }
        parser_next_token(p);


        while (p->current_token.type == TOKEN_LBRACKET) {
            if (!append_token(p, out, out_size, "[")) {
                return false;
            }
            parser_next_token(p);
            if (p->current_token.type != TOKEN_NUMBER) {
                parser_error(p, "Expected index inside []");
                return false;
            }
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%llu",
                     (unsigned long long)p->current_token.value);
            if (!append_token(p, out, out_size, num_buf)) {
                return false;
            }
            parser_next_token(p);
            if (!parser_expect(p, TOKEN_RBRACKET)) {
                return false;
            }
            if (!append_token(p, out, out_size, "]")) {
                return false;
            }
        }

        if (p->current_token.type == TOKEN_DOT) {
            if (!append_token(p, out, out_size, ".")) {
                return false;
            }
            parser_next_token(p);
            if (p->current_token.type != TOKEN_IDENTIFIER) {
                parser_error(p, "Expected field name after '.'");
                return false;
            }
            continue;
        }
        break;
    }

    return true;
}

static bool parser_init(Parser* p, const char* source) {
    memset(p, 0, sizeof(Parser));
    lexer_init(&p->lexer, source);
    p->endian_set = false;


    p->proto = (ProtocolDef*)calloc(1, sizeof(ProtocolDef));
    if (!p->proto) {
        parser_error(p, "Memory allocation failed");
        return false;
    }


    p->proto->default_endian = ENDIAN_BIG;
    p->proto->endian_mode = ENDIAN_MODE_AUTO;
    p->proto->detected_endian = ENDIAN_TYPE_UNKNOWN;
    p->proto->endian_writeback_done = 0;


    p->temp_struct_cap = 16;
    p->temp_structs = (StructDef*)calloc(p->temp_struct_cap, sizeof(StructDef));
    p->temp_filter_cap = 16;
    p->temp_filters = (FilterRule*)calloc(p->temp_filter_cap, sizeof(FilterRule));
    p->temp_filter_rule_cap = 16;
    p->temp_filter_rules = (TempFilterRule*)calloc(p->temp_filter_rule_cap, sizeof(TempFilterRule));

    if (!p->temp_structs || !p->temp_filters || !p->temp_filter_rules) {
        parser_error(p, "Memory allocation failed");
        return false;
    }


    lexer_next_token(&p->lexer, &p->peek_token);
    parser_next_token(p);

    return true;
}

static void parser_cleanup(Parser* p) {
    if (p->temp_structs && p->proto) {
        for (uint32_t i = 0; i < p->proto->struct_count; i++) {
            if (p->temp_structs[i].fields) {
                free(p->temp_structs[i].fields);
            }
        }
        free(p->temp_structs);
    }
    if (p->temp_filters) {
        free(p->temp_filters);
    }
    if (p->temp_filter_rules) {
        for (uint32_t i = 0; i < p->temp_filter_rule_count; i++) {
            if (p->temp_filter_rules[i].conditions) {
                for (uint32_t j = 0; j < p->temp_filter_rules[i].cond_count; j++) {
                    if (p->temp_filter_rules[i].conditions[j].values) {
                        free(p->temp_filter_rules[i].conditions[j].values);
                    }
                }
                free(p->temp_filter_rules[i].conditions);
            }
        }
        free(p->temp_filter_rules);
    }
}

static bool add_constant(Parser* p, const char* name, uint64_t value) {
    ConstantEntry* entry = (ConstantEntry*)malloc(sizeof(ConstantEntry));
    if (!entry) {
        parser_error(p, "Memory allocation failed");
        return false;
    }

    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->value = value;
    entry->next = p->proto->constants;
    p->proto->constants = entry;
    p->proto->constant_count++;

    return true;
}

static bool lookup_constant(Parser* p, const char* name, uint64_t* value) {
    return protocol_find_constant(p->proto, name, value);
}

static bool parse_protocol_block(Parser* p) {

    if (!parser_expect(p, TOKEN_PROTOCOL)) return false;
    if (!parser_expect(p, TOKEN_LBRACE)) return false;


    while (p->current_token.type != TOKEN_RBRACE) {
        if (p->current_token.type != TOKEN_IDENTIFIER) {
            parser_error(p, "Expected identifier in @protocol block");
            return false;
        }

        char key[64];
        strncpy(key, p->current_token.text, sizeof(key) - 1);
        parser_next_token(p);

        if (!parser_expect(p, TOKEN_ASSIGN)) return false;

        if (strcmp(key, "name") == 0) {

            if (p->current_token.type == TOKEN_STRING) {
                strncpy(p->proto->name, p->current_token.text, sizeof(p->proto->name) - 1);
            } else if (p->current_token.type == TOKEN_IDENTIFIER) {
                strncpy(p->proto->name, p->current_token.text, sizeof(p->proto->name) - 1);
            } else {
                parser_error(p, "Expected string or identifier for protocol name");
                return false;
            }
            parser_next_token(p);
        } else if (strcmp(key, "endian") == 0) {

            if (p->current_token.type != TOKEN_IDENTIFIER) {
                parser_error(p, "Expected 'big' or 'little' or 'auto' for endian");
                return false;
            }
            if (strcmp(p->current_token.text, "big") == 0) {
                p->proto->default_endian = ENDIAN_BIG;
                p->proto->endian_mode = ENDIAN_MODE_BIG;
            } else if (strcmp(p->current_token.text, "little") == 0) {
                p->proto->default_endian = ENDIAN_LITTLE;
                p->proto->endian_mode = ENDIAN_MODE_LITTLE;
            } else if (strcmp(p->current_token.text, "auto") == 0) {
                p->proto->default_endian = ENDIAN_BIG;
                p->proto->endian_mode = ENDIAN_MODE_AUTO;
            } else {
                parser_error(p, "Expected 'big' or 'little' or 'auto' for endian, got '%s'",
                             p->current_token.text);
                return false;
            }
            p->endian_set = true;
            parser_next_token(p);
        } else if (strcmp(key, "ports") == 0) {



            fprintf(stderr, "[PDEF WARN] Protocol '%s': 'ports' field is DEPRECATED and will be IGNORED.\n",
                    p->proto->name);
            fprintf(stderr, "[PDEF WARN] Use BPF filter ('filter' or 'port_filter' in API) for port filtering.\n");
            fprintf(stderr, "[PDEF WARN] PDEF is now responsible for protocol content filtering only.\n");


            do {
                if (p->current_token.type == TOKEN_COMMA) {
                    parser_next_token(p);
                }

                if (p->current_token.type != TOKEN_NUMBER) {
                    parser_error(p, "Expected port number");
                    return false;
                }

                parser_next_token(p);
            } while (p->current_token.type == TOKEN_COMMA);


            p->proto->ports = NULL;
            p->proto->port_count = 0;
        } else {
            parser_error(p, "Unknown protocol metadata key: %s", key);
            return false;
        }

        if (p->current_token.type == TOKEN_SEMICOLON) {
            parser_next_token(p);
        }
    }


    if (!p->endian_set) {
        fprintf(stderr, "[PDEF] endian not specified; defaulting to auto-detect (big-endian preferred)\n");
    } else {
        const char* mode_str = "auto";
        if (p->proto->endian_mode == ENDIAN_MODE_BIG) mode_str = "big-endian";
        else if (p->proto->endian_mode == ENDIAN_MODE_LITTLE) mode_str = "little-endian";
        fprintf(stderr, "[PDEF] endian configured: %s\n", mode_str);
    }

    return parser_expect(p, TOKEN_RBRACE);
}

static bool parse_const_block(Parser* p) {

    if (!parser_expect(p, TOKEN_CONST)) return false;
    if (!parser_expect(p, TOKEN_LBRACE)) return false;

    while (p->current_token.type != TOKEN_RBRACE) {
        if (p->current_token.type != TOKEN_IDENTIFIER) {
            parser_error(p, "Expected constant name");
            return false;
        }

        char name[64];
        strncpy(name, p->current_token.text, sizeof(name) - 1);
        parser_next_token(p);

        if (!parser_expect(p, TOKEN_ASSIGN)) return false;

        if (p->current_token.type != TOKEN_NUMBER) {
            parser_error(p, "Expected constant value");
            return false;
        }

        uint64_t value = p->current_token.value;
        parser_next_token(p);

        if (!add_constant(p, name, value)) {
            return false;
        }

        if (p->current_token.type == TOKEN_SEMICOLON) {
            parser_next_token(p);
        }
    }

    return parser_expect(p, TOKEN_RBRACE);
}

static bool parse_filter_block(Parser* p) {

    if (!parser_expect(p, TOKEN_FILTER)) return false;


    if (p->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(p, "Expected filter name");
        return false;
    }


    if (p->proto->filter_count >= p->temp_filter_rule_cap) {
        parser_error(p, "Too many filters");
        return false;
    }

    TempFilterRule* rule = &p->temp_filter_rules[p->proto->filter_count];
    memset(rule, 0, sizeof(TempFilterRule));

    strncpy(rule->name, p->current_token.text, sizeof(rule->name) - 1);
    parser_next_token(p);

    if (!parser_expect(p, TOKEN_LBRACE)) return false;


    uint32_t cond_cap = 8;
    rule->conditions = (FilterCondition*)calloc(cond_cap, sizeof(FilterCondition));
    if (!rule->conditions) {
        parser_error(p, "Memory allocation failed");
        return false;
    }


    while (p->current_token.type != TOKEN_RBRACE) {

        if (p->current_token.type == TOKEN_IDENTIFIER) {
            if (strcmp(p->current_token.text, "sliding") == 0) {
                parser_next_token(p);
                if (!parser_expect(p, TOKEN_ASSIGN)) return false;


                if (p->current_token.type == TOKEN_IDENTIFIER) {
                    if (strcmp(p->current_token.text, "true") == 0) {
                        rule->sliding_window = true;
                    } else if (strcmp(p->current_token.text, "false") == 0) {
                        rule->sliding_window = false;
                    } else {
                        parser_error(p, "Expected 'true' or 'false' for sliding");
                        return false;
                    }
                } else if (p->current_token.type == TOKEN_NUMBER) {
                    rule->sliding_window = (p->current_token.value != 0);
                } else {
                    parser_error(p, "Expected boolean value for sliding");
                    return false;
                }
                parser_next_token(p);


                if (p->current_token.type == TOKEN_SEMICOLON) {
                    parser_next_token(p);
                }
                continue;
            } else if (strcmp(p->current_token.text, "sliding_max") == 0) {
                parser_next_token(p);
                if (!parser_expect(p, TOKEN_ASSIGN)) return false;

                if (p->current_token.type != TOKEN_NUMBER) {
                    parser_error(p, "Expected number for sliding_max");
                    return false;
                }
                rule->sliding_max_offset = (uint32_t)p->current_token.value;
                parser_next_token(p);


                if (p->current_token.type == TOKEN_SEMICOLON) {
                    parser_next_token(p);
                }
                continue;
            }
        }


        if (rule->cond_count >= cond_cap) {
            cond_cap *= 2;
            FilterCondition* new_conds = (FilterCondition*)realloc(rule->conditions,
                                                                    cond_cap * sizeof(FilterCondition));
            if (!new_conds) {
                parser_error(p, "Memory allocation failed");
                return false;
            }
            rule->conditions = new_conds;
        }

        FilterCondition* cond = &rule->conditions[rule->cond_count];
        memset(cond, 0, sizeof(FilterCondition));


        if (p->current_token.type != TOKEN_IDENTIFIER) {
            parser_error(p, "Expected field name or configuration");
            return false;
        }

        char field_path[128] = "";
        if (!parse_field_path(p, field_path, sizeof(field_path))) {
            return false;
        }
        strncpy(cond->field_name, field_path, sizeof(cond->field_name) - 1);


        if (p->current_token.type == TOKEN_AND) {

            parser_next_token(p);


            uint64_t mask_val = 0;
            if (p->current_token.type == TOKEN_NUMBER) {
                mask_val = p->current_token.value;
            } else if (p->current_token.type == TOKEN_IDENTIFIER) {

                if (!lookup_constant(p, p->current_token.text, &mask_val)) {
                    parser_error(p, "Undefined constant '%s'", p->current_token.text);
                    return false;
                }
            } else {
                parser_error(p, "Expected mask value");
                return false;
            }
            cond->mask = mask_val;
            parser_next_token(p);


            if (!parser_expect(p, TOKEN_ASSIGN)) return false;
            cond->op = COND_MASK;
        } else {

            bool negate = false;
            if (p->current_token.type == TOKEN_NOT) {
                negate = true;
                parser_next_token(p);
            }

            if (p->current_token.type == TOKEN_IN ||
                (p->current_token.type == TOKEN_IDENTIFIER &&
                 strcmp(p->current_token.text, "in") == 0)) {
                cond->op = negate ? COND_NOT_IN : COND_IN;
                parser_next_token(p);


                if (!parser_expect(p, TOKEN_LBRACKET)) return false;

                uint32_t val_cap = 4;
                cond->values = (uint64_t*)calloc(val_cap, sizeof(uint64_t));
                if (!cond->values) {
                    parser_error(p, "Memory allocation failed");
                    return false;
                }

                while (true) {
                    if (cond->value_count >= val_cap) {
                        val_cap *= 2;
                        uint64_t* new_vals = (uint64_t*)realloc(cond->values, val_cap * sizeof(uint64_t));
                        if (!new_vals) {
                            parser_error(p, "Memory allocation failed");
                            return false;
                        }
                        cond->values = new_vals;
                    }

                    uint64_t val = 0;
                    if (p->current_token.type == TOKEN_NUMBER) {
                        val = p->current_token.value;
                    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
                        if (!lookup_constant(p, p->current_token.text, &val)) {
                            parser_error(p, "Undefined constant '%s'", p->current_token.text);
                            return false;
                        }
                    } else {
                        parser_error(p, "Expected value in list");
                        return false;
                    }
                    cond->values[cond->value_count++] = val;
                    parser_next_token(p);

                    if (p->current_token.type == TOKEN_COMMA) {
                        parser_next_token(p);
                        continue;
                    } else if (p->current_token.type == TOKEN_RBRACKET) {
                        parser_next_token(p);
                        break;
                    } else {
                        parser_error(p, "Expected ',' or ']' in list");
                        return false;
                    }
                }

                if (cond->value_count == 0) {
                    parser_error(p, "Empty value list is not allowed");
                    return false;
                }
            } else {
                switch (p->current_token.type) {
                    case TOKEN_ASSIGN:
                    case TOKEN_EQ:
                        cond->op = COND_EQ;
                        break;
                    case TOKEN_NE:
                        cond->op = COND_NE;
                        break;
                    case TOKEN_GT:
                        cond->op = COND_GT;
                        break;
                    case TOKEN_GE:
                        cond->op = COND_GE;
                        break;
                    case TOKEN_LT:
                        cond->op = COND_LT;
                        break;
                    case TOKEN_LE:
                        cond->op = COND_LE;
                        break;
                    default:
                        parser_error(p, "Expected comparison operator");
                        return false;
                }
                parser_next_token(p);
            }
        }


        if (cond->op != COND_IN && cond->op != COND_NOT_IN) {
            if (p->current_token.type == TOKEN_NUMBER) {
                cond->value = p->current_token.value;
            } else if (p->current_token.type == TOKEN_IDENTIFIER) {

                if (!lookup_constant(p, p->current_token.text, &cond->value)) {
                    parser_error(p, "Undefined constant '%s'", p->current_token.text);
                    return false;
                }
            } else {
                parser_error(p, "Expected value");
                return false;
            }
            parser_next_token(p);
            cond->value_count = 0;
        }

        rule->cond_count++;


        if (p->current_token.type == TOKEN_SEMICOLON) {
            parser_next_token(p);
        }
    }

    p->proto->filter_count++;
    p->temp_filter_rule_count++;
    return parser_expect(p, TOKEN_RBRACE);
}

static FieldType token_to_field_type(const Token* token) {
    switch (token->type) {
        case TOKEN_UINT8:        return FIELD_TYPE_UINT8;
        case TOKEN_UINT16:       return FIELD_TYPE_UINT16;
        case TOKEN_UINT32:       return FIELD_TYPE_UINT32;
        case TOKEN_UINT64:       return FIELD_TYPE_UINT64;
        case TOKEN_INT8:         return FIELD_TYPE_INT8;
        case TOKEN_INT16:        return FIELD_TYPE_INT16;
        case TOKEN_INT32:        return FIELD_TYPE_INT32;
        case TOKEN_INT64:        return FIELD_TYPE_INT64;
        case TOKEN_BYTES:        return FIELD_TYPE_BYTES;
        case TOKEN_STRING_TYPE:  return FIELD_TYPE_STRING;
        case TOKEN_VARBYTES:     return FIELD_TYPE_VARBYTES;
        case TOKEN_IDENTIFIER:   return FIELD_TYPE_NESTED;
        default:                 return FIELD_TYPE_UINT8;
    }
}

static bool parse_struct_def(Parser* p) {

    if (p->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(p, "Expected struct name");
        return false;
    }

    StructDef* s = &p->temp_structs[p->proto->struct_count];
    strncpy(s->name, p->current_token.text, sizeof(s->name) - 1);
    parser_next_token(p);

    if (!parser_expect(p, TOKEN_LBRACE)) return false;


    uint32_t field_cap = 16;
    s->fields = (Field*)calloc(field_cap, sizeof(Field));
    if (!s->fields) {
        parser_error(p, "Memory allocation failed");
        return false;
    }


    while (p->current_token.type != TOKEN_RBRACE) {

        if (s->has_variable) {
            parser_error(p, "Variable-length field must be the last field in struct '%s'", s->name);
            return false;
        }


        if (s->field_count >= field_cap) {
            field_cap *= 2;
            Field* new_fields = (Field*)realloc(s->fields, field_cap * sizeof(Field));
            if (!new_fields) {
                parser_error(p, "Memory allocation failed");
                return false;
            }
            s->fields = new_fields;
        }

        Field* f = &s->fields[s->field_count];
        memset(f, 0, sizeof(Field));
        f->array_size = 1;


        f->type = token_to_field_type(&p->current_token);


        char type_name[64] = "";
        if (f->type == FIELD_TYPE_NESTED) {
            strncpy(type_name, p->current_token.text, sizeof(type_name) - 1);
        }

        parser_next_token(p);


        if (f->type == FIELD_TYPE_BYTES || f->type == FIELD_TYPE_STRING) {
            if (p->current_token.type == TOKEN_LBRACKET) {
                parser_next_token(p);
                if (p->current_token.type != TOKEN_NUMBER) {
                    parser_error(p, "Expected array size");
                    return false;
                }
                f->size = (uint32_t)p->current_token.value;
                parser_next_token(p);
                if (!parser_expect(p, TOKEN_RBRACKET)) return false;
            } else {
                parser_error(p, "bytes/string requires array size [N]");
                return false;
            }
        }


        if (p->current_token.type != TOKEN_IDENTIFIER) {
            parser_error(p, "Expected field name");
            return false;
        }


        char field_name[64];
        strncpy(field_name, p->current_token.text, sizeof(field_name) - 1);
        field_name[sizeof(field_name) - 1] = '\0';

        if (f->type == FIELD_TYPE_NESTED) {

            snprintf(f->name, sizeof(f->name), "%s.%s", type_name, field_name);
        } else {
            strncpy(f->name, field_name, sizeof(f->name) - 1);
            if (f->type != FIELD_TYPE_BYTES && f->type != FIELD_TYPE_STRING) {
                f->size = field_type_size(f->type);
            }
        }

        parser_next_token(p);


        if (p->current_token.type == TOKEN_LBRACKET) {
            if (f->type != FIELD_TYPE_NESTED) {
                parser_error(p, "Array syntax is only supported for nested struct fields");
                return false;
            }

            f->is_array = true;
            parser_next_token(p);
            if (p->current_token.type != TOKEN_NUMBER) {
                parser_error(p, "Expected array size after '['");
                return false;
            }
            f->array_size = (uint32_t)p->current_token.value;
            if (f->array_size == 0) {
                parser_error(p, "Array size must be greater than 0");
                return false;
            }
            parser_next_token(p);
            if (!parser_expect(p, TOKEN_RBRACKET)) return false;
        }


        f->endian = p->proto->default_endian;


        if (f->type == FIELD_TYPE_VARBYTES) {
            f->is_variable = true;
            s->has_variable = true;
        }

        s->field_count++;


        if (p->current_token.type == TOKEN_SEMICOLON) {
            parser_next_token(p);
        }
    }

    p->proto->struct_count++;
    return parser_expect(p, TOKEN_RBRACE);
}





ProtocolDef* pdef_parse_string(const char* source, char* error_msg, size_t error_size) {
    Parser parser;

    if (!parser_init(&parser, source)) {
        if (error_msg && error_size > 0) {
            strncpy(error_msg, parser.error_msg, error_size - 1);
            error_msg[error_size - 1] = '\0';
        }
        if (parser.proto) {
            protocol_free(parser.proto);
        }
        parser_cleanup(&parser);
        return NULL;
    }


    while (parser.current_token.type != TOKEN_EOF) {
        switch (parser.current_token.type) {
            case TOKEN_PROTOCOL:
                if (!parse_protocol_block(&parser)) goto error;
                break;

            case TOKEN_CONST:
                if (!parse_const_block(&parser)) goto error;
                break;

            case TOKEN_FILTER:
                if (!parse_filter_block(&parser)) goto error;
                break;

            case TOKEN_IDENTIFIER:

                if (!parse_struct_def(&parser)) goto error;
                break;

            default:
                parser_error(&parser, "Unexpected token at top level");
                goto error;
        }
    }


    if (!flatten_structs(&parser)) goto error;
    if (!compile_filter_rules(&parser)) goto error;


    parser.proto->structs = parser.temp_structs;
    parser.temp_structs = NULL;
    parser.proto->filters = parser.temp_filters;
    parser.temp_filters = NULL;

    ProtocolDef* result = parser.proto;
    parser.proto = NULL;
    parser_cleanup(&parser);
    return result;

error:
    if (error_msg && error_size > 0) {
        strncpy(error_msg, parser.error_msg, error_size - 1);
        error_msg[error_size - 1] = '\0';
    }
    if (parser.proto) {
        protocol_free(parser.proto);
    }
    parser_cleanup(&parser);
    return NULL;
}


static StructDef* find_struct_by_name(Parser* p, const char* name) {
    for (uint32_t i = 0; i < p->proto->struct_count; i++) {
        if (strcmp(p->temp_structs[i].name, name) == 0) {
            return &p->temp_structs[i];
        }
    }
    return NULL;
}


static bool expand_nested_field(Parser* p, StructDef* parent, Field* nested_field,
                                 Field** expanded_fields, uint32_t* expanded_count,
                                 uint32_t* expanded_cap, uint32_t base_offset) {
    (void)parent;


    char type_name[64] = "";
    char field_name[64] = "";

    if (!parse_nested_field_names(p, nested_field, type_name, sizeof(type_name),
                                  field_name, sizeof(field_name))) {
        return false;
    }


    StructDef* nested_struct = find_struct_by_name(p, type_name);
    if (!nested_struct) {
        parser_error(p, "Nested struct '%s' not found", type_name);
        return false;
    }


    for (uint32_t i = 0; i < nested_struct->field_count; i++) {

        if (*expanded_count >= *expanded_cap) {
            *expanded_cap *= 2;
            Field* new_fields = (Field*)realloc(*expanded_fields, *expanded_cap * sizeof(Field));
            if (!new_fields) {
                parser_error(p, "Memory allocation failed");
                return false;
            }
            *expanded_fields = new_fields;
        }

        Field* src = &nested_struct->fields[i];
        Field* dst = &(*expanded_fields)[*expanded_count];


        *dst = *src;


        snprintf(dst->name, sizeof(dst->name), "%s.%s", field_name, src->name);


        dst->offset = base_offset + src->offset;

        (*expanded_count)++;
    }

    return true;
}

static bool flatten_structs(Parser* p) {
    for (uint32_t i = 0; i < p->proto->struct_count; i++) {
        StructDef* s = &p->temp_structs[i];
        Field* original_fields = s->fields;
        uint32_t original_count = s->field_count;

        uint32_t expanded_cap = original_count > 0 ? original_count * 4 : 4;
        Field* expanded_fields = (Field*)calloc(expanded_cap, sizeof(Field));
        if (!expanded_fields) {
            parser_error(p, "Memory allocation failed");
            return false;
        }

        uint32_t expanded_count = 0;
        uint32_t offset = 0;
        bool variable_seen = false;
        s->has_variable = false;

        for (uint32_t j = 0; j < original_count; j++) {
            Field* f = &original_fields[j];

            if (variable_seen) {
                parser_error(p, "Variable-length field '%s' must be last in struct '%s'",
                             f->name, s->name);
                free(expanded_fields);
                return false;
            }

            uint32_t repeat = f->is_array ? f->array_size : 1;
            if (repeat == 0) {
                parser_error(p, "Array size must be greater than 0 for field '%s' in struct '%s'",
                             f->name, s->name);
                free(expanded_fields);
                return false;
            }

            if (f->type == FIELD_TYPE_NESTED) {
                char type_name[64] = "";
                char field_name[64] = "";
                if (!parse_nested_field_names(p, f, type_name, sizeof(type_name),
                                              field_name, sizeof(field_name))) {
                    free(expanded_fields);
                    return false;
                }

                StructDef* nested = find_struct_by_name(p, type_name);
                if (!nested) {
                    parser_error(p, "Nested struct '%s' not found", type_name);
                    free(expanded_fields);
                    return false;
                }

                if (nested->has_variable && repeat > 1) {
                    parser_error(p, "Array of variable-length struct '%s' is not supported in '%s'",
                                 type_name, s->name);
                    free(expanded_fields);
                    return false;
                }

                for (uint32_t idx = 0; idx < repeat; idx++) {
                    Field nested_field = *f;
                    nested_field.is_array = false;
                    nested_field.array_size = 1;

                    if (f->is_array) {
                        snprintf(nested_field.name, sizeof(nested_field.name),
                                 "%s.%s[%u]", type_name, field_name, idx);
                    }

                    if (!expand_nested_field(p, s, &nested_field, &expanded_fields,
                                             &expanded_count, &expanded_cap,
                                             offset + idx * nested->min_size)) {
                        free(expanded_fields);
                        return false;
                    }
                }

                if (nested->has_variable) {
                    variable_seen = true;
                    s->has_variable = true;
                }

                offset += nested->min_size * repeat;
            } else {
                if (f->is_array) {
                    parser_error(p, "Arrays are only supported for nested struct fields (field '%s' in struct '%s')",
                                 f->name, s->name);
                    free(expanded_fields);
                    return false;
                }

                if (expanded_count >= expanded_cap) {
                    expanded_cap *= 2;
                    Field* new_fields = (Field*)realloc(expanded_fields, expanded_cap * sizeof(Field));
                    if (!new_fields) {
                        parser_error(p, "Memory allocation failed");
                        free(expanded_fields);
                        return false;
                    }
                    expanded_fields = new_fields;
                }

                expanded_fields[expanded_count] = *f;
                expanded_fields[expanded_count].offset = offset;
                expanded_count++;

                if (f->is_variable) {
                    variable_seen = true;
                    s->has_variable = true;
                } else {
                    offset += f->size;
                }
            }
        }

        s->min_size = offset;
        s->field_count = expanded_count;
        s->fields = expanded_fields;
        free(original_fields);
    }

    return true;
}


static const Field* find_field_by_path(Parser* p, const StructDef* s, const char* path) {
    (void)p;

    for (uint32_t i = 0; i < s->field_count; i++) {
        if (strcmp(s->fields[i].name, path) == 0) {
            return &s->fields[i];
        }
    }
    return NULL;
}


static OpCode get_load_opcode(const Field* field) {
    switch (field->type) {
        case FIELD_TYPE_UINT8:
            return OP_LOAD_U8;
        case FIELD_TYPE_UINT16:
            return field->endian == ENDIAN_BIG ? OP_LOAD_U16_BE : OP_LOAD_U16_LE;
        case FIELD_TYPE_UINT32:
            return field->endian == ENDIAN_BIG ? OP_LOAD_U32_BE : OP_LOAD_U32_LE;
        case FIELD_TYPE_UINT64:
            return field->endian == ENDIAN_BIG ? OP_LOAD_U64_BE : OP_LOAD_U64_LE;
        case FIELD_TYPE_INT8:
            return OP_LOAD_I8;
        case FIELD_TYPE_INT16:
            return field->endian == ENDIAN_BIG ? OP_LOAD_I16_BE : OP_LOAD_I16_LE;
        case FIELD_TYPE_INT32:
            return field->endian == ENDIAN_BIG ? OP_LOAD_I32_BE : OP_LOAD_I32_LE;
        case FIELD_TYPE_INT64:
            return field->endian == ENDIAN_BIG ? OP_LOAD_I64_BE : OP_LOAD_I64_LE;
        default:
            return OP_LOAD_U8;
    }
}


static OpCode swap_endian_opcode(OpCode op) {
    switch (op) {
        case OP_LOAD_U16_BE: return OP_LOAD_U16_LE;
        case OP_LOAD_U16_LE: return OP_LOAD_U16_BE;
        case OP_LOAD_U32_BE: return OP_LOAD_U32_LE;
        case OP_LOAD_U32_LE: return OP_LOAD_U32_BE;
        case OP_LOAD_U64_BE: return OP_LOAD_U64_LE;
        case OP_LOAD_U64_LE: return OP_LOAD_U64_BE;
        case OP_LOAD_I16_BE: return OP_LOAD_I16_LE;
        case OP_LOAD_I16_LE: return OP_LOAD_I16_BE;
        case OP_LOAD_I32_BE: return OP_LOAD_I32_LE;
        case OP_LOAD_I32_LE: return OP_LOAD_I32_BE;
        case OP_LOAD_I64_BE: return OP_LOAD_I64_LE;
        case OP_LOAD_I64_LE: return OP_LOAD_I64_BE;
        default:
            return op;
    }
}


static OpCode get_cmp_opcode(ConditionOp op) {
    switch (op) {
        case COND_EQ:    return OP_CMP_EQ;
        case COND_NE:    return OP_CMP_NE;
        case COND_GT:    return OP_CMP_GT;
        case COND_GE:    return OP_CMP_GE;
        case COND_LT:    return OP_CMP_LT;
        case COND_LE:    return OP_CMP_LE;
        case COND_MASK:  return OP_CMP_MASK;
        default:         return OP_CMP_EQ;
    }
}

static bool compile_filter_rules(Parser* p) {

    for (uint32_t i = 0; i < p->proto->filter_count; i++) {
        TempFilterRule* temp_rule = &p->temp_filter_rules[i];
        FilterRule* rule = &p->temp_filters[i];


        strncpy(rule->name, temp_rule->name, sizeof(rule->name) - 1);
        strncpy(rule->struct_name, temp_rule->struct_name, sizeof(rule->struct_name) - 1);
        rule->sliding_window = temp_rule->sliding_window;
        rule->sliding_max_offset = temp_rule->sliding_max_offset;


        StructDef* target_struct = NULL;
        if (temp_rule->cond_count > 0) {

            for (uint32_t j = 0; j < p->proto->struct_count; j++) {
                if (find_field_by_path(p, &p->temp_structs[j], temp_rule->conditions[0].field_name)) {
                    target_struct = &p->temp_structs[j];
                    strncpy(rule->struct_name, target_struct->name, sizeof(rule->struct_name) - 1);
                    break;
                }
            }
        }

        if (!target_struct) {
            parser_error(p, "Cannot find struct for filter '%s'", temp_rule->name);
            return false;
        }

        rule->min_packet_size = target_struct->min_size;


        uint32_t cond_count = temp_rule->cond_count;
        uint32_t* cond_sizes = NULL;
        if (cond_count > 0) {
            cond_sizes = (uint32_t*)calloc(cond_count, sizeof(uint32_t));
            if (!cond_sizes) {
                parser_error(p, "Memory allocation failed");
                return false;
            }
        }

        uint32_t total_size = 2;
        for (uint32_t j = 0; j < cond_count; j++) {
            FilterCondition* cond = &temp_rule->conditions[j];
            switch (cond->op) {
                case COND_IN:
                    if (cond->value_count == 0) {
                        if (cond_sizes) free(cond_sizes);
                        parser_error(p, "Empty IN list in filter '%s'", temp_rule->name);
                        return false;
                    }
                    cond_sizes[j] = 3 * cond->value_count;
                    total_size += cond_sizes[j];
                    break;
                case COND_NOT_IN:
                    if (cond->value_count == 0) {
                        if (cond_sizes) free(cond_sizes);
                        parser_error(p, "Empty NOT IN list in filter '%s'", temp_rule->name);
                        return false;
                    }
                    cond_sizes[j] = 1 + 3 * cond->value_count;
                    total_size += cond_sizes[j];
                    break;
                case COND_EQ:
                case COND_NE:
                case COND_GT:
                case COND_GE:
                case COND_LT:
                case COND_LE:
                case COND_MASK:
                    cond_sizes[j] = 3;
                    total_size += cond_sizes[j];
                    break;
                default:
                    if (cond_sizes) free(cond_sizes);
                    parser_error(p, "Unsupported operator in filter '%s'", temp_rule->name);
                    return false;
            }
        }


        rule->bytecode = (Instruction*)calloc(total_size, sizeof(Instruction));
        if (!rule->bytecode) {
            if (cond_sizes) free(cond_sizes);
            parser_error(p, "Memory allocation failed");
            return false;
        }


        uint32_t success_label = total_size > 1 ? total_size - 2 : 0;
        uint32_t fail_label = total_size > 0 ? total_size - 1 : 0;
        uint32_t* cond_starts = NULL;
        if (cond_count > 0) {
            cond_starts = (uint32_t*)calloc(cond_count, sizeof(uint32_t));
            if (!cond_starts) {
                free(rule->bytecode);
                if (cond_sizes) free(cond_sizes);
                parser_error(p, "Memory allocation failed");
                return false;
            }
            cond_starts[0] = 0;
            for (uint32_t j = 1; j < cond_count; j++) {
                cond_starts[j] = cond_starts[j - 1] + cond_sizes[j - 1];
            }
        }


        for (uint32_t j = 0; j < cond_count; j++) {
            FilterCondition* cond = &temp_rule->conditions[j];
            const uint32_t start = cond_starts[j];
            uint32_t idx = start;
            uint32_t next_start = (j + 1 < cond_count) ? cond_starts[j + 1] : success_label;


            const Field* field = find_field_by_path(p, target_struct, cond->field_name);
            if (!field) {
                parser_error(p, "Field '%s' not found in struct '%s'",
                             cond->field_name, target_struct->name);
                free(rule->bytecode);
                if (cond_sizes) free(cond_sizes);
                if (cond_starts) free(cond_starts);
                return false;
            }


            Instruction load_ins = (Instruction){0};
            load_ins.opcode = get_load_opcode(field);
            load_ins.offset = field->offset;
            rule->bytecode[idx++] = load_ins;

            if (cond->op == COND_IN) {
                for (uint32_t v = 0; v < cond->value_count; v++) {
                    Instruction cmp_ins = {0};
                    cmp_ins.opcode = OP_CMP_EQ;
                    cmp_ins.operand = cond->values[v];
                    rule->bytecode[idx++] = cmp_ins;

                    Instruction jif = {0};
                    jif.opcode = OP_JUMP_IF_FALSE;
                    if (v + 1 < cond->value_count) {
                        jif.jump_target = idx + 2;
                    } else {
                        jif.jump_target = fail_label;
                    }
                    rule->bytecode[idx++] = jif;

                    if (v + 1 < cond->value_count) {
                        Instruction jmp = {0};
                        jmp.opcode = OP_JUMP;
                        jmp.jump_target = next_start;
                        rule->bytecode[idx++] = jmp;
                    }
                }
            } else if (cond->op == COND_NOT_IN) {
                for (uint32_t v = 0; v < cond->value_count; v++) {
                    Instruction cmp_ins = {0};
                    cmp_ins.opcode = OP_CMP_EQ;
                    cmp_ins.operand = cond->values[v];
                    rule->bytecode[idx++] = cmp_ins;

                    Instruction jif = {0};
                    jif.opcode = OP_JUMP_IF_FALSE;
                    if (v + 1 < cond->value_count) {
                        jif.jump_target = idx + 2;
                    } else {
                        jif.jump_target = next_start;
                    }
                    rule->bytecode[idx++] = jif;

                    Instruction jmp_fail = {0};
                    jmp_fail.opcode = OP_JUMP;
                    jmp_fail.jump_target = fail_label;
                    rule->bytecode[idx++] = jmp_fail;
                }
            } else {

                Instruction cmp_ins = (Instruction){0};
                cmp_ins.opcode = get_cmp_opcode(cond->op);
                cmp_ins.operand = cond->value;
                if (cond->op == COND_MASK) {
                    cmp_ins.operand = cond->mask;
                    cmp_ins.operand2 = cond->value;
                }
                rule->bytecode[idx++] = cmp_ins;

                Instruction jump_ins = (Instruction){0};
                jump_ins.opcode = OP_JUMP_IF_FALSE;
                jump_ins.jump_target = fail_label;
                rule->bytecode[idx++] = jump_ins;
            }
        }


        if (total_size >= 2) {
            rule->bytecode[success_label].opcode = OP_RETURN_TRUE;
            rule->bytecode[fail_label].opcode = OP_RETURN_FALSE;
        }
        rule->bytecode_len = total_size;


        rule->bytecode_be = rule->bytecode;
        rule->bytecode_be_len = total_size;

        rule->bytecode_le = (Instruction*)calloc(total_size, sizeof(Instruction));
        if (rule->bytecode_le) {
            memcpy(rule->bytecode_le, rule->bytecode_be, total_size * sizeof(Instruction));
            for (uint32_t k = 0; k < total_size; k++) {
                rule->bytecode_le[k].opcode = swap_endian_opcode(rule->bytecode_le[k].opcode);
            }
            rule->bytecode_le_len = total_size;
        } else {
            rule->bytecode_le_len = 0;
        }

        if (cond_sizes) free(cond_sizes);
        if (cond_starts) free(cond_starts);
    }

    return true;
}

ProtocolDef* pdef_parse_file(const char* filename, char* error_msg, size_t error_size) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to open file: %s", filename);
        }
        return NULL;
    }


    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* source = (char*)malloc(file_size + 1);
    if (!source) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Memory allocation failed");
        }
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(source, 1, file_size, f);
    source[read_size] = '\0';
    fclose(f);

    ProtocolDef* result = pdef_parse_string(source, error_msg, error_size);
    if (result && filename) {
        strncpy(result->pdef_file_path, filename, sizeof(result->pdef_file_path) - 1);
        result->pdef_file_path[sizeof(result->pdef_file_path) - 1] = '\0';
    }
    free(source);

    return result;
}
