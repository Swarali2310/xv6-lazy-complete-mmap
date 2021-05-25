#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "mman.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

//handler gets called in the case of a page fault (T_PGFLT)
//verify if the page belongs to mapped region of the process, if yes, allocate memory and map it.
void
pagefault_handler(struct trapframe *tf)
{
  struct proc *curproc = myproc();
  uint fault_addr = rcr2();
    // Start -- Required debugging statement -----
  cprintf("============in pagefault_handler============\n");
  cprintf("pid %d %s: trap %d err %d on cpu %d "
  "eip 0x%x addr 0x%x\n",
  curproc->pid, curproc->name, tf->trapno,
  tf->err, cpuid(), tf->eip, fault_addr);
  // End -- Required debugging statement ----

  //get the faulting addr from the CR2 register, page align it.
  //iterate over the mmap_region list and check if the faulting address falls in the mmap space.
  //if the page has appropriate protection level, make it valid
  fault_addr = PGROUNDDOWN(fault_addr);

  int valid = 0;
  mmap_region* cursor = curproc->head;
  while(cursor)
  {
    if((uint)(cursor->addr) <= fault_addr && (uint)(cursor->addr + cursor->len) > fault_addr)
    {
      if((cursor->prot & PROT_WRITE) || !(tf->err & T_PGFLT_W))
      {
        valid = 1;
        break;
      }
    }
    cursor = cursor->next;
  }

  //for valid pages, allocate and initialze the memory region appropriately
  //do the virtual => physical mapping using mappages() since the page is now referenced
  //error out in case of discrepancies
  if(valid == 1)
  {
    char* mem = kalloc();
    if(mem == 0)
      goto error;
    memset(mem,0,PGSIZE);
    int perm;
    if (cursor->prot == PROT_WRITE)
      perm = PTE_W|PTE_U;
    else
      perm = PTE_U;
    if(mappages(curproc->pgdir, (char*)fault_addr, PGSIZE, V2P(mem), perm) < 0)
    {
      kfree(mem);
      goto error;
    }
    switchuvm(curproc);

    if(cursor->rtype == MAP_FILE )
    {
      if (curproc->ofile[cursor->fd])
      {
        fileseek(curproc->ofile[cursor->fd], cursor->offset);
        fileread(curproc->ofile[cursor->fd], mem, cursor->len);
        pde_t* pde = &(myproc()->pgdir)[PDX(cursor->addr)];
        pte_t* pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
        pte_t* pte = &pgtab[PTX(cursor->addr)];
        *pte &= ~PTE_D;
      }
    }
  }
  else
  {
    error:
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
}

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
    break;

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

