#include <ctr/syntax.h>
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

    ctr_scan_ex ex = ctr_scan(sf_ref((char *)fsb.value.ok.ptr));
    if (!ex.is_ok) {
        fprintf(stderr, "Fucked Token: %s\n", ex.value.err.token.c_str);
        sf_str_free(ex.value.err.token);
        return -1;
    }
    sf_str i1 = *(sf_str *)ex.value.ok.data[0].value.val.dyn;
    sf_str i2 = *(sf_str *)ex.value.ok.data[2].value.val.dyn;
    (void)i1;(void)i2;
    ctr_tokenvec_free(&ex.value.ok);
}
