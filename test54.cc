#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>

// test many reallocs

int main() {
    void* ptr = m61_malloc(1);

    for (int i = 0; i != 1'000'000; ++i) {
        ptr = m61_realloc(ptr, i + 2);
    }

    m61_free(ptr);
}