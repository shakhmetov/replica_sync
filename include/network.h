#pragma once

#include <string>

int setup_listening_socket (int port);
int init_network (int listen_port);

struct connection
{
    int fd = -1;
    bool is_used = false;
    uint32_t node_id = -1;
};
