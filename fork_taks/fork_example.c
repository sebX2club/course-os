#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    pid_t pid1, pid2;

    printf("Parent process PID: %d\n", getpid());

    // Create the first child process
    pid1 = fork();

    if (pid1 < 0) {
        perror("Fork failed");
        exit(1);
    } else if (pid1 == 0) {
        // Inside Child1 process
        printf("Child1 created with PID: %d\n", getpid(), getppid());

        // Create grandchild process under Child1
        pid_t grandchild1 = fork();

        if (grandchild1 < 0) {
            perror("Fork failed");
            exit(1);
        } else if (grandchild1 == 0) {
            // Inside Grandchild1 process
            printf("Grandchild1 created with PID: %d, Parent PID: %d\n", getpid(), getppid());
            exit(0); // Grandchild1 exits
        } else {
            wait(NULL); // Child1 waits for Grandchild1 to finish
            printf("Child1 with PID %d is exiting.\n", getpid());
            exit(0); // Child1 exits
        }

    } else {
        // Create the second child process
        pid2 = fork();

        if (pid2 < 0) {
            perror("Fork failed");
            exit(1);
        } else if (pid2 == 0) {
            // Inside Child2 process
            printf("Child2 created with PID: %d\n", getpid(), getppid());

            // Create grandchild process under Child2
            pid_t grandchild2 = fork();

            if (grandchild2 < 0) {
                perror("Fork failed");
                exit(1);
            } else if (grandchild2 == 0) {
                // Inside Grandchild2 process
                printf("Grandchild2 created with PID: %d, Parent PID: %d\n", getpid(), getppid());
                exit(0); // Grandchild2 exits
            } else {
                wait(NULL); // Child2 waits for Grandchild2 to finish
                printf("Child2 with PID %d is exiting.\n", getpid());
                exit(0); // Child2 exits
            }

        } else {
            // Parent process waits for both children to finish
            wait(NULL);
            wait(NULL);
            printf("Parent with PID %d is exiting.\n", getpid());
        }
    }

    return 0;
}
