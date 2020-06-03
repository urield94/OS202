#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;

  for(i = 1; i < 100; i++)
    printf(1, "%d\n", gnofp());
  exit();
}
