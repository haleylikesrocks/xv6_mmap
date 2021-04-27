#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "mman.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // add init of mmap num here - hr

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

p = allocproc();  
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  p->num_mmap = 0;//hr-initialing number of mmap request
  p->num_free = 0;
  p->free_mmap = 0;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  np->num_free = curproc->num_free;
  np->num_mmap = curproc->num_mmap;

  if(np->num_mmap > 0){
    mmap_node *old_node =  curproc->first_node;
    mmap_node *new_prev_node;
    mmap_node *new_first_node = kmalloc(sizeof(mmap_node));
    *new_first_node = *old_node;
    np->first_node = new_first_node;
    old_node = old_node->next_node;
    new_prev_node = np->first_node;

    while ((int)old_node->addr % PGSIZE == 0)
    {
      mmap_node *new_node = kmalloc(sizeof(mmap_node));
      *new_node = *old_node;
      *new_prev_node->next_node = *new_node;
      new_prev_node = new_prev_node->next_node;
      old_node = old_node -> next_node;
    }
  }

  if(np->num_free >0){

    mmap_node * old_node = curproc->free_mmap;
    mmap_node *new_prev_node;

    mmap_node *new_first_free = kmalloc(sizeof(mmap_node));
    *new_first_free = *old_node;
    np->free_mmap = new_first_free;
    old_node = old_node->next_node;
    new_prev_node = np->free_mmap;

    while ((int)old_node->addr % PGSIZE == 0)
    {
      mmap_node *new_node = kmalloc(sizeof(mmap_node));
      *new_node = *old_node;
      *new_prev_node->next_node = *new_node;
      new_prev_node = new_prev_node->next_node;
      old_node = old_node -> next_node;
    }
  }
  

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// hr mmap and munmap starts here

void print_node(mmap_node *node){
  cprintf("the addresss of the node is %p \n", node->addr);
  cprintf("the length of the node is %d\n", node->legth);
  // cprintf("the next node's address is %p\n", node->next_node->addr);
  return;
}

void* mmap(void* addr, int length, int prot, int flags, int fd, int offset){
  struct proc *curproc = myproc();
  void *return_addr =0, *closest_addr = 0;
  int distance;
  mmap_node *free_space=0, *prev_free =0, *closest_node=0, *prev_closest =0;

  // cprintf("welcome to mmap\n");

  if(length < 1){ // you can't map nothing
    return (void*)-1;
  }

  if(flags == MAP_ANONYMOUS){
    if(fd != -1){
      return (void*) -1;
    }

  }
  if(flags == MAP_FILE){
    // if(fd > -1){
    //   int new_fd = dup(fd);
    //   if(new_fd == -1){
    //     return (void*) -1;
    //   }
    // }
    // struct file* f; 
    // f = fdopen(fd);
    // if(f == -1 ){
    //   return (void*) -1;
    // }
    if((fd=fdalloc(curproc->ofile[fd])) < 0)
      return (void*)-1;

    filedup(curproc->ofile[fd]);
    
  }
  
  length = PGROUNDUP(length);
  distance = curproc->sz - (uint)addr;
  int round_addr = PGROUNDUP((int)addr);

  /* this is where our search for the best location begins */
  
  // look for large enough mmap allocte space in the linked list 
  // we'll call this the closest node
  if(curproc->num_free > 0){
    free_space = curproc->free_mmap;
    
    while(1){ // looking for closest free space
      if((int)free_space->addr % PGSIZE != 0){ // no list or end of list
        break;
      }

      if(free_space->legth >= length){ // the space is large enough
        // cprintf("we have found a large enough space \n");
        if (distance > free_space->addr - addr){ // check if closest address space
          closest_addr = free_space->addr;
          closest_node = free_space;
          prev_closest = prev_free;
          distance = free_space->addr - addr;
        }
      }
      // moving to next location in free list
      prev_free = free_space; 
      free_space =free_space->next_node;
    }
    // cprintf("now that we have finished itterating:the closest node adress is %p\n", closest_addr);
    if(closest_node->legth == length){ // prefect fit
      if(prev_closest == 0){ // mapping into the first free space
        curproc->free_mmap= curproc->free_mmap->next_node;
      } else{ // mapping anywhere else
        prev_closest->next_node = closest_node->next_node;
      }
      kmfree(closest_node);  // we can free the node because it is a perfect hit
      curproc->num_free -= 1;
    } else { // not a prefect fit
      // look for the best location within the space : ie is the exact location is avaible
      while(round_addr > (int)closest_node->addr ){
        if((uint)round_addr + length < (uint)closest_node->addr+closest_node->legth){
          // found the nearest address
          closest_addr = (void*)round_addr;

          //set up the next free node new address and size
          mmap_node *new_next = kmalloc(sizeof(mmap_node));
          new_next->next_node = closest_node->next_node;
          closest_node->next_node = new_next;
          // getting the pointers right
          new_next->addr = (void*)round_addr+length; 
          new_next->legth = closest_node->legth - ((round_addr - (int)closest_node->addr) +length);
          closest_node->legth = round_addr - (int)closest_node->addr;
          break;
        } else{
          round_addr -= PGSIZE;
        } 
      }
      if(round_addr < (int)closest_node->addr){
        closest_addr = closest_node->addr;
        closest_node->addr += length;
        closest_node->legth -= length;
      }
      if(prev_closest == 0){ // mapping into the first location
        curproc->free_mmap = closest_node;
      }
      if (closest_node->legth == 0){ // mapped at the top of the free space
        prev_closest->next_node = closest_node->next_node;
        kmfree(closest_node);
      }
      if (closest_node->next_node->legth == 0){ // mapped at the bottom of the free space
        closest_node->next_node = closest_node->next_node->next_node;
        kmfree(closest_node->next_node);
      }
      // cprintf("the address we are mapping to is %p\n", closest_addr);
    }

    // if(allocuvm(curproc->pgdir, (uint)closest_addr, (uint)closest_addr + length) == 0){
    //   cprintf("out o memory \n");
    // }
    // lcr3(V2P(curproc->pgdir));

    // print_node(closest_node);
    return_addr = (void*)closest_addr;
    } else{ // just allocate from bottom of the stack
      // if(allocuvm(curproc->pgdir, PGROUNDUP(curproc->sz), curproc->sz+length) == 0 ){
      //   cprintf("out o memory \n");
      // }
      return_addr = (void*)curproc->sz;
      // cprintf("the return address is now %d \n ", (uint)return_addr);
      curproc->sz += length;
    }

  //add allocate memory for node data
  mmap_node *p = kmalloc(sizeof(mmap_node)); ///need to cast return adddress to next node pointer
  //add data to node
  p->addr = return_addr;
  p->legth = length;
  p->region_type = flags;
  p->offset = offset;
  p->fd = fd;
  p->protection = prot;
  // print_node(p);
  // adding data to the linked list
  if(curproc->num_mmap == 0){ // firist time mmap has been called fo rthis proccesss
    // cprintf ("the number of mmaps is %d\n", curproc->num_mmap);
    curproc->first_node = p;
    p->next_node = 0;
  } else if (curproc->first_node->addr > return_addr) { /// has the lowest address so needs be add at beginning
    p->next_node = curproc->first_node;
    curproc->first_node = p;
  } else {  
    mmap_node *prev_node = curproc->first_node, *tnode;
    for(tnode = curproc->first_node->next_node; ; tnode = tnode->next_node){
      if(tnode->addr > return_addr){
        p->next_node = tnode;
        prev_node->next_node = p;
        break;
      } else if (tnode->next_node == 0){
        tnode->next_node = p;
        p->next_node = 0;
        break;
      }
      prev_node = tnode;
    }
  }
  curproc->num_mmap += 1;
  // cprintf("the current number of mmap is %d\n", curproc->num_mmap);
  // cprintf("the return address is now %p \n ", return_addr);
  return return_addr;
}

int munmap(void* addr, uint length){
  struct proc *curproc = myproc();
  mmap_node *prev = curproc->first_node; 
  mmap_node * node_hit = 0;
  length = PGROUNDUP(length);
  // int counter = 2;

  // cprintf("hello welcom to unmapp \n");

  if(curproc->num_mmap == 0){ //hr- number of mapped regions is zero
    return -1;
  }

  if ((int)addr % 4096 != 0){
    panic("ahhhhh un map");
  }

  prev = curproc->first_node;
  int counter = curproc->num_mmap;
  // cprintf("hello\n");
  // cprintf("the next node addr is %p and the addr passed in is %p\n", prev->next_node->addr, addr);
  while(counter > 0){

    // check to see if it is the first node
    if(curproc->first_node->addr == addr && curproc->first_node->legth == length){
      node_hit = curproc->first_node;
      if(counter > 1){
        curproc->first_node = curproc->first_node->next_node;
      } else {
        curproc->first_node = 0;
      }
    break;
    }
    cprintf("the next node addr is %p and the addr passed in is %p\n", prev->next_node->addr, addr);
    if (prev->next_node->addr == addr && prev->next_node->legth == length){ // got a hit
      node_hit = prev->next_node; 
      prev->next_node = node_hit->next_node; // prev node no longer points to node hit
      break;
    }
    if((int)prev->next_node->addr % PGSIZE != 0){ // we reached the last node with no hits
      return -1;
    }
    prev = prev->next_node;
    counter --;
  }
  pte_t *pte;
  if (node_hit != 0){
    // cprintf("it could be walking the pgdir\n");
    if((pte = walkpgdir(curproc->pgdir, node_hit->addr, 0)) == 0){
      // cprintf("might be the memset\n");
      memset(addr, 0, length); 
    }
    if(node_hit->region_type == MAP_FILE){
      // cprintf("is this the location of the trap?\n");
      fileclose(curproc->ofile[node_hit->fd]);
      curproc->ofile[node_hit->fd] = 0;
      // cprintf("no\n");
    }
    // deallocuvm(curproc->pgdir, (uint)node_hit->addr +length, (uint)node_hit->addr);
    // lcr3(V2P(curproc->pgdir));
    curproc->num_mmap--;
    kmfree(node_hit);
  }

  // add to free list
  mmap_node *next, *previous;
  mmap_node *r = kmalloc(sizeof(mmap_node)); ///need to cast return adddress to next node pointer

  //add data to node
  r->addr = addr;
  r->legth = length;

  if(curproc->num_free == 0){ // incorperate the first node
    curproc->free_mmap = r;
    curproc->free_mmap->next_node = 0;
    // print_node(curproc->free_mmap);
    
  } else{
    previous = curproc->free_mmap;
    while(1){
      if(addr > previous->addr && (addr < previous->next_node->addr || previous->next_node == 0)){ // in between current and next or end of list
        if(previous->addr + previous->legth == r->addr){
          previous->legth += r->legth;
          if(r->addr + r->legth == previous->next_node->addr){
            previous->legth += previous->next_node->legth;
            previous->next_node = previous->next_node->next_node;
            break;
          }
          break;
        } 
        else if (r->addr + r->legth == previous->next_node->addr){
          r->legth += previous->next_node-> legth;
          r->next_node = previous->next_node->next_node;
          previous->next_node = r;
          break;
        } else{
          next = previous->next_node;
          previous->next_node = r;
          r->next_node = next;
          break;
        }
      }

      if(previous->next_node == 0){
        previous->next_node = r;
        r->next_node =0;
        break;
      }

      previous = previous->next_node;
    }

  }
  curproc->num_free += 1;

  return 0;
}

int msync(void * start_addr, int length){
  struct proc *curproc = myproc();
  mmap_node *prev = curproc->first_node; 
  mmap_node * node_hit = 0;
  length = PGROUNDUP(length);

  if(curproc->num_mmap == 0){ //hr- number of mapped regions is zero
    return -1;
  }
  prev = curproc->first_node;
  int counter = curproc->num_mmap;
  // cprintf("the next node addr is %p and the addr passed in is %p\n", prev->next_node->addr, addr);
  while(counter > 0){
    // check to see if it is the first node
    if(curproc->first_node->addr == start_addr && curproc->first_node->legth == length){
      node_hit = curproc->first_node;
    break;
    }
    // cprintf("the next node addr is %p and the addr passed in is %p\n", prev->next_node->addr, addr);
    if (prev->next_node->addr == start_addr && prev->next_node->legth == length){ // got a hit
      node_hit = prev->next_node; 
      break;
    }
    if((int)prev->next_node->addr % PGSIZE != 0){ // we reached the last node with no hits
      return -1;
    }
    prev = prev->next_node;
    counter --;
  }

  if(fileseek(curproc->ofile[node_hit->fd], node_hit->offset) != 0){
    return -1;
  }
  pte_t *pte;
  void *page_check = node_hit->addr;

  while (page_check < node_hit->addr + node_hit->legth)
  {
    pte = walkpgdir(curproc->pgdir, page_check, 0);
    // cprintf("the pte is %p\n", pte);
    if(pte != 0 && *pte & PTE_D){
      filewrite(curproc->ofile[node_hit->fd], page_check, PGSIZE);
      // cprintf("not sure if I am doing the dirty bit correctly the addr is %p\n", page_check);
    }
    // cprintf("is page check: %p less than %p \n", page_check, node_hit->addr + node_hit->legth);
    page_check = (void *)((int)page_check + PGSIZE);
  }
  return 0;
}
