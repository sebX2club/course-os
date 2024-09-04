#include <unistd.h>
#include <sys/syscall.h>

// source https://jameshfisher.com/2018/02/19/how-to-syscall-in-c/
// os dependent
int main(void) {
  // Sys_write = System call number for write (linux x86-64 = 1, macOS = 4)
  syscall(SYS_write, 1, "hello, world!\n", 14);
  return 0;
}