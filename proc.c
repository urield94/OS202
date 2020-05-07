#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

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

int allocpid(void)
{
  pushcli();
  int pid;
  do{
    pid = nextpid;
  }
  while(!cas(&nextpid, pid, pid+1));
  popcli();
  return nextpid;
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
  pushcli();
  
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (cas(&p->state, UNUSED, EMBRYO))
      goto found;
  }
  popcli();
  return 0;

found:
  popcli();
  p->pid = allocpid();

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp; // F.A.Q.11 -  In order to allocate memory for the trapframe backup, see how memory is allocated in allocproc for the existing trapframe.

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  /***************** TASK-2.1.2 *****************/
  /*              Default Handlers              */
  for (int i = 0; i < 32; i++)
  {
    p->signal_handlers[i] = (void *)SIG_DFL;
  }

  p->pending_signals = 0;
  /**********************************************/

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
  pushcli();

  //cas(&p->state, EMBRYO, RUNNABLE); // no!
  p->state = RUNNABLE; // Task 4 - EMBTYO to RUNNABLE

  popcli();
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

  /***************** TASK-2.1.2 *****************/
  /*              Process creation              */
  np->signal_mask = curproc->signal_mask;
  for (int i = 0; i < 32; i++)
  {
    np->signal_handlers[i] = curproc->signal_handlers[i];
  }
  /**********************************************/

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

  pushcli();

  cas(&np->state, EMBRYO, RUNNABLE); // Can be deleted?

  //np->state = RUNNABLE; // Task 4 - EMBRYO to RUNNABLE

  popcli();
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

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

  pushcli();
  //cas(&curproc->state, RUNNING, BEFORE_ZOMBIE);
  curproc->state = BEFORE_ZOMBIE;
  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);  

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE || p->state == BEFORE_ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  //curproc->state = ZOMBIE; // Task 4 - RUNNING to ZOMBIE
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
  
  pushcli();
  for(;;){

    cas(&(curproc->state), RUNNING, BEFORE_SLEEPING);

    curproc->chan = (void *)curproc;

    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      while(p->state == BEFORE_ZOMBIE);
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
        
        popcli();

        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      curproc->chan = 0;
      curproc->state = RUNNING;
      popcli();
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sched(); 
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
  cprintf("start scheduler\n");
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    pushcli();
    //acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if(!cas(&p->state, RUNNABLE, BEFORE_RUNNING)){
        continue;
      }
      //if (p->state != RUNNABLE)
        //continue;

      // F.A.Q.8 -  In order to make SIGCONT and SIGSTOP work correctly, one must modify also the scheduler code.
      if (p->freeze)
      {
        int sig_cont_bits = 1;
        sig_cont_bits = sig_cont_bits << SIGCONT;
        int is_sig_cont_pending = p->pending_signals & sig_cont_bits;
        if (is_sig_cont_pending && p->signal_handlers[SIGCONT] == (void *)SIG_DFL) //nobody changed the SIGCONT handler
        {
          SIGCONT_handler();
          p->pending_signals ^= (1 << SIGCONT); // Remove the signal from the pending_signals
        }
        else
        {
          for (int i = 0; i < 32; i++)
          {
            if (i == SIGKILL || i == SIGSTOP)
            {
              continue;
            }
            int is_sig_i_pending = p->pending_signals & (1 << i);
            if (is_sig_i_pending && p->signal_handlers[i] == SIGCONT_handler)
            {
              SIGCONT_handler();
              p->pending_signals ^= (1 << SIGCONT); // Remove the signal from the pending_signals
              break;
            }
          }
        }
      }
    
    if (p->freeze)
    {
      continue;
    }
    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;  // Task 4 - RUNNABLE to RUNNING

    swtch(&(c->scheduler), p->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.

    cas(&p->state, BEFORE_RUNNABLE, RUNNABLE);
    cas(&p->state, BEFORE_ZOMBIE, ZOMBIE);          //checking different states after process finishes cpu time
    cas(&p->state, BEFORE_SLEEPING, SLEEPING);

    c->proc = 0;
  }
  popcli();
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

  //if (!holding(&ptable.lock))
  //  panic("sched ptable.lock");
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
  struct proc* p = myproc();
  pushcli();
  while(!(cas(&p->state, RUNNING, BEFORE_RUNNABLE)));  
  sched();
  popcli();
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.

  popcli();

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

  pushcli();
  release(lk);
  // Go to sleep.
  p->chan = chan;
  //cas(&p->state, RUNNING, BEFORE_SLEEPING);
  p->state = BEFORE_SLEEPING;   // Task 4 - RUNNING to SLEEPING

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.

    popcli();
    acquire(lk);
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->chan == chan){
      if(cas(&p->state, SLEEPING, BEFORE_RUNNABLE)) {// Task 4 - SLEPPING to RUNNABLE
        p->chan = 0;
        p->state = RUNNABLE;
      }
      if(cas(&p->state, BEFORE_SLEEPING, BEFORE_RUNNABLE)){
        p->chan = 0;
      }
    }
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  pushcli();
  wakeup1(chan);
  popcli();
}

// Send signal to the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid, int signum)
{
  if (signum < 0 || signum > 31)
  {
    return -1;
  }

  struct proc *p;

  pushcli();
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      /***************** TASK-2.2.1 *****************/
      p->pending_signals = p->pending_signals | (1 << signum);
      /**********************************************/
      // Wake process from sleep if necessary.
      if (signum == SIGKILL){ // F.A.Q.2 - Should I wake a SLEEPING process on receiving a signal? Only on SIGKILL.
        cas(&p->state, SLEEPING, BEFORE_RUNNABLE);
        cas(&p->state, BEFORE_SLEEPING, BEFORE_RUNNABLE);
        //p->state = RUNNABLE;  // Task 4 - SLEPPING to RUNNABLE
      }
      popcli();
      return 0;
    }
  }
  popcli();
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
      [ZOMBIE] "zombie",
      [BEFORE_SLEEPING] "-sleep",
      [BEFORE_RUNNABLE] "-runnable",
      [BEFORE_RUNNING] "-running",
      [BEFORE_ZOMBIE] "-zombie",
      [BEFORE_UNUSED] "-unused",
      [BEFORE_EMBRYO] "-embryo"};
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

/***************** TASK-2.1.3 *****************/
/*      Updating the process signal mask      */
uint sigprocmask(uint sigmask)
{
  struct proc *curproc = myproc();
  uint old_maks = curproc->signal_mask;
  curproc->signal_mask = sigmask;
  return old_maks;
}
/**********************************************/

/***************** TASK-2.1.4 *****************/
/*         Registering Signal Handlers        */
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
  if (signum == SIGKILL || signum == SIGSTOP) // F.A.Q.4 -  Attempting to modify the behavior for SIGKILL and SIGSTOP should results in a failed sigaction.
    return -1;
  if (act->sigmask < 0)
    return -1;

  struct proc *curproc = myproc();
  if (oldact != 0)
  {
    oldact->sa_handler = curproc->signal_handlers[signum];
    oldact->sigmask = curproc->signal_mask;
  }
  curproc->signal_handlers[signum] = act->sa_handler; // F.A.Q.3 - Should sigaction signal mask add to the process or set it? Set it.
  curproc->signal_mask = act->sigmask;
  return 0;
}
/**********************************************/

/***************** TASK-2.1.5 ******************/
/*           The sigret system call           */
void sigret()
{
  struct proc *curr_proc = myproc();
  memmove(curr_proc->tf, curr_proc->user_trap_fram_backup, sizeof(struct trapframe));
  curr_proc->tf->esp += sizeof(struct trapframe);
}
/**********************************************/

/***************** TASK-2.3 ******************/
void SIGKILL_handler()
{
  myproc()->killed = 1;
}
void SIGSTOP_handler()
{
  if (myproc()->state == SLEEPING)
    return;
  myproc()->freeze = 1;
}
void SIGCONT_handler()
{
  myproc()->freeze = 0;
}
/**********************************************/

void sig_handler_runner(struct trapframe *tf)
{
  if ((tf->cs & 3) != DPL_USER) // Make sure we in kernel space
    return;

  int sig;
  struct proc *p = myproc();

  if(p == 0){
    return;
  }

  for (int i = 0; i < 32; i++)
  {
    sig = p->pending_signals & (1 << i); // Check whether the i's signal is turnd-on
    if (sig)
    {
      p->pending_signals ^= (1 << i); // Remove the signal from the pending_signals

      // Execute SIGSTOP and SIDKILL immediatly, regardless of the process-signal-mask
      if (i == SIGSTOP)
      {
        SIGSTOP_handler();
        continue;
      }
      if (i == SIGKILL)
      {
        SIGKILL_handler();
        continue;
      }

      if (!(p->signal_mask & sig)) // Execute signal handler if the signal does not blocked by the process-signal-mask
      {

        if (i == SIGCONT && p->signal_handlers[i] == (void *)SIG_DFL)
        {
            SIGCONT_handler(); // TODO - We might want to change this in case the user changed the SIGCONT_handler to be user-space handler.
            continue;
        }
        if (p->signal_handlers[i] == (void *)SIG_DFL)
        {
          SIGKILL_handler();
          continue;
        }
        if (p->signal_handlers[i] == (void *)SIG_IGN)
        {
          continue;
        }

        // F.A.Q.5 -  Including SIGKILL and SIGSTOP bits in the blocked masks (either in sigaction, or in sigprocmask) is ok, but the blocked bit for those signals will be ignored.  - What about SIGCONT?

        // F.A.Q.7 -  If a different signal has its handler as SIGSTOP, then by all definitions, he will act the same, e.g. the process will become frozen, and SIGCONT should awake it up, this is also true for the other case, where you can give a random signal the SIGCONT handler, and it will behave appropriately.

        // F.A.Q.10 -  The trapframe should be backed up before creating the artificial trapframe (that is, when handling pending signals, just before returning to user space) for handling user-space signals. It will be restored upon the sigret syscall.


        //moving back to backup trapframe
        p->tf->esp -= sizeof(struct trapframe);
        memmove((void *)(p->tf->esp), p->tf, sizeof(struct trapframe));   //backup
        p->user_trap_fram_backup = (void *)(p->tf->esp);

        uint size = (uint)&done_implicit_sigret - (uint)&start_implicit_sigret;
        //clearing space for the code
        p->tf->esp -= size;
        memmove((void *)(p->tf->esp), start_implicit_sigret, size);

        *((int *)(p->tf->esp - 4)) = i; //parameter of signal number
        *((int *)(p->tf->esp - 8)) = p->tf->esp;  //return address
        p->tf->esp -= 8;
        //p->old_signal_mask = p->signal_mask;   //TODO: check signal mask, when neet to restore it????

        p->tf->eip = (uint)p->signal_handlers[i];

        //p->signal_mask = p->old_signal_mask;

        // break; // F.A.Q.6 - You can checking the pending array from the start, or continue from where you left off, whatever is more comfortable for you. (To break or not to break)
      }
    }
  }
}