#ifndef CTRC_H
#define CTRC_H

#include <stddef.h>
#include "bytecode.h"

typedef struct {
    ctr_error tt;
    uint16_t line, column;
} ctr_compile_err;

#define EXPECTED_NAME ctr_compile_ex
#define EXPECTED_O ctr_fproto
#define EXPECTED_E ctr_compile_err
#include <sf/containers/expected.h>
/// Compile a ctr_proto from source code
EXPORT ctr_compile_ex ctr_cproto(sf_str src, uint32_t arg_c, ctr_val *args, uint32_t up_c, ctr_upvalue *upvals, bool echo);

#endif // CTRC_H
