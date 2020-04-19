#include "types.h"
#include "stat.h"
#include "user.h"

int main(void)
{
printf(1,"PID\tPS_PRIORITY\tSTIME\tRETIME\tRTIM\n");
int number_of_childs = 3;
int i;
for (i = 0; i < number_of_childs; i++) {
    int pid = fork();
    if(pid < 0){
        exit(1);
    }
    else if (pid > 0){ // Parent process 
    }
    else{ // Child process 
        switch (i)
        {
        /*The first child will be set as low priority, the second child will be mediumpriority, 
        and the last child will be set as high priority (using boththeset_cfs_priority() and the set_ps_priority() functions)*/
            case 0:
                set_ps_priority(10);
                //set_cfs_priority(3);
                break;
            case 1:
                set_ps_priority(5);
                //set_cfs_priority(2);
                break;
            case 2:
                set_ps_priority(1);
                //set_cfs_priority(1);
                break;
            default:
                set_ps_priority(5);
        }
        int i = 1000000;
        int dummy = 0;
        while(i--){
            dummy+=i;
        }
        struct perf proformance;
        proc_info(&proformance); // Each child process should then print its statistics (using system call proc_info()) before it exits
        exit(0);
    }
}

for (i = 0; i < number_of_childs; i++) {
    int status;
    wait(&status); 
}
exit(0);
}
