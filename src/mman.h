// protection bits for mmap
#define PROT_WRITE      1 //Pages may be written the default is to be ready only

//flages for anonymou mapping vs file backed mapping
#define MAP_ANONYMOUS   0 //memory region will be anonymous
#define MAP_FILE        1 //the memory will be file back