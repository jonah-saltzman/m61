#include "m61.hh"
#include "hexdump.hh"
#include <cstdio>
#include <cassert>
#include <cstring>
// Check initialization of heap

int main() {
    m61_memory_buffer* buffer = m61_get_memory_buffer();
    size_t* free_list = m61_get_free_list();
    char* buf = buffer->buffer;
    size_t* prologue_header = ((size_t*) buf) + 1;
    size_t* prologue_footer = prologue_header + 1;
    size_t* free_header = prologue_footer + 1;
    size_t* free_footer = INCREMENT_SIZE_T_PTR(free_header, (GET_SIZE(free_header) - WORD_SIZE));
    size_t* epilogue_header = free_footer + 1;
    size_t* epilogue_header_from_free = INCREMENT_SIZE_T_PTR(free_header, GET_SIZE(free_header));
    size_t* free_header_from_epilogue = PREV_FROM_HEADER(epilogue_header);
    size_t* epilogue_header_from_macro = NEXT_FROM_HEADER(free_header);
    size_t* prologue_header_from_free = PREV_FROM_HEADER(free_header);
    size_t* free_header_from_prologue = NEXT_FROM_HEADER(prologue_header);

    size_t** free_next = LIST_NEXT(free_header);
    size_t** free_prev = LIST_PREV(free_header);

    assert(epilogue_header == epilogue_header_from_free);
    assert(free_header_from_epilogue == free_header);
    assert(epilogue_header_from_macro == epilogue_header);
    assert(prologue_header_from_free == prologue_header);
    assert(free_header_from_prologue == free_header);

    assert(free_list == free_header);
    assert(*free_next == nullptr);
    assert(*free_prev == nullptr);
    assert(GET_SIZE(free_header) == GET_SIZE(free_footer));
    assert(GET_SIZE(epilogue_header) == 0);
    assert(!IS_ALLOC(free_header));
    assert(!IS_ALLOC(free_footer));
    assert(IS_NEXT_ALLOC(free_header) && IS_PREV_ALLOC(free_header));
    assert(IS_NEXT_ALLOC(free_footer) && IS_PREV_ALLOC(free_footer));
    assert(!IS_PREV_ALLOC(epilogue_header));
    assert(IS_ALLOC(epilogue_header) && IS_NEXT_ALLOC(epilogue_header));
}
