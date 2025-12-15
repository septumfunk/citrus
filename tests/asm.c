#include "ctr/vm.h"
#include "sf/containers/buffer.h"
#include "sf/fs.h"
#include <ctr/bytecode.h>
#include <stdio.h>

int main(void) {
    assert(sf_file_exists(sf_lit("ctr.tests/counting.csm")));
    sf_fsb_ex fsb = sf_file_buffer(sf_lit("ctr.tests/counting.csm"));
    if (!fsb.is_ok) {
        switch (fsb.value.err) {
            case SF_FILE_NOT_FOUND: fprintf(stderr, "ctr.tests/counting.csm not found\n");
            case SF_OPEN_FAILURE: fprintf(stderr, "ctr.tests/counting.csm failed to open\n");
            case SF_READ_FAILURE: fprintf(stderr, "ctr.tests/counting.csm failed to read\n");
        }
        return -1;
    }

    sf_buffer *buf = &fsb.value.ok;
    buf->flags = SF_BUFFER_GROW;
    sf_buffer_autoins(buf, ""); // [\0]


    ctr_state *s = ctr_state_new();

    ctr_asm_ex ex = ctr_assemble(sf_ref((char *)buf->ptr));
    if (!ex.is_ok) {
        fprintf(stderr, "NOT doing allat rn. Use a debugger bozo\n");
        return -1;
    }

    ctr_val ret = ctr_run(s, &ex.value.ok);
    if (ret.tt == CTR_TDYN && ctr_header(ret.val.dyn)->tt == CTR_DSTR)
        printf("Returned: %s\n", ((sf_str *)ret.val.dyn)->c_str);

    ctr_state_free(s);
}
