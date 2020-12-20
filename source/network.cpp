#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <iostream>
#include <unistd.h>
#include <sys/ioctl.h>
#include <network.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <vector>
#include <list>

int setup_listening_socket (int port) 
{
    int sock;
    struct sockaddr_in srv_addr;

    sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock == -1) {
        std::cerr << "can't create TCP socket" << std::endl;
        return -1;
    }

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        std::cerr << "can't set option TCP socket" << std::endl;
    }

    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (const struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        close(sock);
        std::cerr << "can't bind TCP socket to port " << port << std::endl;
        return -1;
    }
    
    if (listen(sock, 10) < 0) {
        close(sock);
        std::cerr << "can't listen TCP socket to port " << port << std::endl;
        return -1;
    }

    return (sock);
}

int s_listen_sock = -1;

std::list<connection> s_connections;

int init_network (int listen_port)
{
    s_listen_sock = setup_listening_socket(listen_port);

    int ports[] = {2048, 2049, 2050};

    // struct pollfd fds[4];

    // fds[0] = { .fd = s_listen_sock, .events = POLLIN, .revents = 0 };
    s_connections.push_back( {s_listen_sock, true, 0xffffffff} );

    int fd = -1;
    for (uint32_t node_id = 0; node_id < 3; ++node_id ) {
        fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_port = htons(ports[node_id]);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connect(fd, (sockaddr *)&addr, sizeof(addr));
        // fds[ 1 + node_id ] = { .fd = fd, .events = POLLOUT, .revents = 0 };
        s_connections.push_back( {fd, false, node_id} );
    }

    for ( ; ; ) {
        auto const count = s_connections.size();
        struct pollfd pfds[count];
        size_t i = 0;
        for ( auto const & c : s_connections ) {
            pfds[i] = { .fd = c.fd, .events = (c.is_used ? POLLIN: POLLOUT), .revents = 0 };
            ++i;
        }
        
        int const cnt = poll(pfds, count, 10);
        auto f = s_connections.begin(), l = s_connections.end();
        int events = 0;
        for ( i = 0 ; f != l && i < count && events < cnt; ++i ) {
            if ( pfds[i].revents & (POLLERR | POLLHUP) ) {
                std::cout << "close connection" << std::endl;
                close(pfds[i].fd);
                s_connections.erase(f++);
                ++events;
            } else {
                if ( pfds[i].revents & POLLIN ) {
                    ++events;
                    if ( pfds[i].fd == s_listen_sock ) {
                        struct sockaddr_in remote;
                        socklen_t saddr = sizeof(remote);
                        int fd = accept4(s_listen_sock, (sockaddr *)&remote, &saddr, SOCK_NONBLOCK);
                        s_connections.push_back( {fd, true, 0xffffffff} );
                        std::cout << "incoming connection " << std::endl;
                        ++f;
                    } else {
                        char buffer[128];
                        auto const recvd = read(pfds[i].fd, buffer, sizeof(buffer));
                        if ( 0 < recvd ) {
                            std::cout << "incoming data" << std::endl;
                            std::cout << std::string(buffer, recvd) << std::endl;
                            ++f;
                        } else {
                            std::cout << "close connection" << std::endl;
                            close(pfds[i].fd);
                            s_connections.erase(f++);
                        }
                    }
                } else if ( pfds[i].revents & POLLOUT ) {
                    ++events;
                    std::cout << "outgoing connection accepted" << std::endl;
                    f->is_used = true;
                    pfds[i].events = POLLIN;
                    write(pfds[i].fd, "hello!!\n", 8);
                    ++f;
                } else {
                    ++f;
                }
                
            }
        }

    }

}
