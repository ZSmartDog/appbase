//
// Created by zwg on 19-8-28.
//

#ifndef APPBASE_MAIN_HPP
#define APPBASE_MAIN_HPP
#include <appbase/application.hpp>
#include <iostream>
#include <boost/exception/diagnostic_information.hpp>
#include <thread>
#include <memory>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>
#include <string>
#include <boost/asio/ip/tcp.hpp>
#include <array>
#include <fc/network/message_buffer.hpp>
#include <appbase/plugin.hpp>

struct database { };

namespace bpo = boost::program_options;

using bpo::options_description;
using bpo::variables_map;
using namespace std;
using namespace boost::asio::ip;
using appbase::app;

constexpr auto message_header_size = 4;
constexpr auto def_send_buffer_size_mb = 4;
constexpr auto def_send_buffer_size = 1024*1024*def_send_buffer_size_mb;

using namespace std;
using boost::asio::ip::tcp;
using socket_ptr = std::shared_ptr<tcp::socket>;
void func(string s);

class connection;

using connection_ptr = std::shared_ptr<connection>;
using connection_wptr = std::weak_ptr<connection>;

class net_plugin_impl {
    friend class net_plugin;
private:
    boost::asio::io_service service;
    unique_ptr<boost::asio::steady_timer> keepalive_timer;
    boost::asio::steady_timer::duration   keepalive_interval{std::chrono::seconds{1}};
    shared_ptr<tcp::resolver> resolver;
    unique_ptr<tcp::acceptor> acceptor;
    tcp::endpoint listen_endpoint;
    string host = string("127.0.0.1");
    string port = string("9999");
    uint32_t num_clients = 0;
    uint32_t started_sessions = 0;
    std::set< connection_ptr > connections;
public:
    void ticker();
    void close(const connection_ptr& c);
    bool start_session(const connection_ptr& con);
    void start_listen_loop();
    void start_read_message(const connection_ptr& conn);
};


class connection : public std::enable_shared_from_this<connection> {
public:
    socket_ptr socket;
    bool connecting = false;
    fc::message_buffer<1024*1024*20> _messageBuffer;
    boost::optional<std::size_t> _outStandingReadBytes;
    void close() {
        if(socket) socket->close();
        connecting = false;
    }
    bool process_next_message(net_plugin_impl& impl, uint32_t message_length) {
        char * p = new char[message_length+1];
        _messageBuffer.read(p, message_length);
        p[message_length] = '\0';
        cout << p << endl;
        cout << "current thread:" << this_thread::get_id() << endl;
        appbase::app().get_io_service().post(std::bind(func, string(p)));
        delete[] p;
        return true;
    }

    explicit connection( socket_ptr s ):socket( s ),connecting(true) {}
};

#endif //APPBASE_MAIN_HPP
