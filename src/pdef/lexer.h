#ifndef PDEF_LEXER_H
#define PDEF_LEXER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif



typedef enum {

    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,


    TOKEN_PROTOCOL,
    TOKEN_CONST,
    TOKEN_FILTER,


    TOKEN_UINT8,
    TOKEN_UINT16,
    TOKEN_UINT32,
    TOKEN_UINT64,
    TOKEN_INT8,
    TOKEN_INT16,
    TOKEN_INT32,
    TOKEN_INT64,
    TOKEN_BYTES,
    TOKEN_STRING_TYPE,
    TOKEN_VARBYTES,


    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_ASSIGN,
    TOKEN_EQ,
    TOKEN_NE,
    TOKEN_LT,
    TOKEN_LE,
    TOKEN_GT,
    TOKEN_GE,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_IN,
    TOKEN_NOT,
    TOKEN_AT,


    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;



typedef struct {
    TokenType   type;
    char        text[256];
    uint64_t    value;
    uint32_t    line;
    uint32_t    column;
} Token;



typedef struct {
    const char* source;
    uint32_t    pos;
    uint32_t    line;
    uint32_t    column;
    char        error_msg[256];
} Lexer;









void lexer_init(Lexer* lexer, const char* source);








bool lexer_next_token(Lexer* lexer, Token* token);








bool lexer_peek_token(Lexer* lexer, Token* token);







const char* lexer_get_error(const Lexer* lexer);

#ifdef __cplusplus
}
#endif

#endif
