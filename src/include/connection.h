#define MAX_FDS 1024

int create_socket(int port);
void start_listen_nb(int fd);