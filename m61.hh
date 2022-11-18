#ifndef M61_HH
#define M61_HH 1
#include <cassert>
#include <cstdlib>
#include <cinttypes>
#include <cstdio>
#include <new>
#include <random>
#include <sys/mman.h>
#include "hexdump.hh"

#define DEBUG false
#define M61_ASSERT(x, y) if (!(x)) { printf("Assertion failed: %s\n", y); abort(); }
#define DEBUG_HEXDUMP(ptr, size) { if (DEBUG) { hexdump(ptr, size); } }
#define DEBUG_PRINT(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#define WORD_SIZE       sizeof(size_t) // 8
#define MIN_PAYLOAD     2 * WORD_SIZE
#define ALLOC_META_SIZE (WORD_SIZE * 8)
#define MIN_BLOCK       (ALLOC_META_SIZE + MIN_PAYLOAD)  
#define ALIGNMENT       alignof(std::max_align_t) // 16

#define ALLOC_BIT      0b010
#define NEXT_ALLOC_BIT 0b001
#define PREV_ALLOC_BIT 0b100

#define IS_ALLOC(header)       (((*header) & ALLOC_BIT) > 0)
#define IS_NEXT_ALLOC(header)  (((*header) & NEXT_ALLOC_BIT) > 0)
#define IS_PREV_ALLOC(header)  (((*header) & PREV_ALLOC_BIT) > 0)
#define GET_BITS(header)       ((*header) & (ALLOC_BIT | NEXT_ALLOC_BIT | PREV_ALLOC_BIT))

#define GET_SIZE(header) ((*header) & (~(ALLOC_BIT | NEXT_ALLOC_BIT | PREV_ALLOC_BIT)))
#define GET_HEADER_FROM_PAYLOAD(payload) ((size_t*)((char*)payload - WORD_SIZE))
#define GET_PAYLOAD(header) ((void*)(header + 1))
#define INCREMENT_SIZE_T_PTR(ptr, bytes) ((ptr) + ((bytes) / WORD_SIZE))
#define DECREMENT_SIZE_T_PTR(ptr, bytes) ((ptr) - ((bytes) / WORD_SIZE))

#define PREV_FROM_HEADER(header) (DECREMENT_SIZE_T_PTR(header, GET_SIZE((header - 1))))
#define NEXT_FROM_HEADER(header) (INCREMENT_SIZE_T_PTR(header, GET_SIZE(header)))
#define FOOTER_FROM_HEADER(header) (GET_SIZE(header) == 0 ? nullptr : (NEXT_FROM_HEADER(header) - 1))
#define HEADER_FROM_FOOTER(footer) (DECREMENT_SIZE_T_PTR(footer, (GET_SIZE(footer) - 1)))

#define TOGGLE_NEXT_BITS(header, bits) (*NEXT_FROM_HEADER(header) ^= bits)
#define TOGGLE_PREV_BITS(header, bits) (*PREV_FROM_HEADER(header) ^= bits)

#define LIST_PREV(header) ((size_t**)(FOOTER_FROM_HEADER(header) - 2))
#define LIST_NEXT(header) ((size_t**)(FOOTER_FROM_HEADER(header) - 1))
#define SET_LIST_PREV(header, ptr) (*LIST_PREV(header) = ptr)
#define SET_LIST_NEXT(header, ptr) (*LIST_NEXT(header) = ptr)

#define REQ_SIZE_FROM_HEADER(header) ((unsigned int*)(FOOTER_FROM_HEADER(header) - 5))
#define REQ_SIZE_FROM_PAYLOAD(payload) (REQ_SIZE_FROM_HEADER(GET_HEADER_FROM_PAYLOAD(payload)))
#define SET_REQ_SIZE(header, size) (*REQ_SIZE_FROM_HEADER(header) = size)
#define GET_LINE_NUMBER(header) (REQ_SIZE_FROM_HEADER(header) + 1)
#define SET_LINE_NUMBER(header, ln) (*GET_LINE_NUMBER(header) = ln)
#define GET_FILENAME(header) ((const char**)(FOOTER_FROM_HEADER(header) - 4))
#define SET_FILENAME(header, file) (*GET_FILENAME(header) = file)
#define GET_HEADER_ADDR(header) ((size_t**)FOOTER_FROM_HEADER(header) - 3)
#define SET_HEADER_ADDR(header, addr) (*GET_HEADER_ADDR(header) = addr)

constexpr char magic_number[8] = {0x6b, 0x69, 0x6d, 0x62, 0x6f, 0x72, 0x61, 0x21};

/// m61_malloc(sz, file, line)
///    Return a pointer to `sz` bytes of newly-allocated dynamic memory.
void* m61_malloc(size_t sz, const char* file = __builtin_FILE(), int line = __builtin_LINE());

/// m61_get_adjusted_size(sz)
///    Return the size of a block needed to allocate sz bytes
size_t m61_get_adjusted_size(size_t sz);

/// m61_find_fit(asize)
///    Return a pointer to `asize` bytes by traversing the explicit
///    free list. Returns nullptr if no suitable free block found.
size_t* m61_find_fit(size_t asize);

/// m61_place(header, asize)
///    Places a block of `asize` bytes in the free block with header
///    `header`, splitting the remainder into a new free block if
///    it exceeds the minimum block size
void m61_place(size_t* header, size_t asize);

/// m61_set_alloc_metadata(header, sz, file, line)
///    Store the metadata for an allocated block in the block, including
///    requested size `sz` and file and line number of the allocation.
void m61_set_alloc_metadata(size_t* header, unsigned int sz, const char* file, int line);

/// m61_record_malloc(sz)
///    Records a successful allocation of `sz` bytes
void m61_record_malloc(size_t* header, size_t sz);

/// m61_free(ptr, file, line)
///    Free the memory space pointed to by `ptr`.
void m61_free(void* ptr, const char* file = __builtin_FILE(), int line = __builtin_LINE());

/// m61_validate_free(ptr, file, line)
///    Validate a particular free request. Checks for double frees,
///    wild frees, wild writes, and buffer overflows.
bool m61_validate_free(void* ptr, const char* file, int line);

/// m61_is_free_block(header)
///    Determines if a header represents a valid free block by 
///    checking that the footer matches the header, the size is not 0,
///    the footer is also marked free, and the header & footer sizes match
bool m61_is_free_block(size_t* header);

/// m61_validate_block_ptrs(header)
///    Validates that a block is a member of the list it thinks it's in
///    by checking that the previous and next blocks point back to `header`
bool m61_validate_block_ptrs(size_t* header);

/// m61_contains_ptr(ptr)
///    Determines if any active or free block contains `ptr`, returning
///    the block if so, otherwise nullptr
size_t* m61_contains_ptr(void* ptr);

/// m61_coalesce(header)
///    Determines whether the block indicated by `header` can be coalesced
///    up, down, or both, and calls appropriate coalescing functions as necessary.
size_t* m61_coalesce(size_t* header);

/// m61_coalesce_next(header, next)
///    Coalesce the given block `header` with the next free block `next`
size_t* m61_coalesce_next(size_t* header, size_t* next);

/// m61_coalesce_prev(header, prev)
///    Coalesce the given block `header` with the previous free block `prev`
size_t* m61_coalesce_prev(size_t* header, size_t* prev);

/// m61_record_free(ptr)
///    Records a successful free of `sz` bytes
void m61_record_free(size_t sz);

/// m61_set_header_and_footer(old_header, size, bites)
///    Given a header, set the header to new size `size` with `bits`
///    stored in the 3 least significant bits of the header, and copy
///    the new header to the end of the block
void m61_set_header_and_footer(size_t* header, size_t size, char bits);

/// m61_unstitch_list
///    Helper function to remove a block from the free list by connecting
///    its next and previous blocks to each other
void m61_unstitch_list(size_t** prev, size_t** next, size_t** list);

/// m61_push_to_front
///    Push block `header` to the front of list `list`
void m61_push_to_front(size_t* header, size_t* list);

/// set_footer_magic_number(header, sz)
///    Places the magic number immediately following the region
///    allocated to the user for a given block (header + 2 words + payload size).
///
///    Footer magic number must be set byte-by-byte so the sanitizer
///    is not triggered by setting a size_t from at an invalidly-aligned address.
inline void set_footer_magic_number(size_t* header, size_t sz);

/// check_footer_magic_number(header, sz)
///    Checks the magic number immediately following the region
///    allocated to the user for a given block (header + 2 words + payload size).
///
///    Footer magic number must be read byte-by-byte so the sanitizer
///    is not triggered by reading a size_t from an invalidly-aligned address.
inline bool check_footer_magic_number(size_t* header, size_t sz);

/// m61_calloc(count, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `count` elements of `sz` bytes each. The memory
///    is initialized to zero.
void* m61_calloc(size_t count, size_t sz, const char* file = __builtin_FILE(), int line = __builtin_LINE());

/// m61_realloc(ptr, sz, file, line)
///    Changes the size of the dynamic allocation pointed to by `ptr`
///    to hold at least `sz` bytes. If the existing allocation cannot be
///    enlarged, this function makes a new allocation, copies as much data
///    as possible from the old allocation to the new, and returns a pointer
///    to the new allocation. If `ptr` is `nullptr`, behaves like
///    `m61_malloc(sz, file, line). `sz` must not be 0. If a required
///    allocation fails, returns `nullptr` without freeing the original
///    block.
void* m61_realloc(void* ptr, size_t new_size, const char* file = __builtin_FILE(), int line = __builtin_LINE());

/// m61_print_heap()
///    Print all blocks in the heap, including prologue and epilogue
void m61_print_heap();

/// m61_print_list(start)
///    Print all blocks in the free list
void m61_print_list(size_t* start);

/// m61_print_block(header, message)
///    Prints one block
void m61_print_block(size_t* header, const char* message);

/// m61_validate_list(list)
///    Validates that all members of a list are of the right type (alloc/free)
void m61_validate_list(size_t* list, const char* message);

/// m61_statistics
///    Structure tracking memory statistics.
struct m61_statistics {
    unsigned long long nactive;         // # active allocations
    unsigned long long active_size;     // # bytes in active allocations
    unsigned long long nfree;           // # successful frees
    unsigned long long freed_size;      // # bytes of total successful frees
    unsigned long long ntotal;          // # total allocations
    unsigned long long total_size;      // # bytes in total allocations
    unsigned long long nfail;           // # failed allocation attempts
    unsigned long long fail_size;       // # bytes in failed alloc attempts
    uintptr_t heap_min;                 // smallest allocated addr
    uintptr_t heap_max;                 // largest allocated addr
};

struct m61_memory_buffer {
    char* buffer;
    size_t pos = 0;
    size_t size = 8 << 20; /* 8 MiB */

    m61_memory_buffer();
    ~m61_memory_buffer();
};

/// m61_get_statistics()
///    Return the current memory statistics.
m61_statistics m61_get_statistics();

/// m61_print_statistics()
///    Print the current memory statistics.
void m61_print_statistics();

/// m61_print_leak_report()
///    Print a report of all currently-active allocated blocks of dynamic
///    memory.
void m61_print_leak_report();

/// m61_get_memory_buffer()
///    Get a pointer to the memory buffer
///    For testing purposes only
m61_memory_buffer* m61_get_memory_buffer();

/// m61_get_free_list()
///    Get a pointer to the free list
///    For testing purposes only
size_t* m61_get_free_list();

/// m61_get_alloc_list()
///    Get a pointer to the free list
///    For testing purposes only
size_t* m61_get_alloc_list();

/// This magic class lets standard C++ containers use your allocator
/// instead of the system allocator.
template <typename T>
class m61_allocator {
public:
    using value_type = T;
    m61_allocator() noexcept = default;
    m61_allocator(const m61_allocator<T>&) noexcept = default;
    template <typename U> m61_allocator(m61_allocator<U>&) noexcept {}

    T* allocate(size_t n) {
        return reinterpret_cast<T*>(m61_malloc(n * sizeof(T), "?", 0));
    }
    void deallocate(T* ptr, size_t) {
        m61_free(ptr, "?", 0);
    }
};
template <typename T, typename U>
inline constexpr bool operator==(const m61_allocator<T>&, const m61_allocator<U>&) {
    return true;
}

/// Returns a random integer between `min` and `max`, using randomness from
/// `randomness`.
template <typename Engine, typename T>
inline T uniform_int(T min, T max, Engine& randomness) {
    return std::uniform_int_distribution<T>{min, max}(randomness);
}

#endif
