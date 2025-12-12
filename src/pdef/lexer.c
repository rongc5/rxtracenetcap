#include "lexer.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

void lexer_init(Lexer* lexer, const char* source) {
    lexer->source = source;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->error_msg[0] = '\0';
}

static char current_char(const Lexer* lexer) {
    return lexer->source[lexer->pos];
}

static char peek_char(const Lexer* lexer, int offset) {
    return lexer->source[lexer->pos + offset];
}

static void advance(Lexer* lexer) {
    if (current_char(lexer) == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    lexer->pos++;
}

static void skip_whitespace(Lexer* lexer) {
    while (isspace(current_char(lexer))) {
        advance(lexer);
    }
}

static void skip_comment(Lexer* lexer) {
    if (current_char(lexer) == '/' && peek_char(lexer, 1) == '/') {

        while (current_char(lexer) != '\0' && current_char(lexer) != '\n') {
            advance(lexer);
        }
    }
}

static void skip_whitespace_and_comments(Lexer* lexer) {
    while (true) {
        skip_whitespace(lexer);
        if (current_char(lexer) == '/' && peek_char(lexer, 1) == '/') {
            skip_comment(lexer);
        } else {
            break;
        }
    }
}

static bool read_identifier(Lexer* lexer, Token* token) {
    uint32_t len = 0;
    while (is_alnum(current_char(lexer)) && len < sizeof(token->text) - 1) {
        token->text[len++] = current_char(lexer);
        advance(lexer);
    }
    token->text[len] = '\0';


    if (strcmp(token->text, "uint8") == 0) {
        token->type = TOKEN_UINT8;
    } else if (strcmp(token->text, "uint16") == 0) {
        token->type = TOKEN_UINT16;
    } else if (strcmp(token->text, "uint32") == 0) {
        token->type = TOKEN_UINT32;
    } else if (strcmp(token->text, "uint64") == 0) {
        token->type = TOKEN_UINT64;
    } else if (strcmp(token->text, "int8") == 0) {
        token->type = TOKEN_INT8;
    } else if (strcmp(token->text, "int16") == 0) {
        token->type = TOKEN_INT16;
    } else if (strcmp(token->text, "int32") == 0) {
        token->type = TOKEN_INT32;
    } else if (strcmp(token->text, "int64") == 0) {
        token->type = TOKEN_INT64;
    } else if (strcmp(token->text, "bytes") == 0) {
        token->type = TOKEN_BYTES;
    } else if (strcmp(token->text, "string") == 0) {
        token->type = TOKEN_STRING_TYPE;
    } else if (strcmp(token->text, "varbytes") == 0) {
        token->type = TOKEN_VARBYTES;
    } else if (strcmp(token->text, "in") == 0) {
        token->type = TOKEN_IN;
    } else if (strcmp(token->text, "big") == 0 || strcmp(token->text, "little") == 0) {

        token->type = TOKEN_IDENTIFIER;
    } else if (strcmp(token->text, "name") == 0 || strcmp(token->text, "ports") == 0 ||
               strcmp(token->text, "endian") == 0) {

        token->type = TOKEN_IDENTIFIER;
    } else {
        token->type = TOKEN_IDENTIFIER;
    }

    return true;
}

static bool read_number(Lexer* lexer, Token* token) {
    token->type = TOKEN_NUMBER;
    token->value = 0;


    if (current_char(lexer) == '0' && (peek_char(lexer, 1) == 'x' || peek_char(lexer, 1) == 'X')) {
        advance(lexer);
        advance(lexer);

        uint32_t len = 0;
        while (is_hex_digit(current_char(lexer)) && len < sizeof(token->text) - 1) {
            token->text[len++] = current_char(lexer);
            advance(lexer);
        }
        token->text[len] = '\0';

        if (len == 0) {
            snprintf(lexer->error_msg, sizeof(lexer->error_msg),
                     "Invalid hex number at line %u", lexer->line);
            return false;
        }

        token->value = strtoull(token->text, NULL, 16);
        return true;
    }


    uint32_t len = 0;
    while (is_digit(current_char(lexer)) && len < sizeof(token->text) - 1) {
        token->text[len++] = current_char(lexer);
        advance(lexer);
    }
    token->text[len] = '\0';

    token->value = strtoull(token->text, NULL, 10);
    return true;
}

static bool read_string(Lexer* lexer, Token* token) {
    token->type = TOKEN_STRING;


    advance(lexer);

    uint32_t len = 0;
    while (current_char(lexer) != '"' && current_char(lexer) != '\0' &&
           len < sizeof(token->text) - 1) {
        if (current_char(lexer) == '\\' && peek_char(lexer, 1) == '"') {

            token->text[len++] = '"';
            advance(lexer);
            advance(lexer);
        } else {
            token->text[len++] = current_char(lexer);
            advance(lexer);
        }
    }
    token->text[len] = '\0';

    if (current_char(lexer) != '"') {
        snprintf(lexer->error_msg, sizeof(lexer->error_msg),
                 "Unterminated string at line %u", lexer->line);
        return false;
    }


    advance(lexer);
    return true;
}

bool lexer_next_token(Lexer* lexer, Token* token) {
    skip_whitespace_and_comments(lexer);


    token->line = lexer->line;
    token->column = lexer->column;
    token->text[0] = '\0';
    token->value = 0;

    char c = current_char(lexer);


    if (c == '\0') {
        token->type = TOKEN_EOF;
        return true;
    }


    if (is_alpha(c)) {
        return read_identifier(lexer, token);
    }


    if (is_digit(c)) {
        return read_number(lexer, token);
    }


    if (c == '"') {
        return read_string(lexer, token);
    }


    switch (c) {
        case '{':
            token->type = TOKEN_LBRACE;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case '}':
            token->type = TOKEN_RBRACE;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case '[':
            token->type = TOKEN_LBRACKET;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case ']':
            token->type = TOKEN_RBRACKET;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case ';':
            token->type = TOKEN_SEMICOLON;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case ',':
            token->type = TOKEN_COMMA;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case '.':
            token->type = TOKEN_DOT;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case '@':
            token->type = TOKEN_AT;
            advance(lexer);

            if (is_alpha(current_char(lexer))) {
                uint32_t len = 0;
                while (is_alnum(current_char(lexer)) && len < sizeof(token->text) - 1) {
                    token->text[len++] = current_char(lexer);
                    advance(lexer);
                }
                token->text[len] = '\0';

                if (strcmp(token->text, "protocol") == 0) {
                    token->type = TOKEN_PROTOCOL;
                } else if (strcmp(token->text, "const") == 0) {
                    token->type = TOKEN_CONST;
                } else if (strcmp(token->text, "filter") == 0) {
                    token->type = TOKEN_FILTER;
                } else {
                    snprintf(lexer->error_msg, sizeof(lexer->error_msg),
                             "Unknown keyword @%s at line %u", token->text, lexer->line);
                    token->type = TOKEN_ERROR;
                    return false;
                }
            }
            return true;

        case '=':
            if (peek_char(lexer, 1) == '=') {
                token->type = TOKEN_EQ;
                strcpy(token->text, "==");
                advance(lexer);
                advance(lexer);
            } else {
                token->type = TOKEN_ASSIGN;
                token->text[0] = c;
                token->text[1] = '\0';
                advance(lexer);
            }
            return true;

        case '!':
            if (peek_char(lexer, 1) == '=') {
                token->type = TOKEN_NE;
                strcpy(token->text, "!=");
                advance(lexer);
                advance(lexer);
                return true;
            }
            token->type = TOKEN_NOT;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case '<':
            if (peek_char(lexer, 1) == '=') {
                token->type = TOKEN_LE;
                strcpy(token->text, "<=");
                advance(lexer);
                advance(lexer);
            } else {
                token->type = TOKEN_LT;
                token->text[0] = c;
                token->text[1] = '\0';
                advance(lexer);
            }
            return true;

        case '>':
            if (peek_char(lexer, 1) == '=') {
                token->type = TOKEN_GE;
                strcpy(token->text, ">=");
                advance(lexer);
                advance(lexer);
            } else {
                token->type = TOKEN_GT;
                token->text[0] = c;
                token->text[1] = '\0';
                advance(lexer);
            }
            return true;

        case '&':
            token->type = TOKEN_AND;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;

        case '|':
            token->type = TOKEN_OR;
            token->text[0] = c;
            token->text[1] = '\0';
            advance(lexer);
            return true;
    }


    snprintf(lexer->error_msg, sizeof(lexer->error_msg),
             "Unexpected character '%c' at line %u column %u", c, lexer->line, lexer->column);
    token->type = TOKEN_ERROR;
    return false;
}

bool lexer_peek_token(Lexer* lexer, Token* token) {

    Lexer saved = *lexer;


    bool result = lexer_next_token(lexer, token);


    *lexer = saved;

    return result;
}

const char* lexer_get_error(const Lexer* lexer) {
    return lexer->error_msg;
}
