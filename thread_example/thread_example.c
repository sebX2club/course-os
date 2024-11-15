#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

void *threadFunction(void *arg) {
    int *input = (int *)arg;
    printf("Thread received value: %d\n", *input);
    return NULL;
}

int main() {
    pthread_t thread;   // Declare a thread variable
    int argument = 42;  // Data to pass to the thread

    // Create the thread
    if (pthread_create(&thread, NULL, threadFunction, &argument) != 0) {
        perror("Failed to create thread");
        return 1;
    }

    // Wait for the thread to finish
    if (pthread_join(thread, NULL) != 0) {
        perror("Failed to join thread");
        return 1;
    }

    printf("Thread has finished execution\n");
    return 0;
}
