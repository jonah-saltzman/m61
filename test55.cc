#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>

// test realloc coalescing

int main() {
    void* realloced;
    void* ptrs[10];
    for (int i = 0; i != 10; ++i) {
        ptrs[i] = m61_malloc(100);
    }
    m61_free(ptrs[3]);
    m61_free(ptrs[4]);
    ptrs[3] = nullptr;
    ptrs[4] = nullptr;
    m61_print_heap();
    printf("reallocating %p\n", ptrs[5]);
    realloced = m61_realloc(ptrs[5], 150);
    ptrs[5] = nullptr;
    printf("after realloc: \n");
    m61_print_heap();
    for (int i = 0; i != 10; ++i) {
        if (ptrs[i] != nullptr)
            m61_free(ptrs[i]);
    }
    m61_free(realloced);
    m61_print_heap();
}

//! ???