#include "ctr/bytecode.h"
#include "sf/str.h"
#include <stdio.h>
#include <ctr/vm.h>

ctr_state *ctr_state_new(void) {
    ctr_state *s = malloc(sizeof(ctr_state));
    *s = (ctr_state){
        .stack = ctr_valvec_new(),
        .protos = ctr_protovec_new(),
    };
    return s;
}

void ctr_state_free(ctr_state *state) {
    ctr_valvec_free(&state->stack);
    ctr_protovec_free(&state->protos);
    free(state);
}

sf_str ctr_tostring(ctr_val val) {
    switch (val.tt) {
        case CTR_TNIL: return sf_lit("nil");
        case CTR_TF64: return sf_str_fmt("%f", val.val.f64);
        case CTR_TI64: return sf_str_fmt("%lld", val.val.i64);
        case CTR_TDYN: {
            switch (ctr_header(val.val.dyn)->tt) {
                case CTR_DSTR: return sf_str_dup(*(sf_str *)val.val.dyn); break;
                case CTR_DOBJ:
                case CTR_DFUN: return sf_str_fmt("%p", val.val.dyn);
                default: return SF_STR_EMPTY;
            }
        }
        default: return SF_STR_EMPTY;
    }
}

sf_str ctr_stackdump(ctr_state *state) {
    sf_str out = sf_str_cdup("====STACK DUMP====\n");
    for (size_t i = 0; i < state->stack.count; ++i) {
        ctr_val val = ctr_get(state, i);
        sf_str val_s = ctr_tostring(val);
        sf_str line = sf_str_fmt(
            ctr_header(val.val.dyn)->tt == CTR_DSTR ? "[%llu]: %s = '%s'\n" :
            "[%llu]: %s = %s\n", i, ctr_typename(val).c_str, val_s.c_str
        );
        sf_str_append(&out, line);
        sf_str_free(val_s);
        sf_str_free(line);
    }
    sf_str_append(&out, sf_lit("=================="));
    return out;
}


#define CAT(a, b) a##b
#define EXPAND(a) a
#define EXPAND_CAT(a, b) CAT(a, b)
#if defined(__GNUC__) || defined(__clang__)
#   define COMPUTE_GOTOS
#   define DISPATCH() do { if (pc == proto->code_s) goto ret; ins = proto->code[pc++]; goto *computed[ctr_ins_op(ins)]; } while(0)
#   define LABEL(name) [name] = &&EXPAND_CAT(name, _L)
#   define CASE(name) EXPAND_CAT(name, _L):
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wpedantic"
#else
#   define DISPATCH() continue
#   define CASE(name) case EXPAND(name):
#endif

#define err(str) { sf_str s = (str); fprintf(stderr, "Runtime Error: %s\n", s.c_str); sf_str_free(s); return CTR_NIL; }

ctr_val ctr_run(ctr_state *state, ctr_proto *proto) {
    #ifdef COMPUTE_GOTOS
    void *computed[] = {
        LABEL(CTR_OP_LOAD),
        LABEL(CTR_OP_MOVE),
        LABEL(CTR_OP_RET),
        LABEL(CTR_OP_JMP),

        LABEL(CTR_OP_ADD),
        LABEL(CTR_OP_SUB),

        LABEL(CTR_OP_EQ),
        LABEL(CTR_OP_LT),
        LABEL(CTR_OP_LE),

        LABEL(CTR_OP_OBJ_NEW),
        LABEL(CTR_OP_OBJ_SET),
        LABEL(CTR_OP_OBJ_GET),

        LABEL(CTR_OP_STR_FROM),
        LABEL(CTR_OP_STR_ECHO),

        LABEL(CTR_OP_DBG_DUMP),

        LABEL(CTR_OP_UNKNOWN),
    };
    #endif

    ctr_instruction ins;
    size_t pc = proto->entry;
    state->stack = ctr_valvec_alloc(proto->reg_c, (ctr_val){.tt = CTR_TNIL});
    ctr_val return_val = CTR_NIL;

    #ifdef COMPUTE_GOTOS
    DISPATCH();
    #else
    while (pc < proto->code_s) {
        ins = proto->code[pc];
        switch (ctr_ins_op(ins)) {
    #endif
        CASE(CTR_OP_LOAD) {
            ctr_set(state, ctr_iab_a(ins), ctr_valvec_get(&proto->constants, ctr_iab_b(ins)));
            DISPATCH();
        }
        CASE(CTR_OP_MOVE) {
            ctr_set(state, ctr_iab_a(ins), ctr_get(state, ctr_iab_b(ins)));
            DISPATCH();
        }
        CASE(CTR_OP_RET) {
            return_val = ctr_valvec_get(&state->stack, (size_t)ctr_ia_a(ins));
            goto ret;
            DISPATCH();
        }
        CASE(CTR_OP_JMP) {
            pc = (size_t)((int32_t)pc + ctr_ia_a(ins));
            DISPATCH();
        }

        CASE(CTR_OP_ADD) {
            ctr_val lhs = ctr_get(state, ctr_iabc_b(ins));
            ctr_val rhs = ctr_get(state, ctr_iabc_c(ins));

            sf_str *_l = (sf_str *)lhs.val.dyn;
            sf_str *_r = (sf_str *)rhs.val.dyn;
            (void)_l;(void)_r;

            if (lhs.tt != rhs.tt) {
                if (lhs.tt == CTR_TDYN || rhs.tt == CTR_TDYN)
                    err(sf_lit("Cannot convert dynamic object to primitive."));
                switch (lhs.tt) {
                    case CTR_TI64: rhs = (ctr_val){.tt = CTR_TI64, .val.i64 = (ctr_i64)rhs.val.f64};
                    case CTR_TF64: rhs = (ctr_val){.tt = CTR_TF64, .val.f64 = (ctr_f64)rhs.val.i64};
                    default: err(sf_lit("???"));
                }
            }
            switch (lhs.tt) {
                case CTR_TNIL: err(sf_lit("Can't add nil.")) break;
                case CTR_TF64:
                    ctr_set(state, ctr_iabc_a(ins), (ctr_val){.tt = CTR_TF64, .val.f64 = lhs.val.f64 + rhs.val.f64});
                    break;
                case CTR_TI64:
                    ctr_set(state, ctr_iabc_a(ins), (ctr_val){.tt = CTR_TI64, .val.i64 = lhs.val.i64 + rhs.val.i64});
                    break;
                case CTR_TDYN:
                    if (ctr_header(lhs.val.dyn)->tt != CTR_DSTR || ctr_header(rhs.val.dyn)->tt != CTR_DSTR)
                        err(sf_lit("Cannot concatenate string with dynamic object."));
                    sf_str *str = ctr_dnew(sizeof(sf_str), CTR_DSTR);
                    sf_str out = sf_str_join(*(sf_str *)lhs.val.dyn, *(sf_str *)rhs.val.dyn);
                    memcpy(str, &out, sizeof(sf_str));
                    ctr_set(state, ctr_iabc_a(ins), (ctr_val){.tt = CTR_TDYN, .val.dyn = str});
                    break;
                default: break;
            }
            DISPATCH();
        }
        CASE(CTR_OP_SUB) {
            ctr_val lhs = ctr_get(state, ctr_iabc_b(ins));
            ctr_val rhs = ctr_get(state, ctr_iabc_c(ins));

            if (lhs.tt == CTR_TDYN || rhs.tt == CTR_TDYN)
                err(sf_lit("Cannot convert dynamic object to primitive."));
            if (lhs.tt != rhs.tt) {
                switch (lhs.tt) {
                    case CTR_TI64: rhs = (ctr_val){.tt = CTR_TI64, .val.i64 = (ctr_i64)rhs.val.f64};
                    case CTR_TF64: rhs = (ctr_val){.tt = CTR_TF64, .val.f64 = (ctr_f64)rhs.val.i64};
                    default: err(sf_lit("Corrupt type."));
                }
            }
            switch (lhs.tt) {
                case CTR_TF64:
                    ctr_set(state, ctr_iabc_a(ins), (ctr_val){.tt = CTR_TF64, .val.f64 = lhs.val.f64 - rhs.val.f64});
                    break;
                case CTR_TI64:
                    ctr_set(state, ctr_iabc_a(ins), (ctr_val){.tt = CTR_TI64, .val.i64 = lhs.val.i64 - rhs.val.i64});
                    break;
                default: err(sf_lit("Can't add nil.")) break;
            }
            DISPATCH();
        }

        CASE(CTR_OP_EQ) {
            bool inv = ctr_iabc_a(ins) != 0;
            ctr_val lhs = ctr_get(state, ctr_iabc_b(ins));
            ctr_val rhs = ctr_get(state, ctr_iabc_c(ins));
            if (lhs.tt == CTR_TNIL && rhs.tt == CTR_TNIL) {
                if (!inv) pc++;
                DISPATCH();
            }

            if (lhs.tt != rhs.tt) {
                if (lhs.tt == CTR_TDYN || rhs.tt == CTR_TDYN || lhs.tt == CTR_TNIL || rhs.tt == CTR_TNIL) {
                    if (inv) pc++;
                    DISPATCH();
                }
                switch (lhs.tt) {
                    case CTR_TI64: rhs = (ctr_val){.tt = CTR_TI64, .val.i64 = (ctr_i64)rhs.val.f64};
                    case CTR_TF64: rhs = (ctr_val){.tt = CTR_TF64, .val.f64 = (ctr_f64)rhs.val.i64};
                    default: err(sf_lit("Corrupt type."));
                }
            }

            bool e = false;
            switch (lhs.tt) {
                case CTR_TI64: e = lhs.val.i64 == rhs.val.i64; break;
                case CTR_TF64: e = lhs.val.f64 == rhs.val.f64; break;

                case CTR_TDYN: {
                    ctr_dheader *h1 = ctr_header(lhs.val.dyn);
                    ctr_dheader *h2 = ctr_header(lhs.val.dyn);
                    if (h1->tt != h2->tt) {
                        e = false;
                        break;
                    }
                    switch (h1->tt) {
                        case CTR_DSTR: e = sf_str_cmp(*(sf_str *)lhs.val.dyn, *(sf_str *)rhs.val.dyn); break;
                        case CTR_DOBJ: e = lhs.val.dyn == rhs.val.dyn; break;
                        case CTR_DFUN: e = *(void **)lhs.val.dyn == *(void **)rhs.val.dyn; break;
                        default: err(sf_lit("Corrupt type."));
                    }
                }
                default: break;
            }

            e = inv ? !e : e;
            if (e) pc++;
            DISPATCH();
        }
        CASE(CTR_OP_LT) {
            bool inv = ctr_iabc_a(ins) != 0;
            ctr_val lhs = ctr_get(state, ctr_iabc_b(ins));
            ctr_val rhs = ctr_get(state, ctr_iabc_c(ins));
            if (lhs.tt == CTR_TDYN || rhs.tt == CTR_TDYN || lhs.tt == CTR_TNIL || rhs.tt == CTR_TNIL) {
                if (inv) pc++;
                DISPATCH();
            }
            if (lhs.tt != rhs.tt) {
                switch (lhs.tt) {
                    case CTR_TI64: rhs = (ctr_val){.tt = CTR_TI64, .val.i64 = (ctr_i64)rhs.val.f64};
                    case CTR_TF64: rhs = (ctr_val){.tt = CTR_TF64, .val.f64 = (ctr_f64)rhs.val.i64};
                    default: break;
                }
            }
            bool e = false;
            switch (lhs.tt) {
                case CTR_TI64: e = lhs.val.i64 < rhs.val.i64; break;
                case CTR_TF64: e = lhs.val.f64 < rhs.val.f64; break;
                default: break;
            }

            e = inv ? !e : e;
            if (e) pc++;
            DISPATCH();
        }
        CASE(CTR_OP_LE) {bool inv = ctr_iabc_a(ins) != 0;
            ctr_val lhs = ctr_get(state, ctr_iabc_b(ins));
            ctr_val rhs = ctr_get(state, ctr_iabc_c(ins));
            if (lhs.tt == CTR_TNIL && rhs.tt == CTR_TNIL) {
                if (!inv) pc++;
                DISPATCH();
            }

            if (lhs.tt != rhs.tt) {
                if (lhs.tt == CTR_TDYN || rhs.tt == CTR_TDYN || lhs.tt == CTR_TNIL || rhs.tt == CTR_TNIL) {
                    if (inv) pc++;
                    DISPATCH();
                }
                switch (lhs.tt) {
                    case CTR_TI64: rhs = (ctr_val){.tt = CTR_TI64, .val.i64 = (ctr_i64)rhs.val.f64};
                    case CTR_TF64: rhs = (ctr_val){.tt = CTR_TF64, .val.f64 = (ctr_f64)rhs.val.i64};
                    default: break;
                }
            }

            bool e = false;
            switch (lhs.tt) {
                case CTR_TI64: e = lhs.val.i64 <= rhs.val.i64; break;
                case CTR_TF64: e = lhs.val.f64 <= rhs.val.f64; break;

                case CTR_TDYN: {
                    ctr_dheader *h1 = ctr_header(lhs.val.dyn);
                    ctr_dheader *h2 = ctr_header(lhs.val.dyn);
                    if (h1->tt != h2->tt) {
                        e = false;
                        break;
                    }
                    switch (h1->tt) {
                        case CTR_DSTR: e = sf_str_cmp(*(sf_str *)lhs.val.dyn, *(sf_str *)rhs.val.dyn); break;
                        case CTR_DOBJ: e = lhs.val.dyn == rhs.val.dyn; break;
                        case CTR_DFUN: e = *(void **)lhs.val.dyn == *(void **)rhs.val.dyn; break;
                        default: err(sf_lit("Corrupt type."));
                    }
                }
                default: break;
            }

            e = inv ? !e : e;
            if (e) pc++;
            DISPATCH();
        }

        CASE(CTR_OP_OBJ_NEW) {
            ctr_val v = {.tt = CTR_TDYN, .val.dyn = ctr_dnew(sizeof(ctr_dobj), CTR_DOBJ)};
            *(ctr_dobj *)v.val.dyn = ctr_dobj_new();
            ctr_set(state, (size_t)ctr_ia_a(ins), v);
            DISPATCH();
        }
        CASE(CTR_OP_OBJ_SET) {
            ctr_val obj = ctr_get(state, ctr_iabc_a(ins));
            ctr_val key = ctr_get(state, ctr_iabc_b(ins));
            ctr_val val = ctr_get(state, ctr_iabc_c(ins));
            if (obj.tt != CTR_TDYN || ctr_header(obj.val.dyn)->tt != CTR_DOBJ)
                err(sf_str_fmt("Register [%d] does not contain object.", ctr_iabc_a(ins)));
            if (key.tt != CTR_TDYN || ctr_header(obj.val.dyn)->tt != CTR_DSTR)
                err(sf_str_fmt("Register [%d] does not contain str.", ctr_iabc_a(ins)));
            if (val.tt == CTR_TDYN)
                val.val.dyn = ctr_dref(val.val.dyn);
            ctr_dobj_set((ctr_dobj *)obj.val.dyn, *(sf_str *)key.val.dyn, val);
            DISPATCH();
        }
        CASE(CTR_OP_OBJ_GET) {
            ctr_val obj = ctr_get(state, ctr_iabc_b(ins));
            ctr_val key = ctr_get(state, ctr_iabc_c(ins));
            if (obj.tt != CTR_TDYN || ctr_header(obj.val.dyn)->tt != CTR_DOBJ)
                err(sf_str_fmt("Register [%d] does not contain object.", ctr_iabc_a(ins)));
            if (key.tt != CTR_TDYN || ctr_header(obj.val.dyn)->tt != CTR_DSTR)
                err(sf_str_fmt("Register [%d] does not contain str.", ctr_iabc_a(ins)));
            ctr_dobj_ex ex = ctr_dobj_get((ctr_dobj *)obj.val.dyn, *(sf_str *)key.val.dyn);
            if (!ex.is_ok)
                err(sf_str_fmt("Object at register [%d] does not contain member \"%s\"", ctr_iabc_b(ins), ((sf_str *)key.val.dyn)->c_str));
            ctr_set(state, ctr_iabc_a(ins), ex.value.ok);
            DISPATCH();
        }

        CASE(CTR_OP_STR_FROM) {
            ctr_val in = ctr_get(state, ctr_iab_b(ins));
            sf_str *str = ctr_dnew(sizeof(sf_str), CTR_DSTR);
            *str = ctr_tostring(in);
            ctr_set(state, ctr_iabc_a(ins), (ctr_val){.tt = CTR_TDYN, .val.dyn = str});
            DISPATCH();
        }
        CASE(CTR_OP_STR_ECHO) {
            ctr_val val = ctr_get(state, (size_t)ctr_ia_a(ins));
            if (val.tt != CTR_TDYN || ctr_header(val.val.dyn)->tt != CTR_DSTR)
                err(sf_str_fmt("[%d] is not string."));
            printf("%s", ((sf_str *)val.val.dyn)->c_str);
            DISPATCH();
        }

        CASE(CTR_OP_DBG_DUMP) {
            ctr_stackdump(state);
            DISPATCH();
        }

        CASE(CTR_OP_UNKNOWN) { DISPATCH(); }
    #ifndef COMPUTE_GOTOS
        }
    }
    #endif

ret:
    if (return_val.tt == CTR_TDYN)
        return_val.val.dyn = ctr_dref(return_val.val.dyn);
    return return_val;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
