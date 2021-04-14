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


/*Testing whether address returned by anonymous mmap is page aligned.*/
int
main(int argc, char *argv[])
{
  int size = 12000;
  char *r1 = mmap(0, size, 0/*prot*/, 0/*flags*/, -1/*fd*/, 0/*offset*/);

  char *r2 = mmap(0, size, 0/*prot*/, 0/*flags*/, -1/*fd*/, 0/*offset*/);
  
  char *r3 = mmap(0, size, 0/*prot*/, 0/*flags*/, -1/*fd*/, 0/*offset*/);
  char *r4 = mmap(0, 4000, 0/*prot*/, 0/*flags*/, -1/*fd*/, 0/*offset*/);
  char *r5 = mmap(0, 4000, 0/*prot*/, 0/*flags*/, -1/*fd*/, 0/*offset*/);

  printf(1, "pointer r1 is the address %p\n", r1);
  printf(1, "pointer r2 is the address %p\n", r2);
  printf(1, "pointer r3 is the address %p\n", r3);

  int rv2 = munmap(r2, size);
  if (rv2 < 0) {
    printf(1, "XV6_TEST_OUTPUT : munmap failed\n");
    exit();
  }

  int rv4 = munmap(r4, size);
  if (rv4 < 0) {
    printf(1, "XV6_TEST_OUTPUT : munmap failed\n");
    exit();
  }
  
  printf(1, "XV6_TEST_OUTPUT : munmap good\n");

  char *r6 = mmap((void*)12000, 4000, 0/*prot*/, 0/*flags*/, -1/*fd*/, 0/*offset*/);
  printf(1, "pointer r6 is the address %p\n", r6);
  char *r7 = mmap(0, 4000, 0/*prot*/, 0/*flags*/, -1/*fd*/, 0/*offset*/);

  printf(1, "pointer r5 is the address %p\n", r5);
  printf(1, "pointer r7 is the address %p\n", r7);

  int rv6 = munmap(r6, 4000);
  if (rv6 < 0) {
    printf(1, "XV6_TEST_OUTPUT : munmap failed\n");
    exit();
  }  
  exit();
}
