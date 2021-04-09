#include "types.h"
#include "stat.h"
#include "param.h"
#include "defs.h"
#define PGSIZE 4096

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void
kmfree(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

static Header*
morecore()
{
  char* page;
  Header *hp;

  page = kalloc();
  if(page == 0)
    return 0;
  hp = (Header*)page;
  hp->s.size = PGSIZE / sizeof(Header);
  kmfree((void*)(hp + 1));
  return freep;
}

void*
kmalloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  // cprintf("enerting kmalloc \n");
  if(nbytes > PGSIZE){
    panic("kmalloc: can't allocate more than a page at a time");
  }

  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    // cprintf ("we come in here once but not again?\n");
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    // cprintf("looking for space\n");
    if(p->s.size >= nunits){
      
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        // cprintf("before step 1\n"); 
        // cprintf("1 here p->s.size %d  \n", p->s.size);
        // cprintf("1 here nunits %d  \n", nunits);
        // cprintf("1 here p %p  \n", p);
        p->s.size -= nunits;
        // cprintf("before step 2\n"); 
        // cprintf("2 here p->s.size %d  \n", p->s.size);
        // cprintf("2 here nunits %d  \n", nunits);
        // cprintf("2 here p %p  \n", p);
        // p += nunits;
        p += p->s.size;
        // cprintf("before step 3\n"); 
        // cprintf("3 here p->s.size %d  \n", p->s.size);
        // cprintf("3 here nunits %d  \n", nunits);
        // cprintf("3 here p %p  \n", p);
        p->s.size = nunits;
        // cprintf("end of else");
      }
      freep = prevp;
      // cprintf("leaving kmalloc\n");
      return (void*)(p + 1);
    }
    if(p == freep)
    // cprintf("need more core\n");
      if((p = morecore()) == 0)
        return 0;
  }

}
