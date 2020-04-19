#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include <limits.h>

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

struct min_acc_properties
{
  int min_acc_proc_index;
  long long min_acc;
  int there_is_no_runnbale;
};
static struct min_acc_properties get_min_acc_prop();
static long long get_min_acc(struct min_acc_properties min_acc_prop);
static void set_woken_acc(long long glob_min_acc, int num_of_runnable, int run_on_chan[], int there_is_no_runnbale);


void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
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
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  /****************************************Task-4.2***************************************************
    Each time a new process is created the system should set the value of its accumulator field 
    to the minimum value of the accumulator fields of all the runnable/running processes.*/
  struct min_acc_properties min_acc_props;
  if (sched_type == 1)
    min_acc_props = get_min_acc_prop();
  /****************************************************************************************************/

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  /**********Task-4.2****************/
  if (sched_type == 1)
  {
    p->accumulator = get_min_acc(min_acc_props);
    /**********************************/
  }
  p->ps_priority = 5; /*The priority of a new processis 5*/

  /**********Task-4.3****************/
  if(sched_type == 2){
    p->cfs_priority = 2;
    p->decay_factor = 1;
  }
  /**********************************/

  p->stime = 0; 
  p->rtime= 0; 
  p->retime = 0; 

  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

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
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  /************Task 4.3*************/
  np-> cfs_priority = curproc->cfs_priority;
  np-> decay_factor = curproc->decay_factor;
  /********************************/

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(int status)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  curproc->exit_status = status;

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
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
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
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
int wait(int *status)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        if (status != null)
        {
          *status = p->exit_status;
        }
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
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
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
void scheduler(void)
{
  struct proc *p = null;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    if (sched_type == 1)
    {
      /*******************************Task-4.2*****************************************
                                   Priority Scheduling
        Whenever the scheduler needs to select the next process to execute, it
        will choose the process with the lowest accumulated number*/
      struct min_acc_properties min_acc_props = get_min_acc_prop();
      if (min_acc_props.min_acc_proc_index != -1)
      {
        p = &ptable.proc[min_acc_props.min_acc_proc_index];

        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&(c->scheduler), p->context);
        switchkvm();
        c->proc = 0;
        if (p->state != ZOMBIE)
          p->accumulator += p->ps_priority;
      }
    }
    else if (sched_type == 0)
    {
      /*******************************Task-4.1*****************************************
                                       Round-Robin*/
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if (p->state != RUNNABLE)
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

        // p->accumulator += (p->ps_priority);
      }
    }
    /**************************Task-4.3*****************************************************
                                CFS_PRIORITY*/
    else if (sched_type == 2)
    {
      struct proc *best = null;
      double min_process_ratio = __DBL_MAX__;
      double cur_process_ratio;

      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if (p -> state != RUNNABLE){
          continue;
        }
        else{
          if((p -> rtime + p -> retime + p-> stime) > 0){
            cur_process_ratio = ((p -> rtime) * (p -> decay_factor)) / (p -> rtime + p -> retime + p -> stime);
            if (cur_process_ratio < min_process_ratio){
              min_process_ratio = cur_process_ratio;
              best = p;
            }
          }
        }
      }
      if(best != null){
        c->proc = best;
        switchuvm(best);
        best->state = RUNNING;
        swtch(&(c->scheduler), best->context);
        switchkvm();
        c-> proc = 0;
      }
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
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
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
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { //DOC: sleeplock2
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

  /****************************************Task-4.2***************************************************
    Each time a process shifts from the blocked state to the runnable, state the system should set 
    the value of its accumulator field to the minimum value of the accumulator fields of all the runnable/running processes.*/
  struct min_acc_properties min_acc_props;
  int run_on_chan[NPROC];
  int i = 0;
  int num_of_runnable = 0;
  if (sched_type == 1)
    min_acc_props = get_min_acc_prop();
  /******************************************************************************************************/
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    run_on_chan[i] = 0;
    if (p->state == SLEEPING && p->chan == chan)
    {
      /**********Task-4.2****************/
      if (sched_type == 1)
      {
        num_of_runnable++;
        run_on_chan[i] = 1;
        // p->accumulator = get_min_acc(min_acc_props);
      }
      /**********************************/
      p->state = RUNNABLE;
    }
    i++;
  }
  if(sched_type == 1)
    set_woken_acc(min_acc_props.min_acc, num_of_runnable, run_on_chan, min_acc_props.there_is_no_runnbale);
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);

  /****************************************Task-4.2***************************************************
    Each time a process shifts from the blocked state to the runnable, state the system should set 
    the value of its accumulator field to the minimum value of the accumulator fields of all the runnable/running processes.*/
  struct min_acc_properties min_acc_props;
  if (sched_type == 1)
    min_acc_props = get_min_acc_prop();
  /******************************************************************************************************/

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
      {
        /**********Task-4.2****************/
        if (sched_type == 1)
          p->accumulator = get_min_acc(min_acc_props);
        /**********************************/
        p->state = RUNNABLE;
      }
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
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

/*Task-4.2*/
int set_ps_priority(int np)
{
  if (np < 1 || np > 10)
    return -1;
  myproc()->ps_priority = np;
  return 0;
}

/*Task-4.3*/
int set_cfs_priority(int np)
{
  if (np < 1|| np > 3)
    return -1;
  myproc()->cfs_priority = np;
  myproc()->decay_factor = 0.5 + (0.25 * np);
  return 0;
}

static struct min_acc_properties get_min_acc_prop()
{
  struct proc *p;

  int min_acc_proc_index = -1;
  int i = 0;
  long long min_acc = LLONG_MAX;
  int there_is_no_runnbale = 1;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if ((p->state == RUNNABLE || p->state == RUNNING) && (p->accumulator < min_acc))
    {
      if (p->state == RUNNABLE)
        min_acc_proc_index = i;
      min_acc = p->accumulator;
      there_is_no_runnbale = 0;
    }
    i++;
  }
  struct min_acc_properties res;
  res.min_acc_proc_index = min_acc_proc_index;
  res.min_acc = min_acc;
  res.there_is_no_runnbale = there_is_no_runnbale;
  return res;
}

static long long get_min_acc(struct min_acc_properties min_acc_prop)
{
  long long accumulator;
  if (min_acc_prop.there_is_no_runnbale)
    accumulator = 0;
  else
    accumulator = min_acc_prop.min_acc;
  return accumulator;
}

/*************************************FORUM***********************************
 * If there are no running/runnable processes at a given time and a few processes that are sleeping 
 * on the same channel are waking up,then we need to set their new accumulators to the minimum of
 *  the their old accumulators (prior to them sleeping)*/
static void set_woken_acc(long long glob_min_acc, int num_of_runnable, int run_on_chan[], int there_is_no_runnbale)
{
  struct proc *p;
  long long min_acc = LLONG_MAX;
  int single_process = 0;
  if (num_of_runnable == 0)
    return;
  else if (num_of_runnable == 1)
  {
    if (there_is_no_runnbale)
      single_process = 1;
  }

  int i = 0;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (run_on_chan[i])
    {
      if (single_process)
      {
        p->accumulator = 0;
      }
      else
      {
        if (there_is_no_runnbale)
        {
          if (p->accumulator < min_acc)
            min_acc = p->accumulator;
        }
        else
        {
          p->accumulator = glob_min_acc;
        }
      }
    }
    i++;
  }

  i = 0;
  if (!single_process && there_is_no_runnbale)
  {
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (run_on_chan[i])
        p->accumulator = min_acc;
      i++;
    }
  }
}
/******************************************************************************************************/
/**/

/*Task-4.4*/
int policy(int st)
{
  if (st == 0 || st == 1 || st == 2)
  {
    sched_type = st;
  }
  else
  {
    sched_type = 0;
  }
  return st;
}
/**/

/*Task-4.5*/
void update_statistics()
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == RUNNABLE){
      p->retime++;
    }
    else if(p->state == RUNNING){
      p->rtime++;
    }
    else if(p->state == SLEEPING){
      p->stime++;
    }
  }
}

struct pref *get_prefomance(){
  return (struct pref*)myproc()->ps_priority;
}

int proc_info(struct perf *proformance){ 
  cprintf("%d\t%d\t\t%d\t%d\t%d\n", 
          myproc()->pid,
          proformance->ps_priority,
          proformance->stime,
          proformance->retime,
          proformance->rtime);
  return 0;
}
/**/
