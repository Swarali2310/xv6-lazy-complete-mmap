#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "mmu.h"
#include "mman.h"
#include "memlayout.h"
#include "proc.h"

//deletes the node entry from mmap_region linked list structure
//this function is called when munmap() is called on an address
static void delete_mmap_node(mmap_region* node, mmap_region* prev)
{
  if(node == myproc()->head)
  {
    if(myproc()->head->next != 0)
      myproc()->head = myproc()->head->next;
    else
      myproc()->head = 0;
  }
  else
    prev->next = node->next;
  kmfree(node);
}

//this frees up the entire mmap_region linked list structure.
//this function is called in freevm() when we have to free
//all the memory pages in the user space.
void free_mmap_list()
{
  mmap_region* reg = myproc()->head;
  mmap_region* temp;
  while(reg)
  {
    temp = reg;
    delete_mmap_node(reg, 0);
    reg = temp->next;
  }
}

//mmap creates a new mapping in the calling process's address space
//addr is the hint where the mapped address should be, kernel may or may not use it
//length is the #bytes to be mapped
//prot : protection level of the page : by default all the pages are read-only. PROT_WRITE allows them to be written
//flags : whether it is anonymous mmap (MAP_ANONYMOUS) or file backed mmap (MAP_FILE)?
//fd : In case of MAP_FILE, specify the fd of the file to be read, else -1
//offset : specify the offset to read the file from 
void *mmap(void *addr, int length, int prot, int flags, int fd, int offset)
{
  struct proc *p = myproc();
  uint oldsz = p->sz;
  uint newsz = p->sz + length;
  //allocuvm to increase the process VAS is not applicable in the case of lazy mapping
  //p->sz = allocuvm(p->pgdir, oldsz, newsz); 
  p->sz = newsz;

  //allocate memory to the structure
  mmap_region* reg = (mmap_region*)kmalloc(sizeof(mmap_region));
  //if allocation fails, free up the previously increased process size using allocuvm
  if(reg==(mmap_region*)0)
  {
    //deallocuvm(p->pgdir, newsz, oldsz);
    return (void*)-1;
  }

  //down align the page address with the page boundary
  //populate the mmap_region structure
  addr = (void*)PGROUNDDOWN(oldsz);
  reg->addr = addr;
  reg->len = length;
  reg->rtype = flags;
  reg->offset = offset;
  reg-> prot = prot;
  reg->next = 0;

  //for MAP_ANONYMOUS : fd has to be -1
  //if not, return error
  if(flags == MAP_ANONYMOUS )
  {
    if(fd != -1)
    {
      kmfree(reg);
      return (void*)-1;
    }
  }

  //for MAP_FILE : allocate a descriptor for the file if fd > -1
  //create a duplicate file descriptor to save in mmap_region structure
  else if(flags == MAP_FILE)
  {
    if(fd > -1)
    {
      if((fd=fdalloc(p->ofile[fd])) < 0)
        return (void*)-1;
      filedup(p->ofile[fd]);
      reg->fd = fd;
    }
    else
    {
      kmfree(reg);
      return (void*)-1;  
    }
  }

  //if its the first region to be mmaped, set the head.
  //else iterate over the mapped list, to check if the hint addr is already mapped.
  //in that case, move up PGSIZE+length, align the addr and assign it to the data structure
  if(p->nregions == 0)
    p->head = reg;
  else
  {
    if (addr == p->head->addr)
    {
      addr += PGROUNDDOWN(PGSIZE+length);
    }
    mmap_region* node = p->head;
    while(node->next != 0)
    {
      if(addr == node->addr)
      {
        addr += PGROUNDDOWN(PGSIZE+length);
        node = p->head;
      }
      else if(addr == (void*)KERNBASE || addr > (void*)KERNBASE)
      {
        kmfree(reg);
        //deallocuvm(p->pgdir, newsz, oldsz);
        return (void*)-1;
      }
      node = node->next;
    }
    if (addr == node->addr)
      addr += PGROUNDDOWN(PGSIZE+length);
    node->next = reg;
  }

  p->nregions++;
  reg->addr = addr;
  return reg->addr;  
}

//munmap : unmap the memory mapped using mmap
//here we assume that, the parameters passed to munmap will always be 
//the same starting address and length used by mmap.
int munmap(void *addr, uint length)
{
  if (addr == (void*)KERNBASE || addr > (void*)KERNBASE || length <= 0) 
    return -1;
  struct proc *p = myproc();

  //if there are no mapped regions, there is nothing to unmap
  if (p->nregions == 0)
    return -1;
  mmap_region* node = p->head;
  mmap_region* next = p->head->next;

  //if the parameters passed to munmap as seen at the head of the list structure,
  //reduce the process size, delete the node.
  //if the page is file mapped, close the open files
  if (p->head->addr == addr && p->head->len == length)
  {
    p->sz = deallocuvm(p->pgdir, p->sz, p->sz - length);
    switchuvm(p);
    p->nregions--;
    if(p->head->rtype == MAP_FILE )
    {
      fileclose(p->ofile[p->head->fd]);
      p->ofile[p->head->fd] = 0;
    }
    delete_mmap_node(p->head, 0);
    return 0;
  }

  //iterate over the list to check if the parameters passed to munmap are in the list
  //reduce the process size, delete the node.
  //if the page is file mapped, close the open files
  while(next != 0)
  {
    if(next->addr == addr && next->len == length)
    {
      p->sz = deallocuvm(p->pgdir, p->sz, p->sz - length);
      switchuvm(p);
      p->nregions--;
      if(next->rtype == MAP_FILE )
      {
        fileclose(p->ofile[next->fd]);
        p->ofile[next->fd]=0;
      }
      delete_mmap_node(next, node);
    }
    node = next;
    next = node->next;
  }
  return -1;
} 

//sync the contents of the memory region into the file at the location offset
//assumption : parameters passed to msync are the same as the starting addr and length by the mmap.
int msync (void* start_addr, uint length)
{
  struct proc *p = myproc();

  //if the number of mapped regions is 0, there is nothing to sync
  if (p->nregions == 0)
    return -1;

  //iterate over the list
  //check if there exists a valid page table entry for the page whose VA is start_addr
  //this check is needed because we are using lazy allocation
  //check if the dirty bit is SET, if yes, then seek to the offset specified in the data structure
  //and write the file with the contents in the memory
  mmap_region *cursor = p->head;
  while(cursor)
  {
    pte_t* ret = walkpgdir(p->pgdir, start_addr, 0);
    if((uint)ret)
    {
      if(PTE_D)
      {
        fileseek(p->ofile[cursor->fd], cursor->offset);
        filewrite(p->ofile[cursor->fd], start_addr, length);
      }
      return 0;
    }
    cursor=cursor->next;
  }

  //if no physical page mapping found or the start_addr does not exist in the process VAS, return error
  return -1;
}
