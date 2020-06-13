#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"

/************************************************ TASK - 3 ************************************************/

/*************************************** Policy's Algorithms ***************************************/
int second_chance_fifo()
{
  struct proc *curproc = myproc();
  int index = -1;
  int i;
  struct page temp[MAX_PYSC_PAGES];

  for (i = 0; i < MAX_PYSC_PAGES; i++)
    temp[i].occupied = 0;

  int temp_index = 0;
  for (i = 0; (i < MAX_PYSC_PAGES) && (index == -1); i++)
  {
    uint temp_v_addr = curproc->ram_arr[i].virtual_adrr;
    pte_t *pte = accessable_walkpgdir(curproc->pgdir, (char *)temp_v_addr, 0);
    if (*pte & PTE_A)
    {
      *pte &= ~PTE_A;
      temp[temp_index].occupied = curproc->ram_arr[i].occupied;
      temp[temp_index].offset_in_swap_file = curproc->ram_arr[i].offset_in_swap_file;
      temp[temp_index].virtual_adrr = curproc->ram_arr[i].virtual_adrr;
      temp[temp_index].pagedir = curproc->ram_arr[i].pagedir;
      temp_index++;
    }
    else
    {
      index = i;
      break;
    }
  }

  if (index == -1)
  {
    index = 0;
    if (curproc->ram_arr[i].occupied == 1)
    {
      while (index < MAX_PYSC_PAGES - 1)
      {
        curproc->ram_arr[index].occupied = curproc->ram_arr[index + 1].occupied;
        curproc->ram_arr[index].virtual_adrr = curproc->ram_arr[index + 1].virtual_adrr;
        curproc->ram_arr[index].offset_in_swap_file = curproc->ram_arr[index + 1].offset_in_swap_file;
        curproc->ram_arr[index].pagedir = curproc->ram_arr[index + 1].pagedir;
        index++;
      }
      curproc->ram_arr[MAX_PYSC_PAGES - 1].occupied = 0;
    }
    else
    {
      panic("SCFIFO - Second Chance FIFO ERROR");
    }
    return index;
  }

  struct page tmp_page;
  tmp_page.offset_in_swap_file = curproc->ram_arr[index].offset_in_swap_file;
  tmp_page.occupied = curproc->ram_arr[index].occupied;
  tmp_page.virtual_adrr = curproc->ram_arr[index].virtual_adrr;
  tmp_page.pagedir = curproc->ram_arr[index].pagedir;

  i++;
  int new_index = 0;
  while (i < MAX_PYSC_PAGES && curproc->ram_arr[i].occupied)
  {
    curproc->ram_arr[new_index].occupied = curproc->ram_arr[i].occupied;
    curproc->ram_arr[new_index].virtual_adrr = curproc->ram_arr[i].virtual_adrr;
    curproc->ram_arr[new_index].offset_in_swap_file = curproc->ram_arr[i].offset_in_swap_file;
    curproc->ram_arr[new_index].pagedir = curproc->ram_arr[i].pagedir;

    i++;
    new_index++;
  }

  i = 0;
  while (new_index < MAX_PYSC_PAGES)
  {
    curproc->ram_arr[new_index].occupied = temp[i].occupied;
    curproc->ram_arr[new_index].virtual_adrr = temp[i].virtual_adrr;
    curproc->ram_arr[new_index].offset_in_swap_file = temp[i].offset_in_swap_file;
    curproc->ram_arr[new_index].pagedir = temp[i].pagedir;
    i++;
    new_index++;
  }

    curproc->ram_arr[MAX_PYSC_PAGES - 1].occupied = tmp_page.occupied;
    curproc->ram_arr[MAX_PYSC_PAGES - 1].virtual_adrr = tmp_page.virtual_adrr;
    curproc->ram_arr[MAX_PYSC_PAGES - 1].offset_in_swap_file = tmp_page.offset_in_swap_file;
    curproc->ram_arr[MAX_PYSC_PAGES - 1].pagedir = tmp_page.pagedir;
  return MAX_PYSC_PAGES - 1;
}

int not_frequently_used()
{
  struct proc *curproc = myproc();
  int i;
  int min_index = 0;
  for (i = 1; i < MAX_PYSC_PAGES; i++)
  {
    if (curproc->ram_arr[i].age_count < curproc->ram_arr[min_index].age_count)
      min_index = i;
  }

  int j = min_index;
  while (j < MAX_PYSC_PAGES - 1)
  {
    curproc->ram_arr[j] = curproc->ram_arr[j + 1];
    j++;
  }
  curproc->ram_arr[MAX_PYSC_PAGES - 1].occupied = 0;
  return MAX_PYSC_PAGES - 1;
}

int least_accessed_page()
{
  struct proc *curproc = myproc();
  int i;
  int min_i = -1;
  int min_age = 0xFFFFFFFF;
  int min_count = 33;
  for (i = 0; i < MAX_PYSC_PAGES; i++)
  {
    int curr_age = curproc->ram_arr[i].age_count;
    int curr_count = 0;
    for (int j = 0; j < 32; j++)
    {
      if ((1 << j) & curr_age)
        curr_count++;
    }
    if (curr_count < min_count)
    {
      min_i = i;
      min_age = curr_age;
      min_count = curr_count;
    }
    else if (curr_count == min_count && curr_age < min_age)
    {
      min_i = i;
      min_age = curr_age;
    }
  }

  if (min_i == -1)
    panic("LAPA - Least Accessed Paged Ageing ERROR");

  while (min_i < MAX_PYSC_PAGES - 1)
  {
    curproc->ram_arr[min_i] = curproc->ram_arr[min_i + 1];
    min_i++;
  }
  curproc->ram_arr[MAX_PYSC_PAGES - 1].occupied = 0;
  return MAX_PYSC_PAGES - 1;
}

int advancing_queue()
{
  struct proc *curproc = myproc();
  int i = 0;
  while (i < MAX_PYSC_PAGES - 1)
  {
    curproc->ram_arr[i] = curproc->ram_arr[i + 1];
    i++;
  }
  curproc->ram_arr[MAX_PYSC_PAGES - 1].occupied = 0;
  return MAX_PYSC_PAGES - 1;
}

/****************************************************************************************************/

/*************************************** Policy's Maintainers ***************************************/

void update_pages_age_counter()
{
  struct proc *curproc = myproc();
  int i;

  for (i = 0; i < MAX_PYSC_PAGES; i++)
  {
    if (curproc->ram_arr[i].occupied)
    {
      curproc->ram_arr[i].age_count = curproc->ram_arr[i].age_count >> 1;
      pte_t *pte = accessable_walkpgdir(curproc->pgdir, (void *)curproc->ram_arr[i].virtual_adrr, 0);
      if (*pte & PTE_A)
      {
        curproc->ram_arr[i].age_count = curproc->ram_arr[i].age_count | 0x80000000; // 1 with 31 0
        *pte &= ~PTE_A;
      }
    }
  }
}

void sort_advancing_queue()
{
  struct proc *curproc = myproc();
  int i;
  for (i = MAX_PYSC_PAGES - 1; i < 0; i--)
  {
    if (!curproc->ram_arr[i].occupied)
      continue;
    int curr_page_idx = i;
    int prev_page_idx = i - 1;
    pte_t *pte_curr = accessable_walkpgdir(curproc->pgdir, (void *)curproc->ram_arr[curr_page_idx].virtual_adrr, 0);
    pte_t *pte_pre = accessable_walkpgdir(curproc->pgdir, (void *)curproc->ram_arr[prev_page_idx].virtual_adrr, 0);
    if (((*pte_pre & PTE_A) != 0) && ((*pte_curr & PTE_A) == 0))
    {
      struct page tmp = curproc->ram_arr[prev_page_idx];
      curproc->ram_arr[prev_page_idx] = curproc->ram_arr[curr_page_idx];
      curproc->ram_arr[curr_page_idx] = tmp;
    }
  }
}
/*********************************************************************************************/

/*************************************** Choose Policy ***************************************/
int is_none_paging_policy()
{
#ifdef NONE
  return 1;
#endif
  return 0;
}

int find_ram_by_policy()
{
#ifdef SCFIFO
  return second_chance_fifo();
#endif
#ifdef NFUA
  return not_frequently_used();
#endif
#ifdef LAPA
  return least_accessed_page();
#endif
#ifdef AQ
  return advancing_queue();
#endif
  return -1;
}
/*********************************************************************************************/

/********************************************************************************************************************/
