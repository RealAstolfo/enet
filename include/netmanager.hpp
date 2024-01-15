#ifndef NETMANAGER_HPP
#define NETMANAGER_HPP

#include <asio.hpp>

#include "common/networking/netvar.hpp"
#include <vector>

namespace net = asio;
using io_context = net::io_context;
using tcp_endpoint = net::ip::tcp::endpoint;
using tcp_socket = net::ip::tcp::socket;
using tcp_resolver = net::ip::tcp::resolver;
using error_code = std::error_code;


struct net_manager {
    io_context context;
    tcp_resolver resolver;
    tcp_socket socket;
    std::vector<char> recv_buffer;
    std::vector<char> send_buffer;
    std::thread read_thrd;
    std::thread write_thrd;
    net_manager(std::string host, size_t port);
    void run(std::error_code& ec, tcp_endpoint& endpoint);
    void resolve(std::error_code& ec, tcp_resolver::results_type& endpoints);
};

#endif
