#include<connection.h>

const int PORT = 27017;

int main() {
  int fd = create_socket(PORT);
  start_listen_nb(fd);

  return 0;
}