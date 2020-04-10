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
  int exit_status;
  if(argint(0, &exit_status) < 0)
    exit(1);
  exit(exit_status);
  return 0;  // not reached
}

int
sys_wait(void)
{
  int *exit_status;
  if(argptr(1, (void*)&exit_status, sizeof(*exit_status)) < 0)
    return -1;
  return wait(exit_status);
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

// return size of current process
int
sys_memsize(void)
{
  return myproc()->sz;
}

// return size of current process
int
sys_set_ps_priority(void)
{
  int ps_priority;
  if(argint(0, &ps_priority) < 0)
    return -1;
  return set_ps_priority(ps_priority); 
}

// set the scheduling policy
int
sys_policy(void)
{
  int policy_num;
  if(argint(0, &policy_num) < 0)
    return -1;
  return policy(policy_num); 
}