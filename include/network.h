#pragma once

#include <string>
#include <iostream>

int setup_listening_socket (int port);
int init_network (int listen_port);

struct connection
{
    int on_connected ()
    {
        write(fd, "hello\n", 6);
        return 0;
    }
    int on_receive ()
    {
        char buffer[16];
        auto const recvd = read(fd, buffer, sizeof(buffer));
        if ( 0 < recvd ) {
            std::cout << "received:\n" << std::string(buffer, recvd) << std::endl;
        }
        return 0;
    }
    int on_send ()
    {
        return 0;
    }
    int on_close ()
    {
        return 0;
    }

    int fd = -1;
    bool is_used = false;
    bool is_connecting = false;
    uint32_t node_id = -1;
};
