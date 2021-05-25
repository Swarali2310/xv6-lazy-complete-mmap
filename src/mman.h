// Protection bits for mmap
// if the bit is set, the mmapped region can be written to
#define PROT_WRITE      1

// Flags for mmap
// MAP_ANONYMOUS : initialize the mmapped region with 0s
// MAP_FILE : initialize the mmapped region with the contents of the file from specified offset
#define MAP_ANONYMOUS   0
#define MAP_FILE        1

