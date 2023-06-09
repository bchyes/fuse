#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
int main(){
    
    pid_t pid;
    // OPEN FILES
    int fd;
    fd = open("test.txt" , O_RDWR | O_CREAT | O_TRUNC);
    if (fd == -1)
    {
        /* code */
        printf("file exist\n");
        //return 1;
    }
    //write 'hello fcntl!' to file

    /* code */
    write(fd, "hello fcntl!", 12);

    

    // DUPLICATE FD

    /* code */
    fcntl(fd, F_DUPFD, 0);
    
    

    pid = fork();

    if(pid < 0){
        // FAILS
        printf("error in fork");
        return 1;
    }
    
    struct flock fl;

    if(pid > 0){
        // PARENT PROCESS
        //set the lock
        fl.l_type = F_WRLCK;
        fcntl(fd, F_SETLK, &fl);

        //append 'b'
        write(fd, "b", 1);
        
        //unlock
        sleep(3);

        //printf("%s", str); the feedback should be 'hello fcntl!ba'
        
        exit(0);

    } else {
        // CHILD PROCESS
        sleep(2);
        //get the lock
        fcntl(fd, F_GETLK, &fl);
        //append 'a'
        write(fd, "a", 1);

        exit(0);
    }
    close(fd);
    return 0;
}