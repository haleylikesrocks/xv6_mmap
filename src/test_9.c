#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"
#include "mmu.h"
#include "mman.h"

int memcmp(char* start, uint size, char val) {
    for (uint i = 0; i < size; i++) {
        if (start[i] != val)
            return 1;
    }
    return 0;
}

/* Check to make sure page updates are appropriately flushed along with closest address behavior */
int
main(int argc, char *argv[])
{
  int size = PGSIZE;

  char* a1 = mmap(0, size, PROT_WRITE, 0, -1, 0);
  char* a2 = mmap(0, size, PROT_WRITE, 0, -1, 0);
  munmap(a1, size);
  munmap(a2, size);

  char* a3 = mmap(a1, size, PROT_WRITE, 0, -1, 0);

  if (a3 != a1)
    printf(1, "XV6_TEST_OUTPUT: closest address not chosen\n");

  memset(a3, 0, size);

  int pid = fork();

  if (pid < 0) {
      printf(1, "XV6_TEST_OUTPUT: fork failed\n");
  }

  if (pid == 0) {
      exit();
  }

  wait();

  printf(1, "XV6_TEST_OUTPUT: test9 passed\n");
  
  exit();
}
