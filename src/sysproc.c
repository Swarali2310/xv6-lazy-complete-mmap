#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_kmalloc(void)
{
  int nbytes;
  if( argint(0,&nbytes)<0  )
    return -1;
  return (int)kmalloc((uint)nbytes);
}

int
sys_kmfree(void)
{
  int addr;
  if( argptr(0,(void*)&addr,sizeof((void*)&addr))<0  )
    return -1;
  kmfree((void*)addr);
  return 0;
}

int
sys_mmap(void)
{
  int addr;
  int length;
  int prot;
  int flags;
  int fd;
  int offset;

  if((argint(0, &addr) < 0) || (argint(1, &length) < 0) || (argint(2, &prot) < 0) || (argint(3, &flags) < 0) || (argint(4, &fd) < 0) || (argint(5, &offset) < 0))
  {
    return -1;
  }

  return (int)mmap((void*)addr, length, prot, flags, fd, offset);
}

int
sys_munmap(void)
{
  int addr;
  int length;

  if((argint(0, &addr) < 0) || (argint(1, &length) < 0))
  {
    return -1;
  }

  return munmap((void*)addr, (uint)length);
}

int
sys_msync(void)
{
  int addr;
  int length;

  if((argint(0, &addr) < 0) || (argint(1, &length) < 0))
  {
    return -1;
  }

  return msync((void*)addr, (uint)length);
}
