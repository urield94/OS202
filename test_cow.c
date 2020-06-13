#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

/************************Tests pagining framework section********************************/
/****                                Task 1                                          ****/

void test_paging1(void)
{
    int i, pid;
    int *child_array = malloc(20 * PGSIZE);
    int child_array_length = (20 * PGSIZE) /sizeof(int);
    for (i = 0; i < child_array_length; i++)
    {
        child_array[i] = i + 1;
    }
    printf(1, "forking...\n");
    if((pid=fork()) == 0){
        printf(1, "In Child process....");
        for (i = 0; i < child_array_length; i++)
        {
            if(child_array[i] != i+1){
                printf(2, "Expected: %d, Found: %d\n", (i+1), (child_array[i]));
                break;
            }
        }
        if(i < child_array_length - 1)
            printf(2, "Fork didnt copied the pages correctly, index:%d\n", i);
        else
            printf(1, "Fork copied the pages correctly\n");
    }
    else if (pid > 0)
    {
        for (i = 0; i < child_array_length; i++)
        {
            if (child_array[i] != i + 1)
            {
                printf(2, "Expected: %d, found: %d\n", (i + 1), (child_array[i]));
                break;
            }
        }
        if (i < child_array_length - 1)
            printf(2, "Some problem with paging, with i: %d.\n", i);
        else
            printf(1, "Paging working correctly.\n");
        wait();
    }
    else
        printf(2, "Fork failed!\n");
}

void test_paging2(void)
{
    
}

void test_paging3(void)
{
}

void tests_paging_framework(void)
{
    printf(1, "Paging Test1 starts....\n");
    test_paging1();
    printf(1, "Paging Test1 done!\n");
    printf(1, "--------------------\n");
    printf(1, "Paging Test2 starts....\n");
    test_paging2();
    printf(1, "Paging Test2 done!\n");
    printf(1, "--------------------\n");
    printf(1, "Paging Test3 starts....\n");
    test_paging3();
    printf(1, "Paging Test3 done!\n");
    printf(1, "--------------------\n");
}

/****************************************************************************************/

/***********************************Tests_Cow Section************************************/
/****                                   TASK 2                                       ****/

void test_cow1()
{
    printf(1, "%d free pages before forking\n", gnofp());
    printf(1, "Parent and Child share the global variable a \n");
    int pid = fork();
    int a = 1;
    if (pid == 0)
    {
        printf(1, "Child: a = %d\n", a);
        printf(1, "%d free pages before any changes\n", gnofp());
        a = 2;
        printf(1, "Child: a = %d\n", a);
        printf(1, "%d free pages after changing a\n", gnofp());
        exit();
    }
    printf(1, "Parent: a = %d\n", a);
    wait();
    printf(1, "Parent: a = %d\n", a);
    printf(1, "%d free pages after wait\n", gnofp());
    return;
}

void test_cow2()
{
    printf(1, "%d free pages before fork-1\n", gnofp());
    if (fork() == 0)
    {
        exit();
    }
    else
    {
        printf(1, "%d free pages before fork-2\n", gnofp());
        if (fork() == 0)
        {
            printf(1, "%d free pages before changes in Child-2\n", gnofp());
            printf(1, "%d free pages after changes in Child-2\n", gnofp());
            exit();
        }
        wait();
        printf(1, "%d free pages after reaping Child-1\n", gnofp());
    }
    wait();
    printf(1, "%d free pages after reaping Child-2\n", gnofp());
    return;
}

void tests_cow(void)
{
    printf(1, "Cow Test1 starts....\n");
    test_cow1();
    printf(1, "Cow Test1 done!\n");
    printf(1, "--------------------\n");
    printf(1, "Cow Test2 starts....\n");
    test_cow2();
    printf(1, "Cow Test2 done!\n");
    printf(1, "--------------------\n");
}

/*******************************************************************************************/

int main(void)
{
    tests_paging_framework();
    tests_cow();

    exit();
}