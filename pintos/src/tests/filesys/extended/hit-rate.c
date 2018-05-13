#include <random.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#define BUF_SIZE 1024

static const char file_name[] = "data";
static char buf[BUF_SIZE];

void
test_main (void) 
{
  int start;
  int finish;
  int fd;
  CHECK (create (file_name, BUF_SIZE), "create \"%s\"", file_name);
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  random_bytes (buf, BUF_SIZE);
  CHECK (write (fd, buf, BUF_SIZE) > 0, "write \"%s\"", file_name);
  msg ("close \"%s\"", file_name);
  close (fd);
  cache_reset ();
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  CHECK ((read(fd, buf, BUF_SIZE)) > 0, "read \"%s\"", file_name);
  start = cache_hits();
  msg ("close \"%s\"", file_name);
  close (fd);
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  CHECK ((read(fd, buf, BUF_SIZE)) > 0, "read \"%s\"", file_name);
  msg ("close \"%s\"", file_name);
  finish = cache_hits();
  close (fd);
  CHECK ((finish - start) > start, "more cache hits");

}
