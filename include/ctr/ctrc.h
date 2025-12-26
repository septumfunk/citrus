#ifndef CTRC_H
#define CTRC_H

#include <stddef.h>
#include "syntax.h"

typedef struct {
    enum ctr_compile_errt {
        CTR_ERRC_NONE,
        CTR_ERRC_SCAN_ERR,
        CTR_ERRC_PARSE_ERR,

        CTR_ERRC_EXPECTED_BLOCK,
        CTR_ERRC_UNKNOWN_LOCAL,
        CTR_ERRC_UNKNOWN_OPERATION,
        CTR_ERRC_UNUSED_EVALUATION,
        CTR_ERRC_UNKNOWN,
    } tt;
    union {
        enum ctr_scan_errt scan_err;
        enum ctr_parse_errt parse_err;
    } inner;
    size_t line, column;
} ctr_compile_err;

#define EXPECTED_NAME ctr_compile_ex
#define EXPECTED_O ctr_proto
#define EXPECTED_E ctr_compile_err
#include <sf/containers/expected.h>
/// Compile a ctr_proto from source code.
EXPORT ctr_compile_ex ctr_cproto(sf_str src);

#endif // CTRC_H
