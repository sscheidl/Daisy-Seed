#include "heap_bounds.h"
#include <errno.h>
extern uint8_t __neb_heap_start[],__neb_heap_end[];
void* _sbrk(ptrdiff_t amount) {
    static uintptr_t cursor;
    const uintptr_t start=(uintptr_t)__neb_heap_start,end=(uintptr_t)__neb_heap_end;
    if(!cursor) cursor=start;
    uintptr_t next;
    if(!neb_heap_adjust(start,end,cursor,amount,&next)) {errno=ENOMEM;return (void*)-1;}
    void* previous=(void*)cursor;cursor=next;return previous;
}
