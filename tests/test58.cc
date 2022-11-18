#include "m61.hh"
#include "hexdump.hh"
#include <cstdio>
#include <cassert>
#include <cstring>

// validate free / allocated linked lists

int main() {
    void* ptrs[10];
    size_t* free_node;
    size_t* alloc_node;
    int free_count = 0;
    int alloc_count = 0;

    for (int i = 0; i != 10; ++i) {
        ptrs[i] = m61_malloc(i + 1);
    }
    for (int i = 0; i != 5; ++i) {
        m61_free(ptrs[i]);
    }

    free_node = m61_get_free_list();
    alloc_node = m61_get_alloc_list();

    while (free_node != nullptr) {
        assert(!IS_ALLOC(free_node));
        free_node = *LIST_NEXT(free_node);
        ++free_count;
    }

    while (alloc_node != nullptr) {
        assert(IS_ALLOC(alloc_node));
        alloc_node = *LIST_NEXT(alloc_node);
        ++alloc_count;
    }

    assert(free_count == 2);
    assert(alloc_count == 5);
}