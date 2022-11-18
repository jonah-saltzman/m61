#include "m61.hh"
#include "hexdump.hh"
#include <cstdio>
#include <cassert>
#include <cstring>

// test m61_realloc basics

int main() {
    void* ptr1;
    void* ptr2;
    void* ptr3;
    // check expansion
    ptr1 = m61_malloc(10);
    memset(ptr1, 'B', 10);
    ptr2 = m61_realloc(ptr1, 20);
    memset(ptr2, 'A', 20);
    m61_free(ptr2);

    // check contration
    ptr1 = m61_malloc(200);
    m61_print_heap();
    ptr2 = m61_realloc(ptr1, 100);
    m61_print_heap();
    memset(ptr2, 'A', 100);
    m61_validate_list(m61_get_alloc_list(), "alloc right after contraction");
    m61_validate_list(m61_get_free_list(), "free right after contraction");
    m61_free(ptr2);
    m61_print_heap();

    m61_print_list(m61_get_alloc_list());

    m61_validate_list(m61_get_alloc_list(), "alloc after contraction");
    m61_validate_list(m61_get_free_list(), "free after contraction");

    // // check expansion with no adjacent free block
    ptr1 = m61_malloc(100);
    ptr2 = m61_malloc(100);
    ptr3 = m61_malloc(8 << 19);
    ptr2 = m61_realloc(ptr2, 500);
    memset(ptr2, 'A', 500);
    m61_free(ptr2);
    m61_print_heap();
    m61_free(ptr1);
    m61_validate_list(m61_get_alloc_list(), "alloc before free");
    m61_validate_list(m61_get_free_list(), "free before free");
    m61_print_heap();
    m61_free(ptr3);
}