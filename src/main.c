#include<stdio.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<errno.h>
#include<fcntl.h>
#include<stdbool.h>

#include<debug.h>
#include<connection.h>

const int PORT = 27017;

int main() {
  int fd = create_socket(PORT);
  start_listen_nb(fd);

  return 0;
}