/* Tests filesize on existing, >0 sized file */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int handle, byte_cnt, size;

  CHECK (create ("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK ((handle = open ("test.txt")) > 1, "open \"test.txt\"");

  byte_cnt = write (handle, sample, sizeof sample - 1);
  size = filesize(handle);
  if (byte_cnt != size)
    fail ("write() returned %d but the size of the file is %d", byte_cnt, size);
}