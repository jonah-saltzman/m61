cs61_malloc
===================

This is the debugging memory allocator I wrote for CS61. It implements `malloc`, `free`,
`calloc`, and `realloc` according to the C specification. It also detects double frees,
buffer overflows, memory leaks, wild writes, and general invalid frees. It uses a small,
constant amount of external metadata for bookkeeping; the majority of metadata is internal
to the allocated blocks of memory, demonstrating a strong understanding of low-level memory
management and data structure manipulation.

As this was my first real C/C++ project, I would do several things differently now. Particularly,
I overused macros when I should have used inline functions to ensure type safety and readability.

All of the code in m61.cc and m61.hh was written by me (with the exception of a helper class
to allow C++ library structures to use my allocator, for testing).

Internal metadata documentation
----------------------
All of my blocks of memory, allocated & free, consist of an identical header & footer
in addition to a pair of pointers connecting the block to the doubly-linked free or
allocated lists. The header is a size_t representing the total size of the block,
including header, footer, any metadata, padding, and payload. The last 3 bits of the header/footer
are used to store the allocation state of the block itself and the two blocks before and
after it. The footer is always placed at the address calculated by the following, given a
size_t* `header` pointer pointing to the header:
```
size_t* footer = header + (*header / sizeof(size_t)) - 1
```
Adding the value of header to the header pointer produces the address of the next block;
subtracting one from this pointer yields the footer.

The addresses of all additional pieces of metadata are defined as offsets (using pointer arithmetic)
from the footer. For both allocated & free blocks, the pointer to the next block in the linked list
(not the next adjacent block in the heap) is at offset -1 from the footer, and the pointer to the
previous block is offset -2. These two pointers are the only additional metadata in free blocks.

Allocated blocks have the following structure of internal metadata:

![internal metadata documentation](https://i.imgur.com/9uXgdoU.png)

Each full block of metadata represents a WORD_SIZE block (sizeof(size_t)). To save
space, the request size and line number are stored as half-word integers. So the offset
of the request size is -5, and to get the line number, 1 is added to the int* req_size ptr. 

The magic number goes directly after the payload. Alignment padding follows
the magic number, so the distance between the magic number and the start of the metadata
blocks is variable. But because each of the metadata blocks are defined as offsets from
the foooter, rather than the header, this variable amount of padding does not affect
the ability to calculate the address of any piece of metadata.
