#include <ctr/ctrc.h>
#include <ctr/vm.h>
#include <sf/fs.h>
#include <sf/containers/buffer.h>
#include <stdio.h>

int main(void) {
    sf_str f = sf_lit("ctr.tests/syntax.ctr");
    assert(sf_file_exists(f));
    sf_fsb_ex fsb = sf_file_buffer(f);
    if (!fsb.is_ok) {
        switch (fsb.value.err) {
            case SF_FILE_NOT_FOUND: fprintf(stderr, "%s not found\n", f.c_str);
            case SF_OPEN_FAILURE: fprintf(stderr, "%s failed to open\n", f.c_str);
            case SF_READ_FAILURE: fprintf(stderr, "%s failed to read\n", f.c_str);
        }
        return -1;
    }
    fsb.value.ok.flags = SF_BUFFER_GROW;
    sf_buffer_autoins(&fsb.value.ok, ""); // [\0]

    ctr_compile_ex ex = ctr_cproto(sf_ref((char *)fsb.value.ok.ptr));
    if (!ex.is_ok) {
        return -1;
    }

    ctr_state *s = ctr_state_new();
    ctr_call_ex ex2 = ctr_call(s, &ex.value.ok, NULL);
    if (!ex2.is_ok) {
        return -1;
    }

    sf_str ret = ctr_tostring(ex2.value.ok);
    printf("[RET] [Type: %s] %s", ctr_typename(ex2.value.ok).c_str, ret.c_str);
    sf_str_free(ret);
}
