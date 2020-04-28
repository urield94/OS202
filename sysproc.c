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


/***************** TASK-2.1.3 *****************/
/*      Updating the process signal mask      */
int sys_sigprocmask(void){
  int sigmask; // TODO - Check with Benny weather to get int or uint.
  if(argint(0, &sigmask) < 0)
    return -1;
  return sigprocmask((uint)sigmask); 
}
/**********************************************/


/***************** TASK-2.1.4 *****************/
/*         Registering Signal Handlers        */
int sys_sigaction(void){
  int signum;
  if(argint(0, &signum) < 0)
    return -1;
  
  struct sigaction* act;
  if(argptr(1, (void*)&act, sizeof(*act)) < 0)
    return -1;

  struct sigaction* oldact;
  if(argptr(1, (void*)&oldact, sizeof(*oldact)) < 0)
    return -1;

  return sigaction(signum, act, oldact);
}
/**********************************************/


/***************** TASK-2.1.5******************/
/*           The sigret system call           */
int sys_sigret(void){
 // Not implemented yet
 return 0;
}
/**********************************************/

