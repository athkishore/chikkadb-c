#include<stdio.h>
#include<errno.h>
#include<stdlib.h>

void msg(const char *msg) {
  fprintf(stderr, "%s\n", msg);
}

void msg_errno(const char *msg) {
  fprintf(stderr, "[%d] %s\n", errno, msg);
}

void die(const char *msg) {
  fprintf(stderr, "[%d] %s\n", errno, msg);
  abort();
}