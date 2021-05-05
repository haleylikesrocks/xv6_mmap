#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "mman.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{  
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

void
pagefault_handler(struct trapframe *tf)
{
  mmap_node * node_check = 0;
  void* fault_addr = (void*)PGROUNDDOWN(rcr2());
  node_check = myproc()->first_node;
  int counter = 0;
  char *mem = 0;

  while((int)node_check->addr % PGSIZE == 0){
    counter++;
    if(fault_addr >= node_check->addr && fault_addr < (node_check->addr + node_check->legth + PGSIZE)){
      if(node_check->protection & PROT_WRITE) {
        mem = kalloc();
        if((int)mem == 0){
          return;
        }
        memset(mem, 0 , PGSIZE);
        mappages(myproc()->pgdir, (char*)fault_addr, PGSIZE, V2P(mem), PTE_W|PTE_U); 
        lcr3(V2P(myproc()->pgdir));
        // break;
      } else {
        if(tf->err & 0x2){
          cprintf("trying to write when not allowed\n");
          break;
        }
        cprintf("we shouldn't be here\n");
        mem = kalloc();
        if((int)mem == 0){
          return;
        }

        memset(mem, 0 , PGSIZE);
        mappages(myproc()->pgdir, (char*)fault_addr, PGSIZE, V2P(mem), PTE_U);
        lcr3(V2P(myproc()->pgdir));
        // exit();
        // break;
      }
      if(node_check->region_type == MAP_FILE){
        //read contents of file into mapped region
        fileseek(myproc()->ofile[node_check->fd], node_check->offset);
        fileread(myproc()->ofile[node_check->fd], mem, PGSIZE);
        // need to clear dirty bit
        pte_t *pte = walkpgdir(myproc()->pgdir, fault_addr, 0);
        if(pte){
          *pte &= ~PTE_D;
        }
      }
      return;
    }
    node_check = node_check->next_node;
  }
  
  if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("in the trap handeler\n");
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("============in pagefault_handler============\n");
    cprintf("pid %d %s: trap %d err %d on cpu %d "
          "eip 0x%x addr 0x%x\n",
          myproc()->pid, myproc()->name, tf->trapno,
          tf->err, cpuid(), tf->eip, fault_addr);
    myproc()->killed = 1;
  }

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT:
    pagefault_handler(tf);
    // cprintf("we here\n");
    return;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
