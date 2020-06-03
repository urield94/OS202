#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

#define OCCUPIED 0
#define FREE 1

static char buffer[PGSIZE];

extern char data[]; // defined by kernel.ld
pde_t *kpgdir;      // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X | STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X | STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if (*pde & PTE_P)
  {
    pgtab = (pte_t *)P2V(PTE_ADDR(*pde));
  }
  else
  {
    if (!alloc || (pgtab = (pte_t *)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char *)PGROUNDDOWN((uint)va);
  last = (char *)PGROUNDDOWN(((uint)va) + size - 1);
  for (;;)
  {
    if ((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if (*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap
{
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
    {(void *)KERNBASE, 0, EXTMEM, PTE_W},            // I/O space
    {(void *)KERNLINK, V2P(KERNLINK), V2P(data), 0}, // kern text+rodata
    {(void *)data, V2P(data), PHYSTOP, PTE_W},       // kern data+memory
    {(void *)DEVSPACE, DEVSPACE, 0, PTE_W},          // more devices
};

// Set up kernel part of a page table.
pde_t *
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if ((pgdir = (pde_t *)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void *)DEVSPACE)
    panic("PHYSTOP too high");
  for (k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if (mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                 (uint)k->phys_start, k->perm) < 0)
    {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void switchkvm(void)
{
  lcr3(V2P(kpgdir)); // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void switchuvm(struct proc *p)
{
  if (p == 0)
    panic("switchuvm: no process");
  if (p->kstack == 0)
    panic("switchuvm: no kstack");
  if (p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts) - 1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort)0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir)); // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W | PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if ((uint)addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walkpgdir(pgdir, addr + i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if (sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if (readi(ip, P2V(pa), offset + i, n) != n)
      return -1;
  }
  return 0;
}

/**************helper functions to find pages**************/

int find_free_or_occupied_page(struct proc *p, int condition, int swap_or_ram)
{
  if (condition == 1)
  { //in this case we want to find free page
    for (int i = 0; i < 16; i++)
    {
      if (swap_or_ram == 1)
      { //find in ram_arr
        if (p->ram_arr[i].occupied)
        {
          continue;
        }
        return i;
      }
      else
      { //find in swap_arr
        if (p->swap_arr[i].occupied)
        {
          continue;
        }
        return i;
      }
    }
  }
  else
  { //in this case we want to find_occupied page
    for (int i = 0; i < 16; i++)
    {
      if (swap_or_ram == 1)
      { //find in ram_arr
        if (!p->ram_arr[i].occupied)
        {
          continue;
        }
        return i;
      }
      else
      { //find in swap_arr
        if (!p->swap_arr[i].occupied)
        {
          continue;
        }
        return i;
      }
    }
  }
  return -1;
}

int get_pte_flags(int va, pde_t *pgdir)
{
  pde_t *pte = walkpgdir(pgdir, (int *)va, 0);
  if (!pte)
    return -1;
  return PTE_FLAGS(*pte);
}

void disk_to_ram(uint start_pfault_va, char *ka)
{
  struct proc *p = myproc();
  if (p->pid > 2)
  {
    int perm = get_pte_flags(start_pfault_va, p->pgdir);

    //map all virtual addresses to the apropriate physical ones
    mappages(p->pgdir, (void *)start_pfault_va, PGSIZE, V2P(ka), perm);

    pde_t *pfault_pte = walkpgdir(p->pgdir, (void *)start_pfault_va, 0);
    *pfault_pte |= PTE_P | PTE_W | PTE_U; //declare that page is present, writable and owned by the user
    *pfault_pte &= ~PTE_PG;               //turn on paged_out flag
    lcr3(V2P(p->pgdir));                  //refresh TLB

    for (int i = 0; i < 16; i++)
    {
      if (p->swap_arr[i].virtual_adrr == start_pfault_va)
      {
        readFromSwapFile(p, buffer, i * PGSIZE, PGSIZE);
        int ram_index = find_free_or_occupied_page(p, FREE, 1);
        p->ram_arr[ram_index].offset_in_swap_file = p->swap_arr[i].offset_in_swap_file;
        p->ram_arr[ram_index].virtual_adrr = p->swap_arr[i].virtual_adrr;
        p->ram_arr[ram_index].pagedir = p->swap_arr[i].pagedir;
        p->ram_arr[ram_index].occupied = 1;
        p->swap_arr[i].occupied = 0;
        break;
      }
    }
  }
}

int isValidPage(pde_t *pgdir)
{
  uint va = rcr2();
  pte_t *pte = walkpgdir(pgdir, (char *)va, 0);
  return (*pte & PTE_PG);
}

void handle_page_fault()
{
  struct proc *p = myproc();
  if (p->pid > 2)
  {
    uint start_pfault_va = PGROUNDDOWN(rcr2());

    /*allocate physical address and reset it*/
    char *new_physical_adrr = kalloc();
    if (new_physical_adrr == 0)
      panic("allocuvm out of memory\n");
    memset(new_physical_adrr, 0, PGSIZE);

    int ram_index = find_free_or_occupied_page(p, FREE, 1);
    if (ram_index >= 0)
    {
      disk_to_ram(start_pfault_va, new_physical_adrr);
      memmove((void *)start_pfault_va, buffer, PGSIZE);
    }
    else
    {
      struct page exile_ram = p->ram_arr[0];      //this will change in task 3
      disk_to_ram(start_pfault_va, new_physical_adrr);
      memmove(new_physical_adrr, buffer, PGSIZE);
      int index = find_free_or_occupied_page(p, FREE, 0);
      if (index == -1)
        panic("swap_arr is occupied\n");
      cprintf("the index is:%d\n", exile_ram.virtual_adrr);
      if((writeToSwapFile(p, (char *)exile_ram.virtual_adrr, index*PGSIZE, PGSIZE)) == -1)
        panic("cant write file\n");
      p->swap_arr[index].virtual_adrr = exile_ram.virtual_adrr;
      p->swap_arr[index].offset_in_swap_file = index * PGSIZE;
      p->swap_arr[index].pagedir = exile_ram.pagedir;
      p->swap_arr[index].occupied = 1;

      pde_t *helper = walkpgdir(exile_ram.pagedir, (int *)exile_ram.virtual_adrr, 0);
      uint ramPa = PTE_ADDR(*helper);

      *helper |= PTE_PG;
      *helper &= ~PTE_P;
      *helper &= PTE_FLAGS(*helper);
      lcr3(V2P(p->pgdir));
      kfree((char *)P2V(ramPa));
    }
  }
}

/**********************************************************/

/****************swap function - Task 1.2******************/

void swap(struct proc *p, pde_t *pagedir, uint mem)
{
  if (p->pid > 2)
  {
    /*find avaiable and occupied spaces in ram_arr and swap_arr */

    int index_ram = find_free_or_occupied_page(p, OCCUPIED, 1);
    if (index_ram == -1)
      panic("no occupied cell in ram_arr\n");
    int index_swap = find_free_or_occupied_page(p, FREE, 0);
    if (index_swap == -1)
      panic("swap_arr is occupied\n");

    /*write to swapFile and update swap_arr*/

    if ((writeToSwapFile(p, (char *)p->ram_arr[index_ram].virtual_adrr, index_swap * PGSIZE, PGSIZE)) == -1)
    {
      panic("wrtie to swapFile failed..\n");
    }
    p->swap_arr[index_swap].virtual_adrr = mem;
    p->swap_arr[index_swap].offset_in_swap_file = index_swap * PGSIZE;
    p->swap_arr[index_swap].pagedir = pagedir;
    p->swap_arr[index_swap].occupied = 1;

    /*clear physical address of ram_arr[index] PTE*/

    pte_t *pte = walkpgdir(pagedir, (int *)p->ram_arr[index_ram].virtual_adrr, 0);
    uint pa = PTE_ADDR(*pte);
    kfree(P2V(pa));

    p->ram_arr[index_ram].occupied = 0;

    *pte |= PTE_PG;
    *pte &= ~PTE_P;
    *pte &= PTE_FLAGS(*pte); //clear flags for pte
    lcr3(V2P(p->pgdir));     //flush the TLB

    p->ram_arr[index_ram].virtual_adrr = mem;
    p->ram_arr[index_ram].pagedir = pagedir;
    p->ram_arr[index_ram].occupied = 1;
    p->ram_arr[index_ram].offset_in_swap_file = -1;
  }
}

/***********************************************************/

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;
  struct proc *p = myproc();
  if (newsz >= KERNBASE)
    return 0;
  if (newsz < oldsz)
    return oldsz;
  if (p->pid > 2 && PGROUNDUP(newsz) / PGSIZE > MAX_TOTAL_PAGES)
    return 0;
  a = PGROUNDUP(oldsz);
  for (; a < newsz; a += PGSIZE)
  {
    if ((p->ram_counter + p->swap_counter) >= 32)
    {
      panic("Not Enough space for allocating more pages..\n");
    }
    mem = kalloc();
    if (mem == 0)
    {
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pgdir, (char *)a, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0)
    {
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
    if (p->pid > 2)
    {
      int i = 0;
      if ((i = find_free_or_occupied_page(p, FREE, 1)) >= 0)
      { //case 1: put page in ram
        p->ram_arr[i].occupied = 1;
        p->ram_arr[i].offset_in_swap_file = -1;
        p->ram_arr[i].pagedir = pgdir;
        p->ram_arr[i].virtual_adrr = a;
      }
      else
      { //case 2: no space in ram, so we swap
        swap(p, pgdir, a);
      }
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if (newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for (; a < oldsz; a += PGSIZE)
  {
    pte = walkpgdir(pgdir, (char *)a, 0);
    if (!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if ((*pte & PTE_P) != 0)
    {
      pa = PTE_ADDR(*pte);
      if (pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      if (myproc()->pid > 2)
      {
        for (int i = 0; i < 16; i++)
        {
          if (myproc()->ram_arr[i].virtual_adrr == a && (myproc()->ram_arr[i].pagedir == pgdir))
          {
            myproc()->ram_arr[i].occupied = 0;
          }
        }
      }
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void freevm(pde_t *pgdir)
{
  uint i;

  if (pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for (i = 0; i < NPDENTRIES; i++)
  {
    if (pgdir[i] & PTE_P)
    {
      char *v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char *)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if (pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t *
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if ((d = setupkvm()) == 0)
    return 0;
  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walkpgdir(pgdir, (void *)i, 0)) == 0)
      panic("(copyuvm: pte should exist");
    if (!(*pte & PTE_P))
      panic("copyuvm: page not present");
    if ((myproc()->pid > 2))
    {
      if(*pte & PTE_PG){
      pte = walkpgdir(d, (int *)i, 0);
      *pte |= PTE_PG;
      *pte &= ~PTE_P;
      *pte &= PTE_FLAGS(*pte);
      lcr3(V2P(myproc()->pgdir));
      continue;
      }
    }
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char *)P2V(pa), PGSIZE);
    if (mappages(d, (void *)i, PGSIZE, V2P(mem), flags) < 0)
    {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char *
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if ((*pte & PTE_P) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  return (char *)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char *)p;
  while (len > 0)
  {
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char *)va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if (n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
