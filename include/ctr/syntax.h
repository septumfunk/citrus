#ifndef SYNTAX_H
#define SYNTAX_H

#include <stdint.h>
#include "bytecode.h"

typedef enum {
    TK_SOF,
    TK_LEFT_PAREN, TK_RIGHT_PAREN, TK_LEFT_BRACE, TK_RIGHT_BRACE,
    TK_COMMA, TK_PERIOD, TK_MINUS, TK_PLUS, TK_SEMICOLON, TK_SLASH, TK_ASTERISK,
    TK_FAT_ARROW,

    TK_BANG, TK_NOT_EQUAL,
    TK_EQUAL, TK_DOUBLE_EQUAL,
    TK_GREATER, TK_GREATER_EQUAL,
    TK_LESS, TK_LESS_EQUAL,

    TK_IDENTIFIER, TK_STRING, TK_NUMBER, TK_INTEGER,

    TK_AND, TK_ELSE, TK_TRUE, TK_FALSE, TK_FUN, TK_FOR, TK_IF, TK_NIL, TK_OR,
    TK_RETURN, TK_LET, TK_WHILE,

    TK_EOF
} ctr_tokentype;

typedef struct {
    ctr_tokentype tt;
    ctr_val value;
    uint32_t line, column;
} ctr_token;

typedef struct {
    enum ctr_scan_errt {
        CTR_ERR_UNEXPECTED_TOKEN,
        CTR_ERR_UNTERMINATED_STR,
        CTR_ERR_NUMBER_FORMAT,
    } type;
    sf_str token;
    uint32_t line, column;
} ctr_scan_err;

struct ctr_tokenvec;
void _ctr_tokenvec_cleanup(struct ctr_tokenvec *vec);
#define VEC_NAME ctr_tokenvec
#define VEC_T ctr_token
#define CLEANUP_FN _ctr_tokenvec_cleanup
#include <sf/containers/vec.h>

struct ctr_keywords;
void _ctr_keywords_cleanup(struct ctr_keywords *vec);
#define MAP_NAME ctr_keywords
#define MAP_K sf_str
#define MAP_V ctr_tokentype
#define EQUAL_FN(s1, s2) (sf_str_eq(s1, s2))
#define HASH_FN(s) (sf_str_hash(s))
#define CLEANUP_FN _ctr_keywords_cleanup
#include <sf/containers/map.h>

#define EXPECTED_NAME ctr_scan_ex
#define EXPECTED_O ctr_tokenvec
#define EXPECTED_E ctr_scan_err
#include <sf/containers/expected.h>
EXPORT ctr_scan_ex ctr_scan(sf_str src);

typedef enum {
    ND_BINARY,
    ND_UNARY,
    ND_IDENTIFIER,
    ND_LITERAL,
    ND_LET,
    ND_ASSIGN,
    ND_CALL,
    ND_IF,
    ND_BLOCK,
    ND_RETURN,
} ctr_nodetype;

typedef struct ctr_node {
    ctr_nodetype tt;
    size_t line, column;
    union ctr_statement {
        ctr_val literal, identifier;
        struct {
            ctr_tokentype tt;
            struct ctr_node *left;
            struct ctr_node *right;
        } binary;
        struct {
            ctr_tokentype tt;
            struct ctr_node *expr;
        } unary, stmt_ret;
        struct {
            ctr_val name;
            struct ctr_node *value;
        } stmt_let, stmt_assign;
        struct {
            struct ctr_node *condition;
            struct ctr_node *then_node;
            struct ctr_node *else_node;
        } stmt_if;
        struct {
            ctr_val name;
            struct ctr_node **args;
            size_t arg_c;
        } stmt_call;
        struct {
            struct ctr_node **stmts;
            size_t count;
        } block;
    } statement;
} ctr_node;

EXPORT void ctr_node_free(ctr_node *tree);

typedef struct {
    enum {
        CTR_ERR_NO_TOKENS,
        CTR_ERR_EXPECTED_EXPRESSION,
        CTR_ERR_EXPECTED_IDENTIFIER,
        CTR_ERR_EXPECTED_LPAREN,
        CTR_ERR_EXPECTED_RPAREN,
        CTR_ERR_EXPECTED_RBRACE,
        CTR_ERR_EXPECTED_EQUAL,
        CTR_ERR_EXPECTED_SEMICOLON,
        CTR_ERR_EXPECTED_CONDITION,
        CTR_ERR_EXPECTED_BLOCK,
        CTR_ERR_EXPECTED_STMT,
        CTR_ERR_UNTERMINATED_ARGS,
        CTR_ERR_UNUSED_EVALUATION,
    } tt;
    size_t line, column;
} ctr_parse_err;

typedef ctr_node *ctr_ast;

#define EXPECTED_NAME ctr_parse_ex
#define EXPECTED_O ctr_ast
#define EXPECTED_E ctr_parse_err
#include <sf/containers/expected.h>
EXPORT ctr_parse_ex ctr_parse(ctr_tokenvec *tokens);

#endif // SYNTAX_H
