#include <unistd.h>

// source https://jameshfisher.com/2018/02/19/how-to-syscall-in-c/
// OS independent
int main(void) {
  write(STDOUT_FILENO, "hello, world!\n", 14); // STDOUT_FILENO is 1
  return 0;
}