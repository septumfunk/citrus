#include "ctr/bytecode.h"
#include "sf/str.h"
#include <stdlib.h>

#define MAP_NAME ctr_pp
#define MAP_K sf_str
#define MAP_V sf_str
#define EQUAL_FN sf_str_eq
#define HASH_FN sf_str_hash
#include <sf/containers/map.h>
#define MAP_NAME ctr_cmap
#define MAP_K sf_str
#define MAP_V ctr_i64
#define EQUAL_FN sf_str_eq
#define HASH_FN sf_str_hash
#include <sf/containers/map.h>
#define MAP_NAME ctr_opmap
#define MAP_K sf_str
#define MAP_V const ctr_inssig *
#define EQUAL_FN sf_str_eq
#define HASH_FN sf_str_hash
#include <sf/containers/map.h>

ctr_proto ctr_proto_new(void) {
    return (ctr_proto){
        .code = NULL,
        .code_s = 0,
        .entry = 0,
        .constants = ctr_valvec_new(),
        .reg_c = 0,
    };
}

void ctr_proto_free(ctr_proto *proto) {
    if (proto->code)
        free(proto->code);
    proto->code = NULL;
    ctr_valvec_free(&proto->constants);
    proto->reg_c = 0;
}


ctr_asm_ex ctr_assemble(const sf_str code) {
    ctr_asm_err err;
    ctr_pp ppt = ctr_pp_new();
    ctr_cmap consts = ctr_cmap_new();
    ctr_opmap ops = ctr_opmap_new();
    for (int i = 0; i < CTR_OP_UNKNOWN; ++i)
        ctr_opmap_set(&ops, sf_ref(ctr_op_info(i)->mnemonic), ctr_op_info(i));
    ctr_i64 const_c = 0;
    ctr_proto proto = ctr_proto_new();

    sf_str lb_removed = sf_str_dup(code);
    for (char *c = lb_removed.c_str; c < lb_removed.c_str + lb_removed.len; ++c)
        *c = *c == '\r' ? ' ' : *c;
    sf_str cm_removed = SF_STR_EMPTY;
    sf_str preprocessed = SF_STR_EMPTY;
    for (char *line = strtok(lb_removed.c_str, "\n"); line; line = strtok(NULL, "\n")) {
        if (!line) {
            err = (ctr_asm_err){CTR_ERR_UNEXPECTED_EOF, SF_STR_EMPTY};
            goto err;
        }

        for (char *c = line; c < line + strlen(line); ++c) {
            if (*c == ';')
                break;
            char ap[2] = { *c, '\0' };
            sf_str_append(&cm_removed, sf_ref(ap));
        }
        sf_str_append(&cm_removed, sf_lit(" "));
        continue;
    }
    sf_str_free(lb_removed);

    // Escape
    for (char *c = cm_removed.c_str; c < cm_removed.c_str + cm_removed.len; ++c) {
        if (*c == '\\') {
            if (*(c + 1) == 'n') { // Line Break
                *c = '\n';
                memmove(c + 1, c + 2, strlen(c) - 1);
            }
        }
    }

    // Preprocess
    for (char *token = strtok(cm_removed.c_str, " "); token; token = strtok(NULL, " ")) {
        if (!token) {
            err = (ctr_asm_err){CTR_ERR_UNEXPECTED_EOF, SF_STR_EMPTY};
            goto err;
        }

        if (strcmp(token, "#define") == 0) {
            char *k = strtok(NULL, " ");
            char *v = strtok(NULL, "\n");
            if (!k || !v) {
                err = (ctr_asm_err){CTR_ERR_UNEXPECTED_EOF, SF_STR_EMPTY};
                goto err;
            }

            sf_str key = sf_str_cdup(k);
            sf_str value = sf_str_cdup(v);
            ctr_pp_set(&ppt, key, value);
            continue;
        }

        ctr_pp_ex repl = ctr_pp_get(&ppt, sf_ref(token));
        if (repl.is_ok) {
            sf_str_append(&preprocessed, repl.value.ok);
            continue;
        }
        sf_str_append(&preprocessed, sf_ref(token));
        sf_str_append(&preprocessed, sf_lit(" "));
    }
    sf_str_free(cm_removed);

    // Assemble
    sf_str string_work = SF_STR_EMPTY;
    char *sw_name = NULL;

    int register_count = -1;

    for (char *token = strtok(preprocessed.c_str, " "); token; token = strtok(NULL, " ")) {
        if (!token) {
            err = (ctr_asm_err){CTR_ERR_UNEXPECTED_EOF, SF_STR_EMPTY};
            goto err;
        }
        if (sw_name)
            goto string_continue;

        if (strcmp(token, "#alloc") == 0) {
            char *regs = strtok(NULL, " ");
            if (!regs) {
                err = (ctr_asm_err){CTR_ERR_UNEXPECTED_EOF, SF_STR_EMPTY};
                goto err;
            }
            register_count = atoi(regs);
            continue;
        }

        if (strcmp(token, "#const") == 0) {
            char *name = strtok(NULL, " ");
            char *type = strtok(NULL, " ");

            if (!name || !type) {
                err = (ctr_asm_err){CTR_ERR_UNEXPECTED_EOF, SF_STR_EMPTY};
                goto err;
            }

            ctr_type tt;
            if (sf_str_eq(sf_ref(type), sf_lit("f64")))
                tt = CTR_TF64;
            else if (sf_str_eq(sf_ref(type), sf_lit("i64")))
                tt = CTR_TI64;
            else if (sf_str_eq(sf_ref(type), sf_lit("str")))
                tt = CTR_TDYN; // str is the only dyn type available for consts.
            else {
                err = (ctr_asm_err){CTR_ERR_UNKNOWN_TYPE, sf_str_fmt("Compile Error: Unknown Type '%s'\n", type)};
                goto err;
            }

            char *value = strtok(NULL, " ");
            if (!value) {
                err = (ctr_asm_err){CTR_ERR_UNEXPECTED_EOF, SF_STR_EMPTY};
                goto err;
            }

            ctr_val v = {.tt = tt};
            switch (v.tt) {
                case CTR_TF64: v.val.f64 = (ctr_f64)atof(value); break;
                case CTR_TI64: v.val.i64 = (ctr_i64)atoll(value); break;
                case CTR_TDYN:
                string_continue: {
                    char *tk = token;
                    if (!sw_name) {
                        tk = value;
                        if (tk[0] != '"') {
                            err = (ctr_asm_err){CTR_ERR_STRING_FORMAT, SF_STR_EMPTY};
                            goto err;
                        }
                        sw_name = name;
                        if (strlen(tk) == 1)
                            continue;
                    }

                    if (tk[0] == '"') {
                        tk = tk + 1;
                    } else sf_str_append(&string_work, sf_lit(" "));

                    if (tk[strlen(tk) - 1] == '"') {
                        tk[strlen(tk) - 1] = '\0';

                        __lsan_disable(); /// String stored safely in the constant table.
                        sf_str_append(&string_work, sf_ref(tk));
                        sf_str *s = ctr_dnew(sizeof(sf_str), CTR_DSTR);
                        ctr_header(s)->is_const = true;
                        *s = string_work;
                        ctr_cmap_set(&consts, sf_str_cdup(sw_name), (ctr_i64)const_c++);
                        ctr_valvec_push(&proto.constants, (ctr_val){.tt = CTR_TDYN, .val.dyn = s});
                        __lsan_enable();

                        string_work = SF_STR_EMPTY;
                        sw_name = NULL;
                        continue;
                    }
                    sf_str_append(&string_work, sf_ref(tk));
                    break;
                }
                default:
                    return ctr_asm_ex_err((ctr_asm_err){CTR_ERR_UNKNOWN_TYPE, sf_str_fmt("Compile Error: Unknown Type '%s'\n", type)});
            }

            if (!string_work.c_str) {
                ctr_cmap_set(&consts, sf_str_cdup(name), (ctr_i64)const_c++);
                ctr_valvec_push(&proto.constants, v);
            }
            continue;
        }

        if (*(token + strlen(token) - 1) == ':') { // Label
            ctr_cmap_set(&consts, sf_str_cdup(token), (ctr_i64)proto.code_s);
            continue;
        }

        const ctr_opmap_ex ir = ctr_opmap_get(&ops, sf_ref(token));
        if (!ir.is_ok) {
            err = (ctr_asm_err){CTR_ERR_UNKNOWN_OP, sf_str_fmt("Compile Error: Unknown Opcode '%s'\n", token)};
            goto err;
        }
        const ctr_inssig *inss = ir.value.ok;

        proto.code = realloc(proto.code, ++proto.code_s * sizeof(ctr_instruction));
        ctr_instruction instr;
        ctr_i64 arg[3] = {0, 0, 0};
        for (uint64_t i = 0; i < inss->type + 1; ++i) {
            char *value = strtok(NULL, " ");
            if (!value) {
                err = (ctr_asm_err){CTR_ERR_UNEXPECTED_EOF, SF_STR_EMPTY};
                goto err;
            }
            ctr_cmap_ex idr = ctr_cmap_get(&consts, sf_ref(value));
            if (idr.is_ok && *(value + strlen(value) - 1) == ':')
                arg[i] = idr.value.ok - (uint32_t)proto.code_s;
            else arg[i] = idr.is_ok ? idr.value.ok : atoll(value);
        }
        switch (inss->type) {
            case CTR_INS_A: instr = ctr_ins_a(inss->opcode, arg[0]); break;
            case CTR_INS_AB: instr = ctr_ins_ab(inss->opcode, (uint8_t)arg[0], (uint32_t)arg[1]); break;
            case CTR_INS_ABC: instr = ctr_ins_abc(inss->opcode, (uint8_t)arg[0], (uint16_t)arg[1], (uint16_t)arg[2]); break;
        }
        proto.code[proto.code_s - 1] = instr;
    }

    if (string_work.c_str) {
        err = (ctr_asm_err){CTR_ERR_STRING_FORMAT, SF_STR_EMPTY};
        goto err;
    }

    if (register_count < 0){
        err = (ctr_asm_err){CTR_ERR_STRING_FORMAT, SF_STR_EMPTY};
        goto err;
    }
    proto.reg_c = (uint32_t)register_count;

    // Entry
    ctr_cmap_ex main = ctr_cmap_get(&consts, sf_lit("main:"));
    proto.entry = main.is_ok ? (size_t)main.value.ok : 0;

    sf_str_free(preprocessed);
    ctr_opmap_free(&ops);
    ctr_cmap_free(&consts);
    ctr_pp_free(&ppt);
    return ctr_asm_ex_ok(proto);
err:
    sf_str_free(preprocessed);
    ctr_proto_free(&proto);
    ctr_opmap_free(&ops);
    ctr_cmap_free(&consts);
    ctr_pp_free(&ppt);
    return ctr_asm_ex_err(err);
}

const ctr_inssig CTR_OP_INFO[CTR_OP_COUNT] = {
    [CTR_OP_LOAD] = {
        .opcode = CTR_OP_LOAD,
        .mnemonic = "load",
        .type = CTR_INS_AB,
    },
    [CTR_OP_MOVE] = {
        .opcode = CTR_OP_MOVE,
        .mnemonic = "move",
        .type = CTR_INS_AB,
    },
    [CTR_OP_RET] = {
        .opcode = CTR_OP_RET,
        .mnemonic = "ret",
        .type = CTR_INS_A,
    },
    [CTR_OP_JMP] = {
        .opcode = CTR_OP_JMP,
        .mnemonic = "jmp",
        .type = CTR_INS_A,
    },

    [CTR_OP_ADD] = {
        .opcode = CTR_OP_ADD,
        .mnemonic = "add",
        .type = CTR_INS_ABC,
    },
    [CTR_OP_SUB] = {
        .opcode = CTR_OP_SUB,
        .mnemonic = "sub",
        .type = CTR_INS_ABC,
    },

    [CTR_OP_EQ] = {
        .opcode = CTR_OP_EQ,
        .mnemonic = "eq",
        .type = CTR_INS_ABC,
    },
    [CTR_OP_LT] = {
        .opcode = CTR_OP_LT,
        .mnemonic = "lt",
        .type = CTR_INS_ABC,
    },
    [CTR_OP_LE] = {
        .opcode = CTR_OP_LE,
        .mnemonic = "le",
        .type = CTR_INS_ABC,
    },

    [CTR_OP_OBJ_NEW] = {
        .opcode = CTR_OP_OBJ_NEW,
        .mnemonic = "obj.new",
        .type = CTR_INS_A,
    },
    [CTR_OP_OBJ_SET] = {
        .opcode = CTR_OP_OBJ_SET,
        .mnemonic = "obj.set",
        .type = CTR_INS_ABC,
    },
    [CTR_OP_OBJ_GET] = {
        .opcode = CTR_OP_OBJ_GET,
        .mnemonic = "obj.get",
        .type = CTR_INS_ABC,
    },

    [CTR_OP_STR_FROM] = {
        .opcode = CTR_OP_STR_FROM,
        .mnemonic = "str.from",
        .type = CTR_INS_AB,
    },
    [CTR_OP_STR_ECHO] = {
        .opcode = CTR_OP_STR_ECHO,
        .mnemonic = "str.echo",
        .type = CTR_INS_A,
    },

    [CTR_OP_DBG_DUMP] = {
        .opcode = CTR_OP_DBG_DUMP,
        .mnemonic = "dbg.dump",
        .type = CTR_INS_A,
    },

    [CTR_OP_UNKNOWN] = {
        .opcode = CTR_OP_UNKNOWN,
        .mnemonic = "???",
    }
};

const sf_str CTR_TYPE_NAMES[(size_t)CTR_TCOUNT + (size_t)CTR_DCOUNT] = {
    sf_lit("nil"),
    sf_lit("f64"),
    sf_lit("i64"),
    sf_lit("dyn"),

    sf_lit("str"),
    sf_lit("obj"),
    sf_lit("fun"),
};
