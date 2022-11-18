#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>

// shouldn't be able to free previous pointer after realloc

int main() {
    void* ptr4;
    void* ptr1 = m61_malloc(100);
    void* ptr2 = m61_malloc(100);
    void* ptr3 = m61_malloc(8 << 19);
    ptr4 = m61_realloc(ptr2, 500);
    m61_free(ptr1);
    m61_free(ptr3);
    m61_free(ptr4);
    fprintf(stderr, "Will free %p\n", ptr2);
    m61_free(ptr2);
}

//! Will free ??{0x\w+}=ptr??
//! MEMORY BUG???: invalid free of pointer ??ptr??, not allocated
//! ???