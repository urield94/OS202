#include "types.h"
#include "stat.h"
#include "user.h"

int main(void)
{

  printf(1,"The process is using: %d\n", memsize());

  printf(1,"Allocating more memory\n");
  char *ptr;
  ptr = malloc(2000);
  printf(1,"The process is using: %d\n", memsize());

  printf(1,"Freeing memory\n");
  free(ptr);
  printf(1,"The process is using: %d\n", memsize());
  
  exit(0);
}
