#ifndef PDEF_LEXER_H
#define PDEF_LEXER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Token Types ========== */

typedef enum {
    /* Literals */
    TOKEN_IDENTIFIER,       /* abc, Header, player_id */
    TOKEN_NUMBER,           /* 123, 0x1234, 0777 */
    TOKEN_STRING,           /* "string literal" */

    /* Keywords */
    TOKEN_PROTOCOL,         /* @protocol */
    TOKEN_CONST,            /* @const */
    TOKEN_FILTER,           /* @filter */

    /* Type keywords */
    TOKEN_UINT8,            /* uint8 */
    TOKEN_UINT16,           /* uint16 */
    TOKEN_UINT32,           /* uint32 */
    TOKEN_UINT64,           /* uint64 */
    TOKEN_INT8,             /* int8 */
    TOKEN_INT16,            /* int16 */
    TOKEN_INT32,            /* int32 */
    TOKEN_INT64,            /* int64 */
    TOKEN_BYTES,            /* bytes */
    TOKEN_STRING_TYPE,      /* string */
    TOKEN_VARBYTES,         /* varbytes */

    /* Operators and punctuation */
    TOKEN_LBRACE,           /* { */
    TOKEN_RBRACE,           /* } */
    TOKEN_LBRACKET,         /* [ */
    TOKEN_RBRACKET,         /* ] */
    TOKEN_SEMICOLON,        /* ; */
    TOKEN_COMMA,            /* , */
    TOKEN_DOT,              /* . */
    TOKEN_ASSIGN,           /* = */
    TOKEN_EQ,               /* == */
    TOKEN_NE,               /* != */
    TOKEN_LT,               /* < */
    TOKEN_LE,               /* <= */
    TOKEN_GT,               /* > */
    TOKEN_GE,               /* >= */
    TOKEN_AND,              /* & */
    TOKEN_OR,               /* | */
    TOKEN_IN,               /* in */
    TOKEN_NOT,              /* ! (unary) */
    TOKEN_AT,               /* @ */

    /* Special */
    TOKEN_EOF,              /* End of file */
    TOKEN_ERROR,            /* Lexical error */
} TokenType;

/* ========== Token Structure ========== */

typedef struct {
    TokenType   type;
    char        text[256];      /* Token text */
    uint64_t    value;          /* Numeric value (for TOKEN_NUMBER) */
    uint32_t    line;           /* Line number (1-based) */
    uint32_t    column;         /* Column number (1-based) */
} Token;

/* ========== Lexer Structure ========== */

typedef struct {
    const char* source;         /* Source code */
    uint32_t    pos;            /* Current position */
    uint32_t    line;           /* Current line number */
    uint32_t    column;         /* Current column number */
    char        error_msg[256]; /* Error message */
} Lexer;

/* ========== Lexer Functions ========== */

/**
 * Initialize a lexer with source code.
 *
 * @param lexer         Lexer instance
 * @param source        Source code string (must remain valid during lexing)
 */
void lexer_init(Lexer* lexer, const char* source);

/**
 * Get the next token from the source.
 *
 * @param lexer         Lexer instance
 * @param token         Output: next token
 * @return              true on success, false on error
 */
bool lexer_next_token(Lexer* lexer, Token* token);

/**
 * Peek at the next token without consuming it.
 *
 * @param lexer         Lexer instance
 * @param token         Output: next token
 * @return              true on success, false on error
 */
bool lexer_peek_token(Lexer* lexer, Token* token);

/**
 * Get the error message from the lexer.
 *
 * @param lexer         Lexer instance
 * @return              Error message string
 */
const char* lexer_get_error(const Lexer* lexer);

#ifdef __cplusplus
}
#endif

#endif /* PDEF_LEXER_H */
