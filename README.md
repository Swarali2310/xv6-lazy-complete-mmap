# xv6-lazy-complete-mmap
Adding support to xv6 for file backed and anonymous mmap with lazy allocation.

## Pagefault Handler - lazy page allocation

### Problem

With the anonymous mmap implementation, on calling the mmap() system call, xv6 immediately allocates and maps all the pages of physical memory needed to fulfill the request.
Expectation : Pages are only allocated upon actual access to the page.

This has quite a few advantages :-

i) Some programs allocate memory but never use it, for example, to implement large sparse arrays. Lazy page allocation allows us to avoid spending time and physical memory allocating and mapping the entire region.

ii) We can allow programs to map regions bigger than the amount of physical memory available. Do remember that, one of the operating system’s main goal is to provide the illusion of unlimited resources.

### Solution

In project xv6-anonymous-mmap, in the implementation of mmap, for every call to mmap where existing process space
could not be used, we did virtual to physical page mapping using mappages function. But, this
unnecessarily blocks the physical memory space in case when the allocated memory is not
referenced ever. Thus, in this assignment, we don't actually allocate the physical memory unless
the page is referenced. When the page is referenced, we get a page fault, and we handle the page
fault by allocating memory and doing the virtual to physical page mapping.

#### Implementation
• In the mmap call, we simply extend the p->sz to reflect the highest virtual address that the
process would be accessing, we add the information to the mmap_region linked list but we
dont map it to physical pages.

• Once the program tries to access the mmap-ed memory for which we dont have the physical
mapping, we get a page fault. Once we encounter the page fault, we get the faulting address
from cr2 register. We round it down to align with the page boundary and validate that this
address belongs to the mmap space by iterating over the mmap_region linked list.

• If the address is found to be valid (exists in the mmap region of the process, and works well
with the protection level of page (either read or write) ), we allocate it memory and map it to
physical memory. If not, we panic.

• If the mmap region is a file backed region, then we read the file contents into memory, and
once done, we reset the dirty bit of the page table entry. The dirty bit is the 6th bit (1000000
in binary = 0x40 in hexadecimal)


## mmap part 2 : File-backed mappings and msync

### Problem

File backed memory maps are initialized with the contents of the file descriptor.
Add msync() system call : This writes the changes made to the memory region back to the file.
These file-backed memory maps are extremely useful in avoiding the high cost of writing changes to persistent storage.
The tradeoff is that no changes made to file-backed memory regions are guaranteed to be persistent until msync() successfully returns.

### Solution

• Here, we support file backed mappings for mmap call. Incase of such, we have to initialize
the memory region with the contents of the file.

• As a part of implementation, first coded up the fileseek function which sets the file read to be
done from the offset specified in the mmap call.

• In the mmap call, we validate that the file descriptor is indeed valid, we duplicate it using
filedup() and increment the reference count for the file opened. The duplication of the file
descriptor is needed for msync call.

• Similarly, during munmap call, we close the file descriptor using fileclose() and decrement
the reference count of the opened file.

• For msync, we iterate over the mmap_region structure to find the address to write from, and
once found, we check if it is backed by a physical page, if yes, we file seek to the provided
offset and perform file write for len bytes, provided in the msync call.
