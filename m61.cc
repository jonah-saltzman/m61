#include "m61.hh"
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cassert>

// only external objects are pointers and the statistics struct
// all metadata is internal
static m61_memory_buffer default_buffer;
static m61_statistics statistics = {0, 0, 0, 0, 0, 0, 0, 0, UINTPTR_MAX, 0};
static size_t* free_list_start;
static size_t* alloc_list_start;
static size_t* top_of_heap;
static size_t* end_of_heap;

m61_memory_buffer::m61_memory_buffer() {
    size_t* prologue_header;
    size_t* free_header;
    size_t* end_header;
    size_t free_header_size;
    size_t prologue_size;

    void* buf = mmap(nullptr,    // Place the buffer at a random address
        this->size,              // Buffer should be 8 MiB big
        PROT_WRITE,              // We want to read and write the buffer
        MAP_ANON | MAP_PRIVATE, -1, 0);
                                 // We want memory freshly allocated by the OS
    assert(buf != MAP_FAILED);
    this->buffer = (char*) buf;

    // create three initial blocks (see test52 for assertions re: heap initialization)
    // prologue (allocated)
    prologue_header = (size_t*)this->buffer + 1; // for alignment reasons
    prologue_size = WORD_SIZE * 2;
    m61_set_header_and_footer(prologue_header, prologue_size, ALLOC_BIT | PREV_ALLOC_BIT);

    // initial free block (free)
    free_header = NEXT_FROM_HEADER(prologue_header);
    free_header_size = this->size - GET_SIZE(prologue_header) - WORD_SIZE * 2;
    m61_set_header_and_footer(free_header, free_header_size, PREV_ALLOC_BIT | NEXT_ALLOC_BIT);

    // free block list pointers
    SET_LIST_NEXT(free_header, nullptr);
    SET_LIST_PREV(free_header, nullptr);
    
    // allocated epilogue (no magic number)
    end_header = NEXT_FROM_HEADER(free_header);
    *end_header = 0;
    *end_header |= (ALLOC_BIT | NEXT_ALLOC_BIT);

    // initialize free & allocated lists
    // see test58 for validation of list structures
    free_list_start = free_header;
    alloc_list_start = nullptr;

    // keep track of where the heap starts & ends
    top_of_heap = prologue_header;
    end_of_heap = end_header;
}

m61_memory_buffer::~m61_memory_buffer() {
    munmap(this->buffer, this->size);
}

/// m61_malloc(sz, file, line)
///    Returns a pointer to `sz` bytes of freshly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc may
///    return either `nullptr` or a pointer to a unique allocation.
///    The allocation request was made at source code location `file`:`line`.

void* m61_malloc(size_t sz, const char* file, int line) {
    size_t asize;
    size_t* header;

    if (sz == 0)
        return nullptr;

    // detect unsigned integer overflow
    if (sz > SIZE_MAX - (ALIGNMENT + ALLOC_META_SIZE)) {
        goto fail;
    }

    asize = m61_get_adjusted_size(sz);

    if ((header = m61_find_fit(asize)) != nullptr) {
        assert(!IS_ALLOC(header));  // we are allocating a free block
        m61_place(header, asize);
        assert(IS_ALLOC(header));   // the free block has been allocated
        m61_set_alloc_metadata(header, sz, file, line);
        m61_record_malloc(header, sz);
        return GET_PAYLOAD(header);
    }

fail:
    statistics.nfail += 1;
    statistics.fail_size += sz;
    return nullptr;
}

/// m61_get_adjusted_size(sz)
///    Return the size of a block needed to allocate sz bytes
size_t m61_get_adjusted_size(size_t sz) {
    size_t asize;

    if (sz <= MIN_PAYLOAD)
        asize = MIN_BLOCK;
    else {
        asize = sz + ALLOC_META_SIZE;        // add room for allocated metadata
        asize = asize % ALIGNMENT == 0       // and align properly
            ? asize
            : asize + ALIGNMENT - (asize % ALIGNMENT);
    }

    assert(asize % ALIGNMENT == 0);          // we've calculated the alignment properly
    return asize;
}

/// m61_find_fit(asize)
///    Return a pointer to `asize` bytes by traversing the explicit
///    free list. Returns nullptr if no suitable free block found.
size_t* m61_find_fit(size_t asize) {
    size_t* header = free_list_start;

    while (header != nullptr) {
        if (GET_SIZE(header) >= asize)
            return header;
        header = *LIST_NEXT(header);
    }
    return nullptr;
}

/// m61_place(header, asize)
///    Places a block of `asize` bytes in the free block with header
///    `header`, splitting the remainder into a new free block if
///    it exceeds the minimum block size
void m61_place(size_t* header, size_t asize) {
    size_t** prev_free = LIST_PREV(header);
    size_t** next_free = LIST_NEXT(header);
    size_t* new_free_header;
    size_t new_free_size;

    m61_unstitch_list(prev_free, next_free, &free_list_start);    // remove the block from the free list

    if (GET_SIZE(header) - asize >= MIN_BLOCK) {       // split the block

        // create new free block
        new_free_size = GET_SIZE(header) - asize;
        new_free_header = INCREMENT_SIZE_T_PTR(header, asize);
        m61_set_header_and_footer(new_free_header, new_free_size, PREV_ALLOC_BIT | NEXT_ALLOC_BIT);
        m61_push_to_front(new_free_header, free_list_start);

        // create allocated block
        m61_set_header_and_footer(header, asize, ALLOC_BIT | PREV_ALLOC_BIT);

    } else {                                           // don't split

        m61_set_header_and_footer(header, GET_SIZE(header), ALLOC_BIT | PREV_ALLOC_BIT | NEXT_ALLOC_BIT);
        TOGGLE_NEXT_BITS(header, PREV_ALLOC_BIT);      // let the next block know

    }

    TOGGLE_PREV_BITS(header, NEXT_ALLOC_BIT);          // let the prev block know
    m61_push_to_front(header, alloc_list_start);       // push to front of alloc list
}

/// m61_set_alloc_metadata(header, sz, file, line)
///    Store the metadata for an allocated block in the block, including
///    requested size `sz` and file and line number of the allocation.
void m61_set_alloc_metadata(size_t* header, unsigned int sz, const char* file, int line) {
    SET_REQ_SIZE(header, sz);
    set_footer_magic_number(header, sz);
    SET_LINE_NUMBER(header, line);
    SET_FILENAME(header, file);
    SET_HEADER_ADDR(header, header);
}

/// m61_record_malloc(sz)
///    Records a successful allocation of `sz` bytes in the
///    statistics object
void m61_record_malloc(size_t* header, size_t sz) {

    void* payload = GET_PAYLOAD(header);

    // increment counters
    statistics.ntotal += 1;
    statistics.nactive += 1;
    statistics.active_size += sz;
    statistics.total_size += sz;

    // update heap min & max
    statistics.heap_max = (uintptr_t)payload + sz > statistics.heap_max ? (uintptr_t)payload + sz : statistics.heap_max;
    statistics.heap_min = (uintptr_t)payload < statistics.heap_min ? (uintptr_t)payload : statistics.heap_min;
}

/// m61_free(ptr, file, line)
///    Frees the memory allocation pointed to by `ptr`. If `ptr == nullptr`,
///    does nothing. Otherwise, `ptr` must point to a currently active
///    allocation returned by `m61_malloc`. The free was called at location
///    `file`:`line`.

void m61_free(void* ptr, const char* file, int line) {
    size_t* header;
    size_t requested_size;

    if (ptr == nullptr)
        return;
    if (m61_validate_free(ptr, file, line)) {
        header = GET_HEADER_FROM_PAYLOAD(ptr);
        assert(IS_ALLOC(header));    // we are freeing an allocated block
        m61_unstitch_list(LIST_PREV(header), LIST_NEXT(header), &alloc_list_start);   // remove from allocated list
        requested_size = *REQ_SIZE_FROM_HEADER(header);;
        header = m61_coalesce(header);
        assert(!IS_ALLOC(header));   // the allocated block has been freed
        m61_record_free(requested_size);
    }
}

/// m61_validate_free(ptr, file, line)
///    Validate a particular free request. Checks for double frees,
///    wild frees, wild writes, and buffer overflows.
bool m61_validate_free(void* ptr, const char* file, int line) {
    const char* container_file;
    uintptr_t ptr_val = (uintptr_t) ptr;
    size_t* header = GET_HEADER_FROM_PAYLOAD(ptr);
    size_t* footer;
    size_t* container;
    size_t diff;
    unsigned int container_line;
    unsigned int container_size;

    // not in heap
    if (ptr_val < statistics.heap_min || ptr_val > statistics.heap_max) {
        fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, not in heap\n", file, line, ptr);
        return false;
    }

    // we never would have allocated a misaligned block
    if (ptr_val % alignof(std::max_align_t) != 0) {
        fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
        return false;
    }

    // if the block *looks* like a free block...
    if (!IS_ALLOC(header)) {

        if (m61_is_free_block(header)) {                // check if it's a *real* free block
            fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, double free\n", file, line, ptr);
            return false;
        }
        
        footer = INCREMENT_SIZE_T_PTR(header, GET_SIZE(header)); // this is where the footer should be
        if (HEADER_FROM_FOOTER(footer) != header) {     // check if the footer points to the header

            fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
            if ((container = m61_contains_ptr(ptr)) != nullptr) {   // check if ptr is inside an allocated block

                diff = ptr_val - (uintptr_t)GET_PAYLOAD(container);
                container_file = *GET_FILENAME(container);
                container_line = *GET_LINE_NUMBER(container);
                container_size = *REQ_SIZE_FROM_HEADER(container);
                fprintf(stderr, "\t%s:%d: %p is %zu bytes inside a %d byte region allocated here\n", container_file, container_line, ptr, diff, container_size);

            }
            return false;

        }

        assert(false);   // a "free" block must fall into one of the two above categories

    // if the block doesn't look like a free block...
    } else {

        if (!check_footer_magic_number(header, *REQ_SIZE_FROM_HEADER(header))) { // check for buffer overrun
            fprintf(stderr, "MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n", file, line, ptr);
            return false;
        }

        if (header != *GET_HEADER_ADDR(header)) { // check that the block is where the block thinks it is
            fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
            return false;
        }

        if (!m61_validate_block_ptrs(header)) {   // check in constant time that the block is actually in the allocated list 
            fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
            return false;
        }

    }

    return true;
}

/// m61_is_free_block(header)
///    Determines if a header represents a valid free block by 
///    checking that the footer matches the header, the size is not 0,
///    the footer is also marked free, and the header & footer sizes match
bool m61_is_free_block(size_t* header) {
    size_t* footer = header + (GET_SIZE(header) / WORD_SIZE) - 1;          // get the footer manually to avoid scary dereferences
    if (HEADER_FROM_FOOTER(footer) == header && (GET_SIZE(header) != 0)) { // if the footer points to the header...
        if (!IS_ALLOC(footer) && (GET_SIZE(footer) == GET_SIZE(header)))   // and both are free & match, it's a free block
            return true;
    }
    return false;
}

/// m61_validate_block_ptrs(header)
///    Validates that a block is a member of the list it thinks it's in
///    by checking that the previous and next blocks point back to `header`
bool m61_validate_block_ptrs(size_t* header) {
    size_t** next = LIST_NEXT(header);
    size_t** prev = LIST_PREV(header);
    if (*next != nullptr && *LIST_PREV(*next) != header)
        return false;
    if (*prev != nullptr && *LIST_NEXT(*prev) != header)
        return false;
    return true;
}

/// m61_contains_ptr(ptr)
///    Determines if any allocated block contains `ptr`, by traversing the
///    allocated list, returning the block if so, otherwise nullptr
size_t* m61_contains_ptr(void* ptr) {
    uintptr_t ptr_int = (uintptr_t) ptr;
    size_t* block = alloc_list_start;
    size_t req_size;
    void* payload;
    while (block != nullptr) {
        req_size = *REQ_SIZE_FROM_HEADER(block);
        payload = GET_PAYLOAD(block);
        if (ptr_int > (uintptr_t)payload && ptr_int < (uintptr_t)((char*)payload + req_size)) {
            return block;
        }
        block = *LIST_NEXT(block);
    }
    return nullptr;
}

/// m61_coalesce(header)
///    Determines whether the block indicated by `header` can be coalesced
///    up, down, or both, and calls appropriate coalescing functions as necessary.
size_t* m61_coalesce(size_t* header) {
    bool prev_alloc = IS_PREV_ALLOC(header);
    bool next_alloc = IS_NEXT_ALLOC(header);
    size_t size = GET_SIZE(header);
    size_t* prev = PREV_FROM_HEADER(header);
    size_t* next = NEXT_FROM_HEADER(header);

    if (prev_alloc && next_alloc) {             // can't coalesce
        m61_set_header_and_footer(header, size, PREV_ALLOC_BIT | NEXT_ALLOC_BIT);
        TOGGLE_NEXT_BITS(header, PREV_ALLOC_BIT);
        TOGGLE_PREV_BITS(header, NEXT_ALLOC_BIT);
    }

    else if (prev_alloc && !next_alloc) {       // coalesce with next block
        header = m61_coalesce_next(header, next);
        TOGGLE_PREV_BITS(header, NEXT_ALLOC_BIT);
    }

    else if (!prev_alloc && next_alloc) {       // coalesce with prev block
        header = m61_coalesce_prev(header, prev);
        TOGGLE_NEXT_BITS(header, PREV_ALLOC_BIT);
    }
    
    else {                                      // coalesce in both directions
        header = m61_coalesce_prev(header, prev);
        header = m61_coalesce_next(header, next);
    }

    m61_push_to_front(header, free_list_start);

    return header;
}

/// m61_coalesce_next(header, next)
///    Coalesce the given block `header` with the next free block `next`
///    and remove `next` from the free list
size_t* m61_coalesce_next(size_t* header, size_t* next) {
    size_t size = GET_SIZE(header) + GET_SIZE(next);
    size_t** prev_free = LIST_PREV(next);
    size_t** next_free = LIST_NEXT(next);

    m61_set_header_and_footer(header, size, PREV_ALLOC_BIT | NEXT_ALLOC_BIT);
    m61_unstitch_list(prev_free, next_free, &free_list_start);
    return header;
}

/// m61_coalesce_prev(header, prev)
///    Coalesce the given block `header` with the previous free block `prev`
///    and remove `prev` from the free list
size_t* m61_coalesce_prev(size_t* header, size_t* prev) {
    size_t size = GET_SIZE(header) + GET_SIZE(prev);
    size_t** prev_free = LIST_PREV(prev);
    size_t** next_free = LIST_NEXT(prev);

    m61_set_header_and_footer(prev, size, PREV_ALLOC_BIT | NEXT_ALLOC_BIT);
    m61_unstitch_list(prev_free, next_free, &free_list_start);
    return prev;
}

/// m61_record_free(sz)
///    Records a successful free of `sz` bytes in the
///    statistics object
void m61_record_free(size_t sz) {
    statistics.nfree += 1;
    statistics.nactive -= 1;
    statistics.active_size -= sz;
    statistics.freed_size += sz;
}

/// m61_set_header_and_footer(old_header, size, bites)
///    Given a header, set the header to new size `size` with `bits`
///    stored in the 3 least significant bits of the header, and copy
///    the new header to the end of the block
void m61_set_header_and_footer(size_t* header, size_t size, char bits) {
    size_t* footer;

    *header = (size | bits);
    footer = FOOTER_FROM_HEADER(header);
    *footer = *header;
}

/// m61_unstitch_list
///    Helper function to remove a block from the given list by connecting
///    its next and previous blocks to each other. 
void m61_unstitch_list(size_t** prev, size_t** next, size_t** list) {

    // assert that we are removing the type of block that we think we are
    if (*list == alloc_list_start)
        assert((*prev == nullptr || IS_ALLOC(*prev)) && (*next == nullptr || IS_ALLOC(*next)));
    else
        assert((*prev == nullptr || !IS_ALLOC(*prev)) && (*next == nullptr || !IS_ALLOC(*next)));

    if (*prev == nullptr && *next != nullptr) {      // block was start of list
        SET_LIST_PREV(*next, nullptr);
        *list = *next;
    } 
    
    else if (*prev != nullptr && *next == nullptr) { // block was end of list
        SET_LIST_NEXT(*prev, nullptr);
    } 
    
    else if (*prev == nullptr && *next == nullptr) { // block was last block
        *list = nullptr;
    } 
    
    else {                                           // regular case
        SET_LIST_NEXT(*prev, *next);
        SET_LIST_PREV(*next, *prev);
    }
}

/// m61_push_to_front
///    Push block `header` to the front of list `list`
void m61_push_to_front(size_t* header, size_t* list) {
    size_t* prev_front = list;

    assert(header != list);                // we are not double-pushing a block

    if (list == free_list_start)
        assert(!IS_ALLOC(header));         // only push free blocks to free list
    else
        assert(IS_ALLOC(header));          // only push alloc blocks to alloc list

    if (prev_front == nullptr) {
        SET_LIST_NEXT(header, nullptr);     // because list pointers are stored at the same offset
    } else {                                // from the footer, we can manipulate list pointers of
        SET_LIST_NEXT(header, prev_front);  // allocated & free blocks interchangeably
        SET_LIST_PREV(prev_front, header);
    }

    SET_LIST_PREV(header, nullptr);
    
    if (list == free_list_start) {
        free_list_start = header;
    } else {
        alloc_list_start = header;
    }
}

/// set_footer_magic_number(header, sz)
///    Places the magic number immediately following the region
///    allocated to the user for a given block (header + 2 words + payload size).
///
///    Footer magic number must be set byte-by-byte so the sanitizer
///    is not triggered by setting a size_t from at an invalidly-aligned address.
inline void set_footer_magic_number(size_t* header, size_t sz) {
    char* footer_magic_number = (char*)(header + 1) + sz;
    for (size_t i = 0; i < 8; i++) {
        footer_magic_number[i] = magic_number[i];
    }
}

/// check_footer_magic_number(header, sz)
///    Checks the magic number immediately following the region
///    allocated to the user for a given block (header + 2 words + payload size).
///
///    Footer magic number must be read byte-by-byte so the sanitizer
///    is not triggered by reading a size_t from an invalidly-aligned address.
inline bool check_footer_magic_number(size_t* header, size_t sz) {
    char* footer_magic_number = (char*)(header + 1) + sz;
    for (size_t i = 0; i < 8; i++) {
        if (footer_magic_number[i] != magic_number[i])
            return false;
    }
    return true;
}


/// m61_calloc(count, sz, file, line)
///    Returns a pointer a fresh dynamic memory allocation big enough to
///    hold an array of `count` elements of `sz` bytes each. Returned
///    memory is initialized to zero. The allocation request was at
///    location `file`:`line`. Returns `nullptr` if out of memory; may
///    also return `nullptr` if `count == 0` or `size == 0`.
void* m61_calloc(size_t count, size_t sz, const char* file, int line) {
    void* ptr;

    if (count == 0)
        return nullptr;

    // detect unsigned overflow
    if (sz > SIZE_MAX / count) {
        statistics.nfail += 1;
        statistics.fail_size += sz;
        return nullptr;
    }

    ptr = m61_malloc(count * sz, file, line);
    if (ptr) {
        memset(ptr, 0, count * sz);
    }
    return ptr;
}

/// m61_realloc(ptr, sz, file, line)
///    Changes the size of the dynamic allocation pointed to by `ptr`
///    to hold at least `sz` bytes. If the existing allocation cannot be
///    enlarged, this function makes a new allocation, copies as much data
///    as possible from the old allocation to the new, and returns a pointer
///    to the new allocation. If `ptr` is `nullptr`, behaves like
///    `m61_malloc(sz, file, line). `sz` must not be 0. If a required
///    allocation fails, returns `nullptr` without freeing the original
///    block.
void* m61_realloc(void* ptr, size_t new_size, const char* file, int line) {
    void* new_payload = nullptr;
    size_t* old_header;
    size_t* new_free_header;
    size_t* new_header;
    size_t* prev;
    size_t* next;
    size_t prev_avail;
    size_t next_avail;
    size_t old_size;
    size_t old_req_size;
    size_t asize;
    size_t new_free_size;
    unsigned int new_bits;
    unsigned int next_new_bits;
    bool is_next_alloc;

    if (ptr == nullptr)
        return m61_malloc(new_size, file, line);

    if (new_size == 0)
        return nullptr;

    // detect unsigned integer overflow
    if (new_size > SIZE_MAX - ALIGNMENT) {
        goto fail;
    }

    asize = m61_get_adjusted_size(new_size);
    
    if (m61_validate_free(ptr, file, line)) {

        old_header = GET_HEADER_FROM_PAYLOAD(ptr);
        old_req_size = *REQ_SIZE_FROM_HEADER(old_header);
        old_size = GET_SIZE(old_header);
        prev = PREV_FROM_HEADER(old_header);
        next = NEXT_FROM_HEADER(old_header);
        prev_avail = (IS_PREV_ALLOC(old_header) ? 0 : GET_SIZE(prev));
        next_avail = (IS_NEXT_ALLOC(old_header) ? 0 : GET_SIZE(next));

        if (new_size > old_req_size) {  // expanding the allocation

            // sufficient space by coalescing with previous block
            if ((prev_avail + old_size >= asize) && (prev_avail - (asize - old_size) >= MIN_BLOCK)) {

                m61_unstitch_list(LIST_PREV(old_header), LIST_NEXT(old_header), &alloc_list_start); // remove alloc block from alloc list
                new_free_size = prev_avail - (asize - old_size);
                next_new_bits = GET_BITS(prev); // save the bits of the previous block for re-creating it
                new_bits = ALLOC_BIT;
                if (IS_ALLOC(next))
                    new_bits |= NEXT_ALLOC_BIT;
                new_free_header = m61_coalesce_prev(old_header, prev);  // shrink free block
                m61_set_header_and_footer(new_free_header, new_free_size, next_new_bits);
                new_header = NEXT_FROM_HEADER(new_free_header);        
                m61_set_header_and_footer(new_header, asize, new_bits); // create new alloc block
                new_payload = GET_PAYLOAD(new_header);
                memcpy(new_payload, ptr, old_req_size);                 // copy the data
                m61_push_to_front(new_free_header, free_list_start);    // push shrunken free block to front of free list

            // sufficient space by coalescing with next block
            } else if ((next_avail + old_size >= asize) && (next_avail - (asize - old_size) >= MIN_BLOCK)) {

                m61_unstitch_list(LIST_PREV(old_header), LIST_NEXT(old_header), &alloc_list_start); // remove alloc block from alloc list
                new_free_size = next_avail - (asize - old_size);
                new_bits = ALLOC_BIT;
                next_new_bits = GET_BITS(next); // save the bits of the next block for re-creating it
                if (IS_ALLOC(prev))
                    new_bits |= PREV_ALLOC_BIT;
                new_header = m61_coalesce_next(old_header, next);       // shrink free block
                m61_set_header_and_footer(new_header, asize, new_bits); // create new alloc block
                m61_set_header_and_footer(NEXT_FROM_HEADER(new_header), new_free_size, next_new_bits);
                new_payload = GET_PAYLOAD(new_header);
                m61_push_to_front(NEXT_FROM_HEADER(new_header), free_list_start); // push shrunken free block to front of free list
                // memcpy is unnecessary

            // sufficient space by using the entire previous block and some of next block
            } else if ((prev_avail + next_avail + old_size >= asize) && (next_avail - (asize - old_size - prev_avail) >= MIN_BLOCK)) {

                m61_unstitch_list(LIST_PREV(old_header), LIST_NEXT(old_header), &alloc_list_start); // remove alloc block from alloc list
                new_free_size = next_avail - (asize - old_size - prev_avail);
                new_bits = ALLOC_BIT | PREV_ALLOC_BIT;
                next_new_bits = GET_BITS(next);                         // save the bits of next block to re-create
                new_header = m61_coalesce_prev(old_header, prev);       // coalesce the block in both directions
                new_header = m61_coalesce_next(new_header, next);
                m61_set_header_and_footer(new_header, asize, new_bits); // create new allocated block
                m61_set_header_and_footer(NEXT_FROM_HEADER(new_header), new_free_size, next_new_bits); // new free block
                new_payload = GET_PAYLOAD(new_header);
                memcpy(new_payload, ptr, old_req_size);                 // copy the data
                TOGGLE_PREV_BITS(new_header, NEXT_ALLOC_BIT);           // tell the previous block that the next block is now allocated
                m61_push_to_front(NEXT_FROM_HEADER(new_header), free_list_start);

            // no space for the new block here, just free and make a new allocation
            } else {

                new_payload = m61_malloc(new_size, file, line);
                memcpy(new_payload, ptr, old_req_size);
                m61_free(ptr, file, line);
                return new_payload;
                
            }

            m61_record_free(old_req_size);

        } else if (new_size < old_size) { // contracting the allocation

            // if we shrink the block enough to create a new block, create the new free block
            if (old_size - asize >= MIN_BLOCK) {

                is_next_alloc = IS_ALLOC(next);
                new_free_size = old_size - asize;
                new_bits = ALLOC_BIT;
                if (IS_ALLOC(prev))
                    new_bits |= PREV_ALLOC_BIT;
                m61_set_header_and_footer(old_header, asize, new_bits);
                next = NEXT_FROM_HEADER(old_header);
                next_new_bits = PREV_ALLOC_BIT;
                if (is_next_alloc)
                    next_new_bits |= NEXT_ALLOC_BIT;
                m61_set_header_and_footer(next, new_free_size, next_new_bits);
                if (!is_next_alloc) {
                    m61_coalesce(next);
                }
                m61_set_header_and_footer(old_header, asize, new_bits);

                // this block is already in the allocated list, 
                // so we do the bookkeeping and return early
                m61_set_alloc_metadata(GET_HEADER_FROM_PAYLOAD(ptr), new_size, file, line);
                m61_record_free(new_free_size);
                return ptr;

            }

            // otherwise, do nothing
            return ptr;

        // new_size was identical to existing size
        } else {
            return ptr;
        }
    } else {
        return nullptr; // on failure, returns nullptr; original ptr should remain valid
    }

    // the pointer has been assigned previously in the function
    assert(new_payload != nullptr);

    m61_set_alloc_metadata(GET_HEADER_FROM_PAYLOAD(new_payload), new_size, file, line);
    m61_push_to_front(GET_HEADER_FROM_PAYLOAD(new_payload), alloc_list_start);
    m61_record_malloc(GET_HEADER_FROM_PAYLOAD(new_payload), new_size);

    // the new block we created is of the correct size
    assert(*REQ_SIZE_FROM_PAYLOAD(new_payload) == new_size);

    return new_payload;

fail:
    statistics.nfail += 1;
    statistics.fail_size += new_size;
    return nullptr;
}

/// m61_print_heap()
///    Print all blocks in the heap, including prologue and epilogue
void m61_print_heap() {
    size_t* block = top_of_heap;
    size_t* header;
    size_t* footer;
    size_t size;
    bool alloc;
    bool prev_alloc;
    bool next_alloc;
    int count = 0;

    block = NEXT_FROM_HEADER(block); // skip the prologue
    DEBUG_PRINT("================================================%c\n", 0);
    while (true) {
        header = block;
        footer = FOOTER_FROM_HEADER(header);
        size = GET_SIZE(header);
        alloc = IS_ALLOC(header);
        prev_alloc = IS_PREV_ALLOC(header);
        next_alloc = IS_NEXT_ALLOC(header);
        if (size == 0 && IS_ALLOC(header))
            break;
        DEBUG_PRINT("block %d: %s\nheader: %p footer: %p\nsize: %zu\nprev: %s\nnext: %s\n----------\n", count, (alloc ? "ALLOC" : "FREE"), header, footer, size, (prev_alloc ? "ALLOC" : "FREE"), (next_alloc ? "ALLOC" : "FREE"));
        ++count;
        block = NEXT_FROM_HEADER(header);
    }
    DEBUG_PRINT("================================================%c\n", 0);
}

/// m61_print_list()
///    Print all blocks in the free list
void m61_print_list(size_t* start) {
    size_t* node = start;
    size_t* header;
    size_t* footer;
    size_t* list_prev;
    size_t* list_next;
    size_t size;
    bool alloc;
    int count = 0;
    bool is_free_list = start == free_list_start;
    const char* title = is_free_list ? "FREE" : "ALLOC";

    DEBUG_PRINT("\033[0;%dm====================%s LIST=======================%c\n", is_free_list ? 32 : 31, title, 0);
    while (node != nullptr) {
        header = node;
        footer = FOOTER_FROM_HEADER(header);
        size = GET_SIZE(header);
        alloc = IS_ALLOC(header);
        list_prev = *LIST_PREV(header);
        list_next = *LIST_NEXT(header);
        DEBUG_PRINT("block %d: %s\nheader: %p footer: %p\nsize(h): %zu\nlist_prev: %p\nlist_next: %p\n", count, (alloc ? "ALLOC" : "FREE"), header, footer, size, list_prev, list_next);
        DEBUG_HEXDUMP(FOOTER_FROM_HEADER(header) - 11, 12 * WORD_SIZE);
        DEBUG_PRINT("----------%c\n", 0);
        ++count;
        node = *LIST_NEXT(node);
        if (count > 50)
            break;
    }
    DEBUG_PRINT("=======================%s LIST====================\033[0m%c\n", title, 0);
}

/// m61_print_block()
///    Prints one block
void m61_print_block(size_t* header, const char* message) {
    if (header == nullptr) {
        DEBUG_PRINT("%s: %p\n", message, header);
        return;
    }
    bool alloc = IS_ALLOC(header);
    DEBUG_PRINT("%s\n", message);
    DEBUG_PRINT("\033[0;32m------------%c\n", 0);
    DEBUG_PRINT("block %p: %s\nsize: %zu\nfooter: %p (%zu)\n", header, (alloc ? "ALLOC" : "FREE"), GET_SIZE(header), FOOTER_FROM_HEADER(header), GET_SIZE(FOOTER_FROM_HEADER(header)));
    if (!alloc) {
        DEBUG_PRINT("list_prev: %p\nlist_next: %p\n", *LIST_PREV(header), *LIST_NEXT(header));
    } else {
        DEBUG_PRINT("alloc_list_prev: %p\nalloc_list_next: %p\n", *LIST_PREV(header), *LIST_NEXT(header));
    }
    DEBUG_PRINT("------------\033[0m%c\n", 0);
}

/// m61_validate_list(list)
///    Validates that all members of a list are of the right type (alloc/free)
void m61_validate_list(size_t* list, const char* message) {
    size_t* node = list;
    while (node != nullptr) {
        if (list == free_list_start && IS_ALLOC(node)) {
            DEBUG_PRINT("invalid free list: %s\n", message);
            m61_print_block(node, "due to block:");
            abort();
        }
        else if (list == alloc_list_start && !IS_ALLOC(node)) {
            DEBUG_PRINT("invalid alloc list: %s\n", message);
            m61_print_block(node, "due to block:");
            abort();
        }
        if (*LIST_NEXT(node) == node || *LIST_PREV(node) == node) {
            DEBUG_PRINT("%s\n", message);
            m61_print_block(node, "block points to itself");
            abort();
        }
        node = *LIST_NEXT(node);
    }
}

/// m61_get_statistics()
///    Return the current memory statistics.
m61_statistics m61_get_statistics() {
    return statistics;
}

/// m61_get_memory_buffer()
///    Get a pointer to the memory buffer
///    For testing purposes only
m61_memory_buffer* m61_get_memory_buffer() {
    return &default_buffer;
}

/// m61_get_free_list()
///    Get a pointer to the free list
///    For testing purposes only
size_t* m61_get_free_list() {
    return free_list_start;
}

/// m61_get_alloc_list()
///    Get a pointer to the free list
///    For testing purposes only
size_t* m61_get_alloc_list() {
    return alloc_list_start;
}

/// m61_print_statistics()
///    Prints the current memory statistics.
void m61_print_statistics() {
    m61_statistics stats = m61_get_statistics();
    printf("alloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("alloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}


/// m61_print_leak_report()
///    Prints a report of all currently-active allocated blocks of dynamic
///    memory.
void m61_print_leak_report() {
    size_t** block = &alloc_list_start;
    const char* file;
    unsigned int line;
    unsigned int size;
    void* payload;
    while (*block != nullptr) {
        file = *GET_FILENAME(*block);
        line = *GET_LINE_NUMBER(*block);
        size = *REQ_SIZE_FROM_HEADER(*block);
        payload = GET_PAYLOAD(*block);
        fprintf(stdout, "LEAK CHECK: %s:%d: allocated object %p with size %d\n", file, line, payload, size);
        block = LIST_NEXT(*block);
    }
}
