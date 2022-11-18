#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>

// realloc should copy memory

int main() {
    // expansion
    char* ptr1 = (char*)m61_malloc(100);
    memset(ptr1, 'A', 100);
    m61_malloc(100);         // put a block in between so it has to copy
    char* ptr2 = (char*)m61_realloc(ptr1, 200);
    
    for (int i = 0; i != 100; ++i) {
        assert(ptr2[i] == 'A');
    }

    m61_free(ptr2);

    // contraction
    ptr1 = (char*)m61_malloc(100);
    memset(ptr1, 'B', 100);
    ptr2 = (char*)m61_realloc(ptr1, 50);

    for (int i = 0; i != 50; ++i) {
        assert(ptr2[i] == 'B');
    }

    m61_free(ptr2);
}