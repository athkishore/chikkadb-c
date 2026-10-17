#ifndef CONNECTION_H
#define CONNECTION_H

#define MAX_FDS 1024

int create_socket(int port);
void start_listen_nb(int fd);
#endif