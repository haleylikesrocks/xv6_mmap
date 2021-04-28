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

/* Fork test to make sure deep copy of linked list, along with a malloc */
int
main(int argc, char *argv[])
{
  int size =  2*PGSIZE;

  char* a1 = mmap(0, size, PROT_WRITE, 0, -1, 0);
  char* m1 = malloc(size);
  char* a2 = mmap(0, size, PROT_WRITE, 0, -1, 0);

  memset(a1, 1, size);
  memset(m1, 2, size);
  memset(a2, 3, size);

  int pid = fork();

  if (pid < 0) {
      printf(1, "XV6_TEST_OUTPUT: fork failed\n");
  }

  if (pid == 0) {
      memset(a2, 0, size);
      munmap(a1, size);
      exit();
  }

  wait();

  char* a3 = mmap(0, size, PROT_WRITE, 0, -1, 0);

  if (a3 == a1 || a3 == a2) {
      printf(1, "XV6_TEST_OUTPUT: reuse of address\n");
  }

  if (memcmp(a1, size, 1) != 0 || memcmp(a2, size, 3) != 0 || memcmp(m1, size, 2) != 0)
    printf(1, "XV6_TEST_OUTPUT: values changed\n");

  printf(1, "XV6_TEST_OUTPUT: test8 passed\n");
  
  exit();
}
