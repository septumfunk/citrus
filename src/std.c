#include "sol/bytecode.h"
#include "sol/vm.h"
#include "sf/math.h"
#include "sf/str.h"
#include <sf/fs.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
double sol_timesec(void) {
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (double)(uli.QuadPart - 116444736000000000ULL) / 10000000.0;
}
#else
#include <time.h>
#include <sys/time.h>
double sol_timesec(void) {
#if defined(CLOCK_REALTIME)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
#endif
}
#endif


#define sol_serr(T, str) \
    sol_call_ex_err((sol_call_err){T, \
        sf_ref(str), \
    0})
#define sol_serrf(T, fmt, ...) \
    sol_call_ex_err((sol_call_err){T, \
        sf_str_fmt(fmt, __VA_ARGS__), \
    0})
#define expect_type(T, val) do { \
    if (val.tt != T) \
        return sol_serrf(SOL_ERRV_TYPE_MISMATCH, "'%s' expected %s, found %s", #val, SOL_TYPE_NAMES[T], sol_typename(val).c_str); \
} while (0);
#define expect_dtype(T, val) do { \
    if (!sol_isdtype(val, T)) \
        return sol_serrf(SOL_ERRV_TYPE_MISMATCH, "'%s' expected %s, found %s", #val, SOL_TYPE_NAMES[(int)SOL_TDYN + 1 + T], sol_typename(val).c_str); \
} while (0);


static sol_call_ex builtin_import(sol_state *s) {
    sol_val path = sol_get(s, 0);
    expect_dtype(SOL_DSTR, path)

    sf_str cwd = sol_cwd(s);

    sf_str p = sf_str_fmt("%s%s", cwd.c_str, ((sf_str *)path.dyn)->c_str);
    if (!sf_file_exists(p)) {
        sf_str p2 = sf_str_fmt("%s.sol", p.c_str);
        sf_str_free(p);
        p = p2;
    }
    if (!sf_file_exists(p)) {
        sf_str p2 = sf_str_fmt("File '%s' not found", p.c_str);
        sf_str_free(p);
        return sol_call_ex_ok(sol_dnerr(s, p2));
    }

    sol_compile_ex cm_ex = sol_cfile(s, p);
    if (!cm_ex.is_ok)
        return sol_call_ex_ok(sol_dnerr(s, sf_str_dup(sol_err_string(cm_ex.err.tt))));
    sol_call_ex cl_ex = sol_call(s, &cm_ex.ok, NULL, 0);
    sol_fproto_free(&cm_ex.ok);
    if (!cl_ex.is_ok)
            return sol_call_ex_ok(sol_dnerr(s, sf_str_dup(cl_ex.err.panic)
            ));
    return cl_ex;
}
static sol_call_ex builtin_require(sol_state *s) {
    sol_call_ex import = builtin_import(s);
    if (!import.is_ok) return import;
    if (sol_isdtype(import.ok, SOL_DERR))
        return sol_call_ex_err((sol_call_err){
            SOL_ERRV_PANIC, sf_str_dup(*(sf_str *)import.ok.dyn), 0
        });
    return import;
}
static sol_call_ex builtin_eval(sol_state *s) {
    sol_val src = sol_get(s, 0);
    expect_dtype(SOL_DSTR, src);

    sol_compile_ex cm_ex = sol_csrc(s, *(sf_str *)src.dyn);
    if (!cm_ex.is_ok)
        return sol_call_ex_ok(sol_dnerr(s, sf_str_dup(sol_err_string(cm_ex.err.tt))));
    sol_call_ex cl_ex = sol_call(s, &cm_ex.ok, NULL, 0);
    sol_fproto_free(&cm_ex.ok);
    if (!cl_ex.is_ok)
        return sol_call_ex_ok(sol_dnerr(s, sf_str_dup(cl_ex.err.tt == SOL_ERRV_PANIC ?
            cl_ex.err.panic :
            sol_err_string(cm_ex.err.tt))
        ));
    return cl_ex;
}
static sol_call_ex builtin_err(sol_state *s) {
    sol_val str = sol_get(s, 0);
    sol_val err = sol_dnew(s, SOL_DERR);
    *(sf_str *)err.dyn = sf_str_dup(*(sf_str *)str.dyn);
    return sol_call_ex_ok(err);
}
static sol_call_ex builtin_panic(sol_state *s) {
    sol_val err = sol_get(s, 0);
    if (!sol_isdtype(err, SOL_DSTR))
        return sol_call_ex_err((sol_call_err){SOL_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'err' expected str, found '%s'", sol_typename(err).c_str),
        0});
    return sol_call_ex_err((sol_call_err){SOL_ERRV_PANIC, sf_str_dup(*(sf_str *)err.dyn), 0});
}
static sol_call_ex builtin_catch(sol_state *s) {
    sol_val try = sol_get(s, 0);
    if (!sol_isdtype(try, SOL_DFUN))
        return sol_call_ex_err((sol_call_err){SOL_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'try' expected fun, found '%s'", sol_typename(try).c_str),
        0});
    sol_call_ex try_ex = sol_call(s, try.dyn, NULL, 0);
    if (!try_ex.is_ok) {
        sol_popframe(s); // Frame remains after panic!
        return sol_call_ex_ok(sol_dnerr(s, sf_str_dup(try_ex.err.panic)));
    }
    return sol_call_ex_ok(try_ex.ok);
}
static sol_call_ex builtin_attempt(sol_state *s) {
    sol_val try = sol_get(s, 0);
    if (!sol_isdtype(try, SOL_DFUN))
        return sol_call_ex_err((sol_call_err){SOL_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'try' expected fun, found '%s'", sol_typename(try).c_str),
        0});
    sol_val handler = sol_get(s, 1);
    if (!sol_isdtype(handler, SOL_DFUN))
        return sol_call_ex_err((sol_call_err){SOL_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'handler' expected fun, found '%s'", sol_typename(handler).c_str),
        0});
    sol_call_ex try_ex = sol_call(s, try.dyn, NULL, 0);
    if (!try_ex.is_ok) {
        sol_popframe(s); // Frame remains after panic!
        sol_val err = sol_dnerr(s, sf_str_dup(try_ex.err.panic));
        sol_call_ex hand_ex = sol_call(s, handler.dyn, (sol_val[]){err}, 1);
        if (!hand_ex.is_ok) return hand_ex;
        return sol_call_ex_ok(hand_ex.ok);
    }
    if (sol_isdtype(try_ex.ok, SOL_DERR)) {
        sol_call_ex hand_ex = sol_call(s, handler.dyn, (sol_val[]){try_ex.ok}, 1);
        if (!hand_ex.is_ok) return hand_ex;
        return sol_call_ex_ok(hand_ex.ok);
    }
    return sol_call_ex_ok(try_ex.ok);
}
static sol_call_ex builtin_unwrap(sol_state *s) {
    sol_val val = sol_get(s, 0);
    if (!sol_isdtype(val, SOL_DERR))
        return sol_call_ex_ok(val);
    return sol_call_ex_err((sol_call_err){SOL_ERRV_PANIC, sf_str_dup(*(sf_str *)val.dyn), 0});
}
static sol_call_ex builtin_unwrap_or(sol_state *s) {
    sol_val val = sol_get(s, 0);
    if (!sol_isdtype(val, SOL_DERR))
        return sol_call_ex_ok(val);
    return sol_call_ex_ok(sol_get(s, 1));
}
static sol_call_ex builtin_assert(sol_state *s) {
    sol_val con = sol_get(s, 0);
    expect_type(SOL_TBOOL, con);
    return con.boolean ? sol_call_ex_ok(SOL_NIL) : sol_call_ex_err((sol_call_err){SOL_ERRV_ASSERT, SF_STR_EMPTY, 0});
}
static sol_call_ex builtin_type(sol_state *s) {
    return sol_call_ex_ok(sol_dnstr(s, sol_typename(sol_get(s, 0))));
}

static sol_call_ex builtin_str(sol_state *s) {
    return sol_call_ex_ok(sol_dnstr(s, sol_tostring(sol_get(s, 0))));
}
static sol_call_ex builtin_i64(sol_state *s) {
    sol_val f64 = sol_get(s, 0);
    expect_type(SOL_TF64, f64);
    return sol_call_ex_ok((sol_val){SOL_TI64, .i64 = (sol_i64)f64.f64});
}
static sol_call_ex builtin_f64(sol_state *s) {
    sol_val i64 = sol_get(s, 0);
    expect_type(SOL_TI64, i64);
    return sol_call_ex_ok((sol_val){SOL_TF64, .f64 = (sol_f64)i64.i64});
}

static sol_call_ex io_print(sol_state *s) {
    sol_val to_print = sol_get(s, 0);
    sf_str val = sol_tostring(to_print);
    printf("%s", val.c_str);
    sf_str_free(val);
    return sol_call_ex_ok(SOL_NIL);
}
static sol_call_ex io_println(sol_state *s) {
    sol_val to_print = sol_get(s, 0);
    sf_str val = sol_tostring(to_print);
    printf("%s\n", val.c_str);
    sf_str_free(val);
    return sol_call_ex_ok(SOL_NIL);
}
static sol_call_ex io_time(sol_state *s) {
    (void)s;
    return sol_call_ex_ok((sol_val){.tt = SOL_TF64, .f64 = sol_timesec()});
}
static sol_call_ex io_fread(sol_state *s) {
    sol_val path = sol_get(s, 0);
    expect_dtype(SOL_DSTR, path);

    sf_str p = *(sf_str *)path.dyn;
    if (!sf_file_exists(p))
        return sol_call_ex_ok(sol_dnerr(s, sf_str_fmt("File '%s' not found", p.c_str)));
    sf_fsb_ex fsb = sf_file_buffer(p);
    if (!fsb.is_ok) {
        sf_str errs = SF_STR_EMPTY;
        switch (fsb.err) {
            case SF_FILE_NOT_FOUND: errs = sf_str_fmt("File '%s' not found", p.c_str); break;
            case SF_OPEN_FAILURE: errs = sf_str_fmt("File '%s' failed to open", p.c_str); break;
            case SF_READ_FAILURE: errs = sf_str_fmt("File '%s' failed to read", p.c_str); break;
        }
        return sol_call_ex_ok(sol_dnerr(s, sf_str_dup(errs)));
    }
    fsb.ok.flags = SF_BUFFER_GROW;
    sf_buffer_autoins(&fsb.ok, ""); // [\0]

    sol_val str = sol_dnew(s, SOL_DSTR);
    *(sf_str *)str.dyn = sf_own((char *)fsb.ok.ptr);
    return sol_call_ex_ok(str);
}
static sol_call_ex io_fwrite(sol_state *s) {
    sol_val path = sol_get(s, 0);
    expect_dtype(SOL_DSTR, path);
    sol_val content = sol_get(s, 1);
    expect_dtype(SOL_DSTR, content);

    sf_str p = *(sf_str *)path.dyn;
    sf_str cont = *(sf_str *)content.dyn;

    FILE *f = fopen(p.c_str, "w");
    if (!f) return sol_call_ex_ok(sol_dnerr(s, sf_str_fmt("File '%s' failed to open", p.c_str)));;
    fwrite(cont.c_str, 1, cont.len, f);
    fclose(f);

    return sol_call_ex_ok(SOL_NIL);
}

static sol_call_ex string_sub(sol_state *s) {
    sol_val str = sol_get(s, 0);
    expect_dtype(SOL_DSTR, str);
    sol_val start = sol_get(s, 1);
    expect_type(SOL_TI64, start);
    sol_val end = sol_get(s, 2);
    expect_type(SOL_TI64, end);
    if (end.i64 < start.i64)
        return sol_serr(SOL_ERRV_PANIC, "end cannot be before start");

    sf_str *sstr = str.dyn;
    sol_i64 len = (sol_i64)sstr->len;
    if (len == 0)
        return sol_call_ex_ok(sol_dnew(s, SOL_DSTR));
    start.i64 = max(0, min(start.i64, len > 0 ? len - 1 : 0));
    end.i64 = max(0, min(end.i64, len > 0 ? len - 1 : 0));

    size_t slen = (size_t)(end.i64 - start.i64 + 1);
    char *buf = malloc(slen + 1);
    memcpy(buf, sstr->c_str + start.i64, slen - 1);
    buf[slen] = 0;
    return sol_call_ex_ok(sol_dnstr(s, sf_own(buf)));
}
static sol_call_ex string_len(sol_state *s) {
    sol_val str = sol_get(s, 0);
    expect_dtype(SOL_DSTR, str);
    return sol_call_ex_ok((sol_val){.tt = SOL_TI64, .i64 = (sol_i64)((sf_str *)str.dyn)->len});
}

static sol_call_ex obj_new(sol_state *s) {
    return sol_call_ex_ok(sol_dnew(s, SOL_DOBJ));
}
static sol_call_ex obj_set(sol_state *s) {
    sol_val obj = sol_get(s, 0);
    expect_dtype(SOL_DOBJ, obj);
    sol_val key = sol_get(s, 1);
    sol_val val = sol_get(s, 2);

    sf_str kstr;
    if (!sol_isdtype(key, SOL_DSTR))
        kstr = sol_tostring(key);
    else kstr = sf_str_dup(*(sf_str *)key.dyn);
    sol_dobj_set(obj.dyn, kstr, val);
    return sol_call_ex_ok(SOL_NIL);
}
static sol_call_ex obj_get(sol_state *s) {
    sol_val obj = sol_get(s, 0);
    expect_dtype(SOL_DOBJ, obj);
    sol_val key = sol_get(s, 1);

    sf_str kstr;
    if (!sol_isdtype(key, SOL_DSTR))
        kstr = sol_tostring(key);
    else kstr = sf_str_dup(*(sf_str *)key.dyn);

    sol_dobj_ex ex = sol_dobj_get(obj.dyn, kstr);
    if (!ex.is_ok) {
        sf_str estr = sf_str_fmt("Object does not contain member '%s'", kstr.c_str);
        sf_str_free(kstr);
        return sol_call_ex_err((sol_call_err){SOL_ERRV_MEMBER_NOT_FOUND, estr, 0});
    }
    return sol_call_ex_ok(ex.ok);
}

typedef struct {
    sf_str *out;
    bool pretty, commas;
    uint32_t id;
} _sol_stringify_args;
static void _stringify_fe(void *u, sf_str key, sol_val val);
static sf_str _stringify(sol_dobj *obj, bool pretty, bool commas, uint32_t id) {
    if (obj->pair_count == 0) return sf_lit("{}");
    sf_str out = sf_str_cdup(pretty ? "{\n" : "{ ");
    sol_dobj_foreach(obj, _stringify_fe, &(_sol_stringify_args){&out, pretty, commas, id});

    if (pretty && id) {
        size_t s = sizeof(char) * (id-1) * 2;
        char *idt = malloc(s + 1);
        memset(idt, ' ', sizeof(char) * (id-1) * 2);
        sf_str_append(&out, sf_ref(idt));
        free(idt);
    }
    sf_str_append(&out, sf_lit("}"));
    return out;
}
static void _stringify_fe(void *u, sf_str key, sol_val val) {
    _sol_stringify_args *args = u;
    if (args->pretty && args->id) {
        size_t s = sizeof(char) * args->id * 2;
        char *id = malloc(s + 1);
        memset(id, ' ', sizeof(char) * args->id * 2);
        sf_str_append(args->out, sf_ref(id));
        free(id);
    }
    sf_str_append(args->out, key);
    sf_str_append(args->out, sf_lit(" = "));
    switch (val.tt) {
        case SOL_TDYN: if (sol_isdtype(val, SOL_DOBJ)) {
            sf_str_append(args->out, _stringify(val.dyn, args->pretty, args->commas, args->id + 1));
            break;
        }
        default: sf_str_append(args->out, sol_tostring(val)); break;
    }
    sf_str ec = sf_lit(" ");
    if (args->pretty && args->commas)
        ec = sf_lit(",\n");
    else if (args->commas)
        ec = sf_lit(",");
    else if (args->pretty)
        ec = sf_lit("\n");
    sf_str_append(args->out, ec);
}
static sol_call_ex obj_stringify(sol_state *s) {
    sol_val obj = sol_get(s, 0);
    expect_dtype(SOL_DOBJ, obj);
    sol_val pretty = sol_get(s, 1);
    sol_val commas = sol_get(s, 2);

    return sol_call_ex_ok(sol_dnstr(s, _stringify(
        obj.dyn,
        pretty.tt == SOL_TBOOL ? pretty.boolean : true,
        commas.tt == SOL_TBOOL ? commas.boolean : false,
        1
    )));
}

static sol_call_ex math_mini(sol_state *s) {
    sol_val a = sol_get(s, 0);
    expect_type(SOL_TI64, a);
    sol_val b = sol_get(s, 0);
    expect_type(SOL_TI64, b);
    return sol_call_ex_ok((sol_val){SOL_TI64, .i64 = min(a.i64, b.i64)});
}
static sol_call_ex math_maxi(sol_state *s) {
    sol_val a = sol_get(s, 0);
    expect_type(SOL_TI64, a);
    sol_val b = sol_get(s, 0);
    expect_type(SOL_TI64, b);
    return sol_call_ex_ok((sol_val){SOL_TI64, .i64 = max(a.i64, b.i64)});
}
static sol_call_ex math_minf(sol_state *s) {
    sol_val a = sol_get(s, 0);
    expect_type(SOL_TF64, a);
    sol_val b = sol_get(s, 0);
    expect_type(SOL_TF64, b);
    return sol_call_ex_ok((sol_val){SOL_TF64, .f64 = min(a.f64, b.f64)});
}
static sol_call_ex math_maxf(sol_state *s) {
    sol_val a = sol_get(s, 0);
    expect_type(SOL_TF64, a);
    sol_val b = sol_get(s, 0);
    expect_type(SOL_TF64, b);
    return sol_call_ex_ok((sol_val){SOL_TF64, .f64 = min(a.f64, b.f64)});
}
static sol_call_ex math_randi(sol_state *s) {
    sol_val min_v = sol_get(s, 0);
    expect_type(SOL_TI64, min_v);
    sol_val max_v = sol_get(s, 1);
    expect_type(SOL_TI64, max_v);
    sol_i64 min = min_v.i64, max = max_v.i64;


    if (min > max) { int64_t tmp = min; min = max; max = tmp; }
    uint64_t range = (uint64_t)(max - min) + 1;
#if defined(_WIN32)
    uint64_t r = ((uint64_t)rand() << 48) | ((uint64_t)rand() << 32) |
                 ((uint64_t)rand() << 16) | (uint64_t)rand();
#else
    uint64_t r = (uint64_t)rand();
#endif
    return sol_call_ex_ok((sol_val){ .tt = SOL_TI64, .i64 = (int64_t)(r % range) + min });
}
static sol_call_ex math_randf(sol_state *s) {
    sol_val min_v = sol_get(s, 0);
    expect_type(SOL_TF64, min_v);
    sol_val max_v = sol_get(s, 1);
    expect_type(SOL_TF64, max_v);
    double min = min_v.f64, max = max_v.f64;

    if (min > max) { double tmp = min; min = max; max = tmp; }

    double frac = (double)rand() / (double)RAND_MAX;
    double val = min + frac * (max - min);

    return sol_call_ex_ok((sol_val){ .tt = SOL_TF64, .f64 = val });
}

static sol_call_ex gc_collect(sol_state *s) {
    sol_dcollect(s);
    return sol_call_ex_ok(SOL_NIL);
}

void sol_usestd(sol_state *state) {
    sol_val sol = sol_dnew(state, SOL_DOBJ);
    sol_dobj_set(sol.dyn, sf_lit("version"), sol_dnstr(state, sf_str_cdup(SOL_VERSION)));
    sol_dobj_set(sol.dyn, sf_lit("git"), sol_dnstr(state, sf_str_cdup(SOL_GIT)));

    sol_val io = sol_dnew(state, SOL_DOBJ);
    sol_dobj_set(io.dyn, sf_lit("print"), sol_wrapcfun(state, io_print, 1, 0));
    sol_dobj_set(io.dyn, sf_lit("println"), sol_wrapcfun(state, io_println, 1, 0));
    sol_dobj_set(io.dyn, sf_lit("time"), sol_wrapcfun(state, io_time, 0, 0));
    sol_dobj_set(io.dyn, sf_lit("fread"), sol_wrapcfun(state, io_fread, 1, 0));
    sol_dobj_set(io.dyn, sf_lit("fwrite"), sol_wrapcfun(state, io_fwrite, 2, 0));

    sol_val string = sol_dnew(state, SOL_DOBJ);
    sol_dobj_set(string.dyn, sf_lit("sub"), sol_wrapcfun(state, string_sub, 3, 0));
    sol_dobj_set(string.dyn, sf_lit("len"), sol_wrapcfun(state, string_len, 1, 0));

    sol_val obj = sol_dnew(state, SOL_DOBJ);
    sol_dobj_set(obj.dyn, sf_lit("new"), sol_wrapcfun(state, obj_new, 0, 0));
    sol_dobj_set(obj.dyn, sf_lit("set"), sol_wrapcfun(state, obj_set, 3, 0));
    sol_dobj_set(obj.dyn, sf_lit("get"), sol_wrapcfun(state, obj_get, 2, 0));
    sol_dobj_set(obj.dyn, sf_lit("stringify"), sol_wrapcfun(state, obj_stringify, 3, 0));

    sol_val math = sol_dnew(state, SOL_DOBJ);
    sol_dobj_set(math.dyn, sf_lit("mini"), sol_wrapcfun(state, math_mini, 2, 0));
    sol_dobj_set(math.dyn, sf_lit("maxi"), sol_wrapcfun(state, math_maxi, 2, 0));
    sol_dobj_set(math.dyn, sf_lit("minf"), sol_wrapcfun(state, math_minf, 2, 0));
    sol_dobj_set(math.dyn, sf_lit("maxf"), sol_wrapcfun(state, math_maxf, 2, 0));
    sol_dobj_set(math.dyn, sf_lit("randi"), sol_wrapcfun(state, math_randi, 2, 0));
    sol_dobj_set(math.dyn, sf_lit("randf"), sol_wrapcfun(state, math_randf, 2, 0));

    sol_val gc = sol_dnew(state, SOL_DOBJ);
    sol_dobj_set(gc.dyn, sf_lit("collect"), sol_wrapcfun(state, gc_collect, 0, 0));

    sol_dobj *_g = state->global.dyn;
    sol_dobj_set(_g, sf_lit("import"), sol_wrapcfun(state, builtin_import, 1, 0));
    sol_dobj_set(_g, sf_lit("require"), sol_wrapcfun(state, builtin_require, 1, 0));
    sol_dobj_set(_g, sf_lit("eval"), sol_wrapcfun(state, builtin_eval, 1, 0));
    sol_dobj_set(_g, sf_lit("panic"), sol_wrapcfun(state, builtin_panic, 1, 0));
    sol_dobj_set(_g, sf_lit("catch"), sol_wrapcfun(state, builtin_catch, 1, 0));
    sol_dobj_set(_g, sf_lit("attempt"), sol_wrapcfun(state, builtin_attempt, 2, 0));
    sol_dobj_set(_g, sf_lit("unwrap"), sol_wrapcfun(state, builtin_unwrap, 1, 0));
    sol_dobj_set(_g, sf_lit("unwrap_or"), sol_wrapcfun(state, builtin_unwrap_or, 2, 0));
    sol_dobj_set(_g, sf_lit("assert"), sol_wrapcfun(state, builtin_assert, 1, 0));
    sol_dobj_set(_g, sf_lit("type"), sol_wrapcfun(state, builtin_type, 1, 0));

    sol_dobj_set(_g, sf_lit("str"), sol_wrapcfun(state, builtin_str, 1, 0));
    sol_dobj_set(_g, sf_lit("err"), sol_wrapcfun(state, builtin_err, 1, 0));
    sol_dobj_set(_g, sf_lit("i64"), sol_wrapcfun(state, builtin_i64, 1, 0));
    sol_dobj_set(_g, sf_lit("f64"), sol_wrapcfun(state, builtin_f64, 1, 0));


    sol_dobj_set(_g, sf_lit("sol"), sol);
    sol_dobj_set(_g, sf_lit("io"), io);
    sol_dobj_set(_g, sf_lit("string"), sol);
    sol_dobj_set(_g, sf_lit("obj"), obj);
    sol_dobj_set(_g, sf_lit("math"), math);

    srand((unsigned)time(NULL));
}
