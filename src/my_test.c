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


/* Check to make sure kernel checks for freed address in between */
int
main(int argc, char *argv[])
{
  int size = PGSIZE;

  mmap(0, size, 0, 0, -1, 0);
  char* a2 = mmap(0, size, 0, 0, -1, 0);
  mmap(0, size, 0, 0, -1, 0);
  
  if (munmap(a2, size) < 0)
    printf(1, "XV6_TEST_OUTPUT: munmap failed\n");

  printf(1, "about to enter exec\n");

  // random addresses inside freed page
  exec(a2 + 105, (char**) (a2 + 800));

  printf(1, "XV6_TEST_OUTPUT: should print\n");
  printf(1, "XV6_TEST_OUTPUT: test10 passed\n");

  *a2 = 0;

  printf(1, "XV6_TEST_OUTPUT: shouldn't print\n");
  
  exit();
}
