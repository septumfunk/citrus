#include "ctr/bytecode.h"
#include "ctr/vm.h"
#include "sf/math.h"
#include "sf/str.h"
#include <sf/gfx/window.h>
#include <sf/fs.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
double ctr_timesec(void) {
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
double ctr_timesec(void) {
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

ctr_call_ex ctr_std_print(ctr_state *s) {
    ctr_val to_print = ctr_get(s, 0);
    sf_str val = ctr_tostring(to_print);
    printf("%s", val.c_str);
    sf_str_free(val);
    return ctr_call_ex_ok(CTR_NIL);
}
ctr_call_ex ctr_std_println(ctr_state *s) {
    ctr_val to_print = ctr_get(s, 0);
    sf_str val = ctr_tostring(to_print);
    printf("%s\n", val.c_str);
    sf_str_free(val);
    return ctr_call_ex_ok(CTR_NIL);
}
ctr_call_ex ctr_std_time(ctr_state *s) {
    (void)s;
    return ctr_call_ex_ok((ctr_val){.tt = CTR_TF64, .f64 = ctr_timesec()});
}
ctr_call_ex ctr_std_string(ctr_state *s) {
    ctr_val var = ctr_get(s, 0);
    ctr_val str = ctr_dnew(CTR_DSTR);
    *(sf_str *)str.dyn = ctr_tostring(var);
    return ctr_call_ex_ok(str);
}

ctr_call_ex ctr_std_new(ctr_state *s) {
    (void)s;
    return ctr_call_ex_ok(ctr_dnew(CTR_DOBJ));
}
ctr_call_ex ctr_std_set(ctr_state *s) {
    ctr_val obj = ctr_get(s, 0);
    ctr_val key = ctr_get(s, 1);
    ctr_val val = ctr_get(s, 2);

    if (!ctr_isdtype(obj, CTR_DOBJ))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'obj' expected obj, found '%s'", ctr_typename(obj).c_str),
        0});

    sf_str kstr;
    if (!ctr_isdtype(key, CTR_DSTR))
        kstr = ctr_tostring(key);
    else kstr = sf_str_dup(*(sf_str *)key.dyn);
    ctr_dobj_set(obj.dyn, kstr, ctr_dref(val));
    return ctr_call_ex_ok(CTR_NIL);
}
ctr_call_ex ctr_std_get(ctr_state *s) {
    ctr_val obj = ctr_get(s, 0);
    ctr_val key = ctr_get(s, 1);

    if (!ctr_isdtype(obj, CTR_DOBJ))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'obj' expected obj, found '%s'", ctr_typename(obj).c_str),
        0});

    sf_str kstr;
    if (!ctr_isdtype(key, CTR_DSTR))
        kstr = ctr_tostring(key);
    else kstr = sf_str_dup(*(sf_str *)key.dyn);

    ctr_dobj_ex ex = ctr_dobj_get(obj.dyn, kstr);
    if (!ex.is_ok) {
        sf_str estr = sf_str_fmt("Object does not contain member '%s'", kstr.c_str);
        sf_str_free(kstr);
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_MEMBER_NOT_FOUND, estr, 0});
    }

    return ctr_call_ex_ok(ctr_dref(ex.ok));
}

ctr_call_ex ctr_std_err(ctr_state *s) {
    ctr_val str = ctr_get(s, 0);
    ctr_val err = ctr_dnew(CTR_DERR);
    *(sf_str *)err.dyn = sf_str_dup(*(sf_str *)str.dyn);
    return ctr_call_ex_ok(err);
}
ctr_call_ex ctr_std_panic(ctr_state *s) {
    ctr_val err = ctr_get(s, 0);
    if (!ctr_isdtype(err, CTR_DERR))
        return ctr_call_ex_ok(ctr_dref(err));
    return ctr_call_ex_err((ctr_call_err){CTR_ERRV_PANIC, sf_str_dup(*(sf_str *)err.dyn), 0});
}
ctr_call_ex ctr_std_assert(ctr_state *s) {
    ctr_val con = ctr_get(s, 0);
    if (con.tt != CTR_TBOOL)
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'con' expected bool, found '%s'", ctr_typename(con).c_str),
        0});
    return con.boolean ? ctr_call_ex_ok(CTR_NIL) : ctr_call_ex_err((ctr_call_err){CTR_ERRV_ASSERT, SF_STR_EMPTY, 0});
}
ctr_call_ex ctr_std_type(ctr_state *s) {
    ctr_val val = ctr_get(s, 0);
    ctr_val str = ctr_dnew(CTR_DSTR);
    *(sf_str *)str.dyn = ctr_typename(val);
    return ctr_call_ex_ok(str);
}

ctr_call_ex ctr_std_randi(ctr_state *s) {
    ctr_val min_v = ctr_get(s, 0);
    ctr_val max_v = ctr_get(s, 1);
    ctr_i64 min, max;
    if (min_v.tt != CTR_TI64) {
        if (min_v.tt == CTR_TF64) min = min_v.i64;
        else {
            return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
                sf_str_fmt("Arg 'min' expected i64, found '%s'", ctr_typename(min_v).c_str),
            0});
        }
    } else min = min_v.i64;
    if (max_v.tt != CTR_TI64) {
        if (max_v.tt == CTR_TF64) max = max_v.i64;
        else {
            return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
                sf_str_fmt("Arg 'max' expected i64, found '%s'", ctr_typename(max_v).c_str),
            0});
        }
    } else max = max_v.i64;

    if (min > max) { int64_t tmp = min; min = max; max = tmp; }
    uint64_t range = (uint64_t)(max - min) + 1;
#if defined(_WIN32)
    uint64_t r = ((uint64_t)rand() << 48) | ((uint64_t)rand() << 32) |
                 ((uint64_t)rand() << 16) | rand();
#else
    uint64_t r = (uint64_t)rand();
#endif
    return ctr_call_ex_ok((ctr_val){ .tt = CTR_TI64, .i64 = (int64_t)(r % range) + min });
}
ctr_call_ex ctr_std_randf(ctr_state *s) {
    ctr_val min_v = ctr_get(s, 0);
    ctr_val max_v = ctr_get(s, 1);
    double min, max;

    if (min_v.tt == CTR_TF64) min = min_v.f64;
    else if (min_v.tt == CTR_TI64) min = (double)min_v.i64;
    else {
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'min' expected f64, found '%s'", ctr_typename(min_v).c_str),
        0});
    }

    if (max_v.tt == CTR_TF64) max = max_v.f64;
    else if (max_v.tt == CTR_TI64) max = (double)max_v.i64;
    else {
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'max' expected f64, found '%s'", ctr_typename(max_v).c_str),
        0});
    }

    if (min > max) { double tmp = min; min = max; max = tmp; }

    double frac = (double)rand() / (double)RAND_MAX; // [0, 1]
    double val = min + frac * (max - min);

    return ctr_call_ex_ok((ctr_val){ .tt = CTR_TF64, .f64 = val });
}
ctr_call_ex ctr_std_floor(ctr_state *s) {
    ctr_val f64 = ctr_get(s, 0);
    return ctr_call_ex_ok((ctr_val){ .tt = CTR_TI64, .i64 = (ctr_i64)f64.f64 });
}

ctr_call_ex ctr_std_fread(ctr_state *s) {
    ctr_val path = ctr_get(s, 0);
    if (!ctr_isdtype(path, CTR_DSTR))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'path' expected str, found '%s'", ctr_typename(path).c_str),
        0});
    sf_str p = *(sf_str *)path.dyn;
    if (!sf_file_exists(p))
        return ctr_call_ex_ok(ctr_dnewerr(sf_str_fmt("File '%s' not found", p.c_str)));
    sf_fsb_ex fsb = sf_file_buffer(p);
    if (!fsb.is_ok) {
        sf_str errs;
        switch (fsb.err) {
            case SF_FILE_NOT_FOUND: errs = sf_str_fmt("File '%s' not found", p.c_str); break;
            case SF_OPEN_FAILURE: errs = sf_str_fmt("File '%s' failed to open", p.c_str); break;
            case SF_READ_FAILURE: errs = sf_str_fmt("File '%s' failed to read", p.c_str); break;
        }
        return ctr_call_ex_ok(ctr_dnewerr(sf_str_dup(errs)));
    }
    fsb.ok.flags = SF_BUFFER_GROW;
    sf_buffer_autoins(&fsb.ok, ""); // [\0]

    ctr_val str = ctr_dnew(CTR_DSTR);
    *(sf_str *)str.dyn = sf_own((char *)fsb.ok.ptr);
    return ctr_call_ex_ok(str);
}
ctr_call_ex ctr_std_fwrite(ctr_state *s) {
    ctr_val path = ctr_get(s, 0);
    ctr_val str = ctr_get(s, 0);
    if (!ctr_isdtype(path, CTR_DSTR))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'path' expected str, found '%s'", ctr_typename(path).c_str),
        0});
    if (!ctr_isdtype(path, CTR_DSTR))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'path' expected str, found '%s'", ctr_typename(path).c_str),
        0});
    sf_str p = *(sf_str *)path.dyn;
    sf_str cont = *(sf_str *)str.dyn;

    FILE *f = fopen(p.c_str, "w");
    if (!f) return ctr_call_ex_ok(ctr_dnewerr(sf_str_fmt("File '%s' failed to open", p.c_str)));;
    fwrite(cont.c_str, 1, cont.len, f);
    fclose(f);

    return ctr_call_ex_ok(CTR_NIL);
}

sf_str _ctr_camera_tostr(sf_camera *c) { return sf_str_fmt("sf_camera (%f, %f, %f)", c->fov, c->near, c->far); }
ctr_call_ex ctr_std_gfx_camera(ctr_state *s) {
    ctr_val fov = ctr_get(s, 0);
    ctr_val near = ctr_get(s, 1);
    ctr_val far = ctr_get(s, 2);
    if (fov.tt != CTR_TF64 && fov.tt != CTR_TI64)
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'fov' expected number, found '%s'", ctr_typename(fov).c_str),
        0});
    if (near.tt != CTR_TF64 && near.tt != CTR_TI64)
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'near' expected number, found '%s'", ctr_typename(near).c_str),
        0});
    if (far.tt != CTR_TF64 && far.tt != CTR_TI64)
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'far' expected number, found '%s'", ctr_typename(far).c_str),
        0});
    sf_camera main_cam = sf_camera_new(
        SF_CAMERA_PERSPECTIVE,
        fov.tt == CTR_TF64 ? (float)fov.f64 : (float)fov.i64,
        near.tt == CTR_TF64 ? (float)near.f64 : (float)near.i64,
        far.tt == CTR_TF64 ? (float)far.f64 : (float)far.i64
    );
    return ctr_call_ex_ok(ctr_dnewusr(
        sizeof(sf_camera),
        sf_lit("sf_camera"),
        &main_cam,
        (ctr_usrdel)sf_camera_delete,
        (ctr_usrtostring)_ctr_camera_tostr
    ));
}

sf_str _ctr_shader_tostr(sf_shader *s) { return sf_str_fmt("sf_shader ('%s')", s->path.c_str); }
ctr_call_ex ctr_std_gfx_shader(ctr_state *s) {
    ctr_val path = ctr_get(s, 0);
    if (!ctr_isdtype(path, CTR_DSTR))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'path' expected str, found '%s'", ctr_typename(path).c_str),
        0});
    sf_shader_ex sx = sf_shader_new(*(sf_str *)path.dyn);
    if (!sx.is_ok) {
        switch (sx.err.type) {
            case SF_SHADER_COMPILE_ERROR:
                return ctr_call_ex_ok(ctr_dnewerr(sf_str_fmt("Shader '%s' failed to compile: %s\n",
                    ((sf_str *)path.dyn)->c_str, sx.err.compile_err.c_str)));
            case SF_SHADER_NOT_FOUND:
                return ctr_call_ex_ok(ctr_dnewerr(sf_str_fmt("Shader '%s' not found\n",
                    ((sf_str *)path.dyn)->c_str)));
            default: return ctr_call_ex_ok(ctr_dnewerr(sf_lit("Shader '%s' not found\n")));
        }
    }
    return ctr_call_ex_ok(ctr_dnewusr(
        sizeof(sf_shader),
        sf_lit("sf_shader"),
        &sx.ok,
        (ctr_usrdel)sf_shader_free,
        (ctr_usrtostring)_ctr_shader_tostr
    ));
}


void _ctr_window_close(sf_window **w) { sf_window_close(*w); }
sf_str _ctr_window_tostr(sf_window **w) { return sf_str_fmt("sf_window ('%s')", (*w)->title.c_str); }
ctr_call_ex ctr_std_gfx_window(ctr_state *s) {
    ctr_val title = ctr_get(s, 0);
    ctr_val width = ctr_get(s, 1);
    ctr_val height = ctr_get(s, 2);
    ctr_val camera = ctr_get(s, 3);

    if (!ctr_isdtype(title, CTR_DSTR))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'title' expected str, found '%s'", ctr_typename(title).c_str),
        0});
    if (width.tt != CTR_TF64 && width.tt != CTR_TI64)
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'width' expected number, found '%s'", ctr_typename(width).c_str),
        0});
    if (height.tt != CTR_TF64 && height.tt != CTR_TI64)
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'height' expected number, found '%s'", ctr_typename(height).c_str),
        0});
    if (!ctr_isutype(camera, sf_lit("sf_camera")))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'camera' expected 'sf_camera', found '%s'", ctr_typename(camera).c_str),
        0});
    sf_window_ex wx = sf_window_new(
        *(sf_str *)title.dyn,
        (sf_vec2){
            (float)(width.tt == CTR_TF64 ? width.f64 : (ctr_f64)width.i64),
            (float)(height.tt == CTR_TF64 ? height.f64 : (ctr_f64)height.i64)
        },
        ctr_uptr(camera),
        SF_WINDOW_VISIBLE | SF_WINDOW_RESIZABLE
    );
    if (!wx.is_ok) {
        switch (wx.err) {
            case SF_GLFW_INIT_FAILED: return ctr_call_ex_ok(ctr_dnewerr(sf_lit("GLFW failed to initialize")));
            case SF_GLFW_CREATE_FAILED: return ctr_call_ex_ok(ctr_dnewerr(sf_lit("GLFW failed to create a window")));
            case SF_GLAD_INIT_FAILED: return ctr_call_ex_ok(ctr_dnewerr(sf_lit("GLAD failed to initialize")));
            default: return ctr_call_ex_ok(ctr_dnewerr(sf_lit("Window failed to initialize")));
        }
    }
    return ctr_call_ex_ok(ctr_dnewusr(
        sizeof(sf_window *),
        sf_lit("sf_window"),
        &wx.ok,
        (ctr_usrdel)_ctr_window_close,
        (ctr_usrtostring)_ctr_window_tostr
    ));
}
ctr_call_ex ctr_std_gfx_loop(ctr_state *s) {
    ctr_val win = ctr_get(s, 0);
    if (!ctr_isutype(win, sf_lit("sf_window")))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'win' expected 'sf_window', found '%s'", ctr_typename(win).c_str),
        0});
    return ctr_call_ex_ok((ctr_val){.tt = CTR_TBOOL, .boolean = sf_window_loop(*(sf_window **)ctr_uptr(win))});
}
ctr_call_ex ctr_std_gfx_draw(ctr_state *s) {
    ctr_val win = ctr_get(s, 0);
    if (!ctr_isutype(win, sf_lit("sf_window")))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'win' expected 'sf_window', found '%s'", ctr_typename(win).c_str),
        0});
    ctr_val post = ctr_get(s, 1);
    if (!ctr_isutype(post, sf_lit("sf_shader")))
        return ctr_call_ex_err((ctr_call_err){CTR_ERRV_TYPE_MISMATCH,
            sf_str_fmt("Arg 'post' expected 'sf_shader', found '%s'", ctr_typename(post).c_str),
        0});
    sf_draw_ex dx = sf_window_draw(*(sf_window **)ctr_uptr(win), ctr_uptr(post));
    if (!dx.is_ok) {
        switch (dx.err.type) {
            case SF_DRAW_UNKNOWN_UNIFORM:
                return ctr_call_ex_ok(ctr_dnewerr(sf_str_fmt("Unknown uniform: '%s'\n", dx.err.value.uniform_name.c_str)));
            case SF_DRAW_SHADER_MISSING:
                return ctr_call_ex_ok(ctr_dnewerr(sf_lit("Shader missing")));
        }
    }
    return ctr_call_ex_ok((ctr_val){.tt = CTR_TBOOL, .boolean = (ctr_uptr(win))});
}

void ctr_usestd(ctr_state *state) {
    ctr_val io = ctr_dnew(CTR_DOBJ);
    ctr_dobj_set(io.dyn, sf_lit("print"), ctr_wrapcfun(ctr_std_print, 1, 0));
    ctr_dobj_set(io.dyn, sf_lit("println"), ctr_wrapcfun(ctr_std_println, 1, 0));
    ctr_dobj_set(io.dyn, sf_lit("time"), ctr_wrapcfun(ctr_std_time, 0, 0));
    ctr_dobj_set(io.dyn, sf_lit("fread"), ctr_wrapcfun(ctr_std_fread, 2, 0));
    ctr_dobj_set(io.dyn, sf_lit("fwrite"), ctr_wrapcfun(ctr_std_fread, 2, 0));

    ctr_val gfx = ctr_dnew(CTR_DOBJ);
    ctr_dobj_set(gfx.dyn, sf_lit("camera"), ctr_wrapcfun(ctr_std_gfx_camera, 3, 0));
    ctr_dobj_set(gfx.dyn, sf_lit("shader"), ctr_wrapcfun(ctr_std_gfx_shader, 1, 0));
    ctr_dobj_set(gfx.dyn, sf_lit("window"), ctr_wrapcfun(ctr_std_gfx_window, 4, 0));
    ctr_dobj_set(gfx.dyn, sf_lit("loop"), ctr_wrapcfun(ctr_std_gfx_loop, 1, 0));
    ctr_dobj_set(gfx.dyn, sf_lit("draw"), ctr_wrapcfun(ctr_std_gfx_draw, 2, 0));

    ctr_val obj = ctr_dnew(CTR_DOBJ);
    ctr_dobj_set(obj.dyn, sf_lit("new"), ctr_wrapcfun(ctr_std_new, 0, 0));
    ctr_dobj_set(obj.dyn, sf_lit("set"), ctr_wrapcfun(ctr_std_set, 3, 0));
    ctr_dobj_set(obj.dyn, sf_lit("get"), ctr_wrapcfun(ctr_std_get, 2, 0));

    ctr_val math = ctr_dnew(CTR_DOBJ);
    ctr_dobj_set(math.dyn, sf_lit("randi"), ctr_wrapcfun(ctr_std_randi, 2, 0));
    ctr_dobj_set(math.dyn, sf_lit("randf"), ctr_wrapcfun(ctr_std_randf, 2, 0));
    ctr_dobj_set(math.dyn, sf_lit("floor"), ctr_wrapcfun(ctr_std_floor, 1, 0));

    ctr_dobj *_g = state->global.dyn;
    ctr_dobj_set(_g, sf_lit("string"), ctr_wrapcfun(ctr_std_string, 1, 0));
    ctr_dobj_set(_g, sf_lit("err"), ctr_wrapcfun(ctr_std_err, 1, 0));
    ctr_dobj_set(_g, sf_lit("panic"), ctr_wrapcfun(ctr_std_panic, 1, 0));
    ctr_dobj_set(_g, sf_lit("assert"), ctr_wrapcfun(ctr_std_assert, 1, 0));
    ctr_dobj_set(_g, sf_lit("type"), ctr_wrapcfun(ctr_std_type, 1, 0));

    ctr_dobj_set(_g, sf_lit("io"), io);
    ctr_dobj_set(_g, sf_lit("gfx"), gfx);
    ctr_dobj_set(_g, sf_lit("obj"), obj);
    ctr_dobj_set(_g, sf_lit("math"), math);

    srand((unsigned)time(NULL));
}
