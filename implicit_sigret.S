#include "syscall.h"
#include "traps.h"

.globl done_implicit_sigret
.globl start_implicit_sigret

start_implicit_sigret:
  movl $SYS_sigret, %eax // Set the sys_sigret system call to be executed
  int $T_SYSCALL // Execute sigret and return to the signals check
done_implicit_sigret: