#include "ctr/syntax.h"
#include "ctr/bytecode.h"
#include "sf/str.h"
#include <stdlib.h>

void _ctr_tokenvec_cleanup(ctr_tokenvec *vec) {
    for (ctr_token *t = vec->data; t && t < vec->top + 1; ++t) {
        if (t->tt == TK_STRING || t->tt == TK_IDENTIFIER)
            ctr_ddel(t->value);
    }
}

void _keywords_foreach(void *_u, sf_str k, ctr_tokentype _v) { (void)_u;(void)_v; sf_str_free(k); }
void _ctr_keywords_cleanup(ctr_keywords *map) {
    ctr_keywords_foreach(map, _keywords_foreach, NULL);
}

typedef struct {
    sf_str src;
    ctr_token current;
    size_t cc;
    ctr_keywords keywords;
} ctr_scanner;

#define ctr_scancase(_c, _tt) case _c: s.current.tt = _tt; break
static inline bool ctr_scanpeek(ctr_scanner *s, char match) {
    if (s->cc + 1 >= s->src.len || s->src.c_str[s->cc+1] != match)
        return false;
    ++s->cc;
    ++s->current.column;
    return true;
}

static inline bool ctr_isnumber(char c) { return c >= '0' && c <= '9'; }
static inline bool ctr_isalphan(char c) { return (c >= 'A' && c <= 'Z')|| (c >= 'a' && c <= 'z') || c == '_' || ctr_isnumber(c); }

ctr_token ctr_scanstr(ctr_scanner *s) {
    bool terminated = false;
    size_t len = 0;
    for (size_t cc = s->cc + 1; cc < s->src.len; ++cc) {
        char c = s->src.c_str[cc];
        if (c == '"') {
            terminated = true;
            break;
        }
        ++len;
    }
    if (!terminated)
        return (ctr_token){TK_NIL, CTR_NIL, 0, 0};

    char *str = calloc(1, len + 1);
    memcpy(str, s->src.c_str + s->cc + 1, len);
    s->cc += len + 1;
    s->current.column += len + 1;
    ctr_token tok = {
        TK_STRING,
        ctr_dnew(CTR_DSTR),
        s->current.line,
        s->current.column,
    };
    *(sf_str *)tok.value.val.dyn = sf_own(str);

    return tok;
}

ctr_token ctr_scannum(ctr_scanner *s) {
    bool is_number = false;
    size_t len = 1;
    for (size_t cc = s->cc + 1; cc < s->src.len; ++cc) {
        if (s->src.c_str[cc] == '.') {
            if (is_number)
                return (ctr_token){TK_NIL, CTR_NIL, 0, 0};
            is_number = true;
        } else if (!ctr_isnumber(s->src.c_str[cc]))
            break;
        ++len;
    }

    char str[len + 1];
    memset(str, 0, sizeof(str));
    memcpy(str, s->src.c_str + s->cc, len);
    s->cc += len - 1;
    s->current.column += len;

    ctr_token tok;
    if (is_number)
        tok = (ctr_token) {
            .tt = TK_NUMBER,
            .value = (ctr_val){.val.f64 = atof(str), .tt = CTR_TF64},
            .line = s->current.line,
            .column = s->current.column,
        };
    else
        tok = (ctr_token) {
            .tt = TK_INTEGER,
            .value = (ctr_val){.val.i64 = atoll(str), .tt = CTR_TF64},
            .line = s->current.line,
            .column = s->current.column,
        };
    return tok;
}

ctr_token ctr_scanidentifier(ctr_scanner *s) {
    size_t len = 1;
    for (size_t cc = s->cc + 1; cc < s->src.len; ++cc) {
        if (!ctr_isalphan(s->src.c_str[cc]))
            break;
        ++len;
    }

    char str[len + 1];
    memset(str, 0, sizeof(str));
    memcpy(str, s->src.c_str + s->cc, len);

    s->cc += len - 1;
    s->current.column += len - 1;

    ctr_keywords_ex ex = ctr_keywords_get(&s->keywords, sf_ref(str));
    if (ex.is_ok) return (ctr_token) {
            .tt = ex.value.ok,
            .value = CTR_NIL,
            .line = s->current.line,
            .column = s->current.line,
        };
    else {
        char *s2 = calloc(1, len + 1);
        memcpy(s2, str, len);
        ctr_token tok = {
            TK_IDENTIFIER,
            ctr_dnew(CTR_DSTR),
            s->current.line,
            s->current.column,
        };
        *(sf_str *)tok.value.val.dyn = sf_own(s2);
        return tok;
    }
}

ctr_scan_ex ctr_scan(sf_str src) {
    ctr_tokenvec tks = ctr_tokenvec_new();
    ctr_scanner s = {
        .src = src,
        .current = {TK_EOF, CTR_NIL, 1, 1},
        .cc = 0,
        .keywords = ctr_keywords_new(),
    };
    enum ctr_scan_errt eval = CTR_ERR_UNEXPECTED_TOKEN;

    ctr_keywords_set(&s.keywords, sf_lit("and"), TK_AND);
    ctr_keywords_set(&s.keywords, sf_lit("or"), TK_OR);
    ctr_keywords_set(&s.keywords, sf_lit("return"), TK_RETURN);
    ctr_keywords_set(&s.keywords, sf_lit("if"), TK_IF);
    ctr_keywords_set(&s.keywords, sf_lit("else"), TK_ELSE);
    ctr_keywords_set(&s.keywords, sf_lit("nil"), TK_NIL);
    ctr_keywords_set(&s.keywords, sf_lit("let"), TK_LET);
    ctr_keywords_set(&s.keywords, sf_lit("for"), TK_FOR);
    ctr_keywords_set(&s.keywords, sf_lit("while"), TK_WHILE);
    ctr_keywords_set(&s.keywords, sf_lit("true"), TK_TRUE);
    ctr_keywords_set(&s.keywords, sf_lit("false"), TK_FALSE);

    for (; s.cc < src.len; ++s.cc, ++s.current.column) {
        s.current = (ctr_token){TK_EOF, CTR_NIL, s.current.line, s.current.column};
        char c = src.c_str[s.cc];
        size_t pcc = s.cc;
        switch (c) {
            ctr_scancase('(', TK_LEFT_PAREN);
            ctr_scancase(')', TK_RIGHT_PAREN);
            ctr_scancase('{', TK_LEFT_BRACE);
            ctr_scancase('}', TK_RIGHT_BRACE);
            ctr_scancase(',', TK_COMMA);
            ctr_scancase('.', TK_PERIOD);
            ctr_scancase('-', TK_MINUS);
            ctr_scancase('+', TK_PLUS);
            ctr_scancase(';', TK_SEMICOLON);
            ctr_scancase('*', TK_ASTERISK);
            ctr_scancase('!', ctr_scanpeek(&s, '=') ? TK_NOT_EQUAL : TK_BANG);
            ctr_scancase('<', ctr_scanpeek(&s, '=') ? TK_LESS_EQUAL : TK_LESS);
            ctr_scancase('>', ctr_scanpeek(&s, '=') ? TK_GREATER_EQUAL : TK_GREATER);

            case '=': {
                if (ctr_scanpeek(&s, '=')) { s.current.tt = TK_DOUBLE_EQUAL; break; }
                if (ctr_scanpeek(&s, '>')) { s.current.tt = TK_FAT_ARROW; break; }
                s.current.tt = TK_EQUAL;
                break;
            }
            case '&': {
                if (ctr_scanpeek(&s, '&')) { s.current.tt = TK_AND; break; }
                goto err;
            }
            case '|': {
                if (ctr_scanpeek(&s, '|')) { s.current.tt = TK_OR; break; }
                goto err;
            }
            case '/': {
                if (ctr_scanpeek(&s, '/')) {
                    for (; s.cc < src.len && src.c_str[s.cc] != '\n'; ++s.cc){};
                    continue;
                }
                s.current.tt = TK_SLASH;
                break;
            }

            case '\n': {
                ++s.current.line;
                s.current.column = 0;
                continue;
            }
            case ' ': case '\r': case '\t': continue;
            case '"': {
                s.current = ctr_scanstr(&s);
                if (s.current.tt != TK_STRING) {
                    eval = CTR_ERR_UNTERMINATED_STR;
                    goto err;
                }
                ctr_tokenvec_push(&tks, s.current);
                continue;
            }

            default:
                if (ctr_isnumber(c)) { // Number
                    s.current = ctr_scannum(&s);
                    if (s.current.tt != TK_NUMBER && s.current.tt != TK_INTEGER) {
                        eval = CTR_ERR_NUMBER_FORMAT;
                        goto err;
                    }
                    ctr_tokenvec_push(&tks, s.current);
                    continue;
                } else if (ctr_isalphan(c)) { // Identifier
                    s.current = ctr_scanidentifier(&s);
                    ctr_tokenvec_push(&tks, s.current);
                    continue;
                }
            err: {
                ctr_tokenvec_free(&tks);
                ctr_keywords_free(&s.keywords);
                size_t tk_len = s.cc - pcc + 1;
                for (size_t cc2 = s.cc; cc2 < s.src.len; ++cc2) {
                    char c = s.src.c_str[cc2];
                    if (c == ' ' || c == '\n' || c == '\r' || c == '\n')
                        break;
                    ++tk_len;
                }
                char *str = calloc(1, tk_len + 1);
                memcpy(str, s.src.c_str + pcc, tk_len);
                return ctr_scan_ex_err((ctr_scan_err){eval, sf_own(str), s.current.line, s.current.column});
            }
        }
        ctr_tokenvec_push(&tks, s.current);
    }

    ctr_keywords_free(&s.keywords);
    ctr_tokenvec_push(&tks, (ctr_token){TK_EOF, CTR_NIL, s.current.line, s.current.column});
    return ctr_scan_ex_ok(tks);
}
