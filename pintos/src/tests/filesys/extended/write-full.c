#include <random.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#define BUF_SIZE 66560

static const char file_name[] = "data";
static char buf[BUF_SIZE];

void
test_main (void) 
{
  int start;
  int finish;
  int fd;
  CHECK (create (file_name, sizeof buf), "create \"%s\"", file_name);
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  random_bytes (buf, sizeof buf);
  start = disk_reads();
  CHECK (write (fd, buf, sizeof buf) > 0, "write \"%s\"", file_name);
  finish = disk_reads();
  msg ("close \"%s\"", file_name);
  close (fd);
  CHECK ((start == finish), "no additional reads");
}