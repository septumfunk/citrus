#include "ctr/ctrc.h"
#include "ctr/bytecode.h"
#include "ctr/syntax.h"
#include "sf/str.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct ctr_localmap;
void _ctr_localmap_fe(void *_, sf_str key, uint32_t _v) { (void)_v; sf_str_free(key); }
void _ctr_localmap_cleanup(struct ctr_localmap *);
#define MAP_NAME ctr_localmap
#define MAP_K sf_str
#define MAP_V uint32_t
#define EQUAL_FN sf_str_eq
#define HASH_FN sf_str_hash
#define CLEANUP_FN _ctr_localmap_cleanup
#include <sf/containers/map.h>
void _ctr_localmap_cleanup(ctr_localmap *map) { ctr_localmap_foreach(map, _ctr_localmap_fe, NULL); }

typedef struct {
    ctr_proto proto;
    ctr_ast ast;
    ctr_localmap lmap;
    uint32_t locals, temps, max_temps;
} ctr_compiler;

#define EXPECTED_NAME ctr_cnode_ex
#define EXPECTED_E ctr_compile_err
#include <sf/containers/expected.h>

static inline void ctr_cemit(ctr_compiler *c, ctr_instruction ins) {
    c->proto.code = realloc(c->proto.code, ++c->proto.code_s);
    c->proto.code[c->proto.code_s - 1] = ins;
}

static inline uint32_t ctr_temp(ctr_compiler *c) {
    ++c->temps;
    c->max_temps = c->temps > c->max_temps ? c->temps : c->max_temps;
    return c->locals + c->temps - 1;
}
static inline void ctr_ctemps(ctr_compiler *c, uint32_t count) { c->temps -= count; }

bool ctr_kexists(ctr_compiler *c, ctr_val con, uint32_t *idx) {
    for (uint32_t i = 0; i < c->proto.constants.count; ++i) {
        ctr_val v = c->proto.constants.data[i];
        if (v.tt != con.tt) continue;
        switch (v.tt) {
            case CTR_TNIL: *idx = i; return true;
            case CTR_TF64: if (v.val.f64 == con.val.f64) { *idx = i; return true; } else continue;
            case CTR_TI64: if (v.val.i64 == con.val.i64) { *idx = i; return true; } else continue;
            case CTR_TDYN: if (v.val.dyn == con.val.dyn) { *idx = i; return true; } else continue;
            default: continue;
        }
    }
    return false;
}

/// Passing UINT_MAX as t_reg acts as a discard.
ctr_cnode_ex ctr_cnode(ctr_compiler *c, ctr_node *node, uint32_t t_reg) {
    switch (node->tt) {
        case CTR_ND_BINARY: {
            if (t_reg == UINT_MAX)
                return ctr_cnode_ex_err((ctr_compile_err){CTR_ERRC_UNUSED_EVALUATION, {0}, node->line, node->column});

            uint32_t left = ctr_temp(c), right = ctr_temp(c);
            ctr_cnode_ex left_ex = ctr_cnode(c, node->inner.binary.left, left);
            if (!left_ex.is_ok)
                return left_ex;

            ctr_cnode_ex right_ex = ctr_cnode(c, node->inner.binary.right, right);
            if (!right_ex.is_ok) return right_ex;

            switch (node->inner.binary.tt) {
                case TK_PLUS: ctr_cemit(c, ctr_ins_abc(CTR_OP_ADD, t_reg, left, right)); break;
                case TK_MINUS: ctr_cemit(c, ctr_ins_abc(CTR_OP_SUB, t_reg, left, right)); break;
                case TK_ASTERISK: ctr_cemit(c, ctr_ins_abc(CTR_OP_MUL, t_reg, left, right)); break;
                case TK_SLASH: ctr_cemit(c, ctr_ins_abc(CTR_OP_DIV, t_reg, left, right)); break;

                case TK_DOUBLE_EQUAL: ctr_cemit(c, ctr_ins_abc(CTR_OP_EQ, 0, left, right)); break;
                case TK_LESS: ctr_cemit(c, ctr_ins_abc(CTR_OP_LT, 0, left, right)); break;
                case TK_LESS_EQUAL: ctr_cemit(c, ctr_ins_abc(CTR_OP_LE, 0, left, right)); break;

                default:
                    return ctr_cnode_ex_err((ctr_compile_err){CTR_ERRC_UNKNOWN_OPERATION, {0}, node->line, node->column});
            }

            ctr_ctemps(c, 2);
            return ctr_cnode_ex_ok();
        }
        case CTR_ND_IDENTIFIER: {
            if (t_reg == UINT_MAX)
                return ctr_cnode_ex_err((ctr_compile_err){CTR_ERRC_UNUSED_EVALUATION, {0}, node->line, node->column});
            ctr_localmap_ex lex = ctr_localmap_get(&c->lmap, *(sf_str *)node->inner.identifier.val.dyn);
            if (!lex.is_ok)
                return ctr_cnode_ex_err((ctr_compile_err){CTR_ERRC_UNKNOWN_LOCAL, {0}, node->line, node->column});
            ctr_cemit(c, ctr_ins_ab(CTR_OP_MOVE, t_reg, lex.value.ok));
            return ctr_cnode_ex_ok();
        }
        case CTR_ND_LITERAL: {
            if (t_reg == UINT_MAX)
                return ctr_cnode_ex_err((ctr_compile_err){CTR_ERRC_UNUSED_EVALUATION, {0}, node->line, node->column});
            uint32_t pos;
            if (!ctr_kexists(c, node->inner.literal, &pos)) {
                ctr_valvec_push(&c->proto.constants, ctr_dref(node->inner.literal));
                pos = c->proto.constants.count - 1;
            }
            ctr_cemit(c, ctr_ins_ab(CTR_OP_LOAD, t_reg, pos));
            return ctr_cnode_ex_ok();
        }

        case CTR_ND_LET: {
            uint8_t r = (uint8_t)c->locals++;
            ctr_cnode_ex rv_ex = ctr_cnode(c, node->inner.stmt_let.value, r); // temp
            if (!rv_ex.is_ok) return rv_ex;
            ctr_localmap_set(&c->lmap, sf_str_dup(*(sf_str *)node->inner.stmt_let.name.val.dyn), r);
            if (ctr_niscondition(node->inner.stmt_let.value)) { // Conditions
                ctr_cemit(c, ctr_ins_ab(CTR_OP_LOAD, r, 0));
                ctr_cemit(c, ctr_ins_ab(CTR_OP_LOAD, r, 1));
            }
            return ctr_cnode_ex_ok();
        }
        case CTR_ND_ASSIGN: {
            uint32_t rhs = ctr_temp(c);
            ctr_cnode_ex rv_ex = ctr_cnode(c, node->inner.stmt_let.value, rhs);
            if (!rv_ex.is_ok) return rv_ex;
            ctr_localmap_ex lex = ctr_localmap_get(&c->lmap, *(sf_str *)node->inner.stmt_assign.name.val.dyn);
            if (!lex.is_ok)
                return ctr_cnode_ex_err((ctr_compile_err){CTR_ERRC_UNKNOWN_LOCAL, {0}, node->line, node->column});
            ctr_cemit(c, ctr_ins_ab(CTR_OP_MOVE, lex.value.ok, rhs));
            if (ctr_niscondition(node->inner.stmt_let.value)) { // Conditions
                ctr_cemit(c, ctr_ins_ab(CTR_OP_LOAD, lex.value.ok, 0));
                ctr_cemit(c, ctr_ins_ab(CTR_OP_LOAD, lex.value.ok, 1));
            }
            ctr_ctemps(c, 1); // temp
            return ctr_cnode_ex_ok();
        }
        case CTR_ND_CALL: {
            ctr_localmap_ex lex = ctr_localmap_get(&c->lmap, *(sf_str *)node->inner.stmt_call.name.val.dyn);
            if (!lex.is_ok)
                return ctr_cnode_ex_err((ctr_compile_err){CTR_ERRC_UNKNOWN_LOCAL, {0}, node->line, node->column});

            uint32_t arg_rs = UINT32_MAX;
            uint8_t o_temp = (uint8_t)ctr_temp(c);
            for (size_t i = 0; i < node->inner.stmt_call.arg_c; ++i) {
                uint32_t r = ctr_temp(c);
                ctr_cnode_ex ex = ctr_cnode(c, node->inner.stmt_call.args[i], r);
                if (!ex.is_ok) return ex;
                if (arg_rs == UINT32_MAX) arg_rs = r; // first
            }
            ctr_cemit(c, ctr_ins_abc(CTR_OP_CALL, o_temp, lex.value.ok, arg_rs == UINT32_MAX ? 0 : arg_rs));
            ctr_ctemps(c, node->inner.stmt_call.arg_c);
            return ctr_cnode_ex_ok();
        }
        case CTR_ND_IF: {
            uint32_t cr = ctr_temp(c);
            ctr_cnode_ex reg = ctr_cnode(c, node->inner.stmt_if.condition, cr);
            if (!reg.is_ok) return reg;

            uint32_t jmp_false = c->proto.code_s;
            ctr_cemit(c, ctr_ins_a(CTR_OP_JMP, 0));

            // Then
            reg = ctr_cnode(c, node->inner.stmt_if.then_node, UINT_MAX);
            c->proto.code[jmp_false] = ctr_ins_a(CTR_OP_JMP, c->proto.code_s - jmp_false);

            if (node->inner.stmt_if.else_node) {
                uint32_t jmp_end = c->proto.code_s;
                ctr_cemit(c, ctr_ins_a(CTR_OP_JMP, 0));
                reg = ctr_cnode(c, node->inner.stmt_if.else_node, UINT_MAX);
                c->proto.code[jmp_end] = ctr_ins_a(CTR_OP_JMP, c->proto.code_s - jmp_end);
            }

            ctr_ctemps(c, 1);
            return ctr_cnode_ex_ok();
        }
        case CTR_ND_BLOCK:
            for (size_t i = 0; i < node->inner.block.count; ++i) {
                ctr_cnode_ex ex = ctr_cnode(c, node->inner.block.stmts[i], UINT_MAX);
                if (!ex.is_ok) return ex;
            }
            return ctr_cnode_ex_ok();
        case CTR_ND_RETURN: {
            uint32_t r = ctr_temp(c);
            ctr_cnode_ex ex = ctr_cnode(c, node->inner.stmt_ret, r);
            if (!ex.is_ok) return ex;
            ctr_cemit(c, ctr_ins_a(CTR_OP_RET, r));
        }
        default: return ctr_cnode_ex_err((ctr_compile_err){CTR_ERRC_UNKNOWN, {0}, node->line, node->column});
    }
}

EXPORT ctr_compile_ex ctr_cproto(sf_str src) {
    ctr_scan_ex scan_ex = ctr_scan(src);
    if (!scan_ex.is_ok)
        return ctr_compile_ex_err((ctr_compile_err){
            .tt = CTR_ERRC_SCAN_ERR,
            .inner.scan_err = scan_ex.value.err.tt,
            .line = scan_ex.value.err.line,
            .column = scan_ex.value.err.column,
        });
    ctr_parse_ex par_ex = ctr_parse(&scan_ex.value.ok);
    if (!par_ex.is_ok)
        return ctr_compile_ex_err((ctr_compile_err){
            .tt = CTR_ERRC_PARSE_ERR,
            .inner.parse_err = par_ex.value.err.tt,
            .line = par_ex.value.err.line,
            .column = par_ex.value.err.column,
        });
    ctr_ast ast = par_ex.value.ok;

    if (ast->tt != CTR_ND_BLOCK)
        return ctr_compile_ex_err((ctr_compile_err){CTR_ERRC_EXPECTED_BLOCK, {0}, 0, 0});

    ctr_compiler c = {
        .proto = ctr_proto_new(),
        .ast = ast,
        .lmap = ctr_localmap_new(),
        .locals = 0,
        .temps = 0, .max_temps = 0,
    };
    ctr_valvec_push(&c.proto.constants, (ctr_val){.tt = CTR_TI64, .val.i64 = 0});
    ctr_valvec_push(&c.proto.constants, (ctr_val){.tt = CTR_TI64, .val.i64 = 1});

    ctr_cnode_ex e = ctr_cnode(&c, c.ast, UINT_MAX);
    c.proto.reg_c = c.locals + c.max_temps;
    return e.is_ok ? ctr_compile_ex_ok(c.proto) : ctr_compile_ex_err(e.value.err);
}
