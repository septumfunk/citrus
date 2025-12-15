#ifndef VM_H
#define VM_H

#include "bytecode.h"

typedef struct {
    ctr_valvec stack;
    ctr_protovec protos;
} ctr_state;

EXPORT ctr_state *ctr_state_new(void);
EXPORT void ctr_state_free(ctr_state *state);

EXPORT sf_str ctr_tostring(ctr_val val);
EXPORT sf_str ctr_stackdump(ctr_state *state);

static inline ctr_val ctr_get(ctr_state *state, size_t index) {
    return ctr_valvec_get(&state->stack, index);
}
static inline void ctr_set(ctr_state *state, size_t index, ctr_val val) {
    ctr_val old = ctr_get(state, index);
    if (old.tt == CTR_TDYN)
        ctr_ddel(old.val.dyn);
    ctr_valvec_set(&state->stack, index, val);
}

EXPORT ctr_val ctr_run(ctr_state *state, ctr_proto *proto);

#endif // VM_H
