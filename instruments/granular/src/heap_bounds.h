#pragma once
#include <stddef.h>
#include <stdint.h>
// Integer arithmetic avoids out-of-object pointer formation and signed-min negation.
static inline int neb_heap_adjust(uintptr_t start,uintptr_t end,uintptr_t current,
                                 ptrdiff_t amount,uintptr_t* next) {
    if(start>end || current<start || current>end) return 0;
    if(amount>=0) {
        if((uintptr_t)amount>end-current) return 0;
        *next=current+(uintptr_t)amount;
    } else {
        uintptr_t magnitude=(uintptr_t)(-(amount+1))+1;
        if(magnitude>current-start) return 0;
        *next=current-magnitude;
    }
    return 1;
}
