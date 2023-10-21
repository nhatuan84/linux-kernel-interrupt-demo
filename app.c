#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
 
#define SIG_GPIO    18 //should choose the value that does not kill the process
//https://github.com/tetang1230/zhoulifa/blob/master/strsignal.c

#define REG_SIG_GPIO _IOW('s', 1, int32_t*)
 
static int done = 0;

 
void sig_event_handler(int sign, siginfo_t *info, void *unused)
{
    int sigval;
    
    if (sign == SIG_GPIO) 
    {
        done = info->si_int;
        printf ("gpio signal from kernel %d\n", done);
    }
}
 
int main()
{
    int fd;
    int32_t value, number;
    struct sigaction act;
 
    /* install custom signal handler */
    sigemptyset(&act.sa_mask);
    act.sa_flags = (SA_SIGINFO | SA_RESTART);
    act.sa_sigaction = sig_event_handler;
    sigaction(SIG_GPIO, &act, NULL);
 
    printf("open dev\n");
    fd = open("/dev/misc_dev", O_RDWR);
    if(fd < 0) 
    {
            printf("cannot open device file...\n");
            return 0;
    }
 
    printf("register sig\n");
    ioctl(fd, REG_SIG_GPIO, NULL);

    write (fd, "1", strlen("1"));

    while(1) 
    {
        printf("waiting for signal\n");
        while (!done);
        printf("got signal\n");
        done = 0;
    }
 
    printf("close dev\n");
    close(fd);
}