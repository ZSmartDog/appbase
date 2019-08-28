#include "main.hpp"

void func(string s) {
    cout << "string in" << __func__ << ":" << s << ", thread_id:" << this_thread::get_id() << endl;
}

class chain_plugin : public appbase::plugin<chain_plugin>
{
public:
    APPBASE_PLUGIN_REQUIRES();

    virtual void set_program_options( options_description& cli, options_description& cfg ) override
    {
        cfg.add_options()
                ("readonly", "open the database in read only mode")
                ("dbsize", bpo::value<uint64_t>()->default_value( 8*1024 ), "Minimum size MB of database shared memory file")
                ;
        cli.add_options()
                ("replay", "clear chain database and replay all blocks" )
                ("reset", "clear chain database and block log" )
                ;
    }

    void plugin_initialize( const variables_map& options ) { std::cout << "initialize chain plugin\n"; }
    void plugin_startup()  { std::cout << "starting chain plugin \n"; }
    void plugin_shutdown() { std::cout << "shutdown chain plugin \n"; }

    database& db() { return _db; }

private:
    database _db;
};

class net_plugin : public appbase::plugin<net_plugin>
{
private:
    thread net_thread;
    std::unique_ptr<class net_plugin_impl> my;
    static boost::asio::io_service io_service;
    void _plugin_startup() {
        cout << "sub_thread:" << std::this_thread::get_id() << endl;
        my->ticker();
        my->acceptor->open(my->listen_endpoint.protocol());
        my->acceptor->set_option(tcp::acceptor::reuse_address(true));
        my->acceptor->bind(my->listen_endpoint);
        my->acceptor->listen();
        my->start_listen_loop();
        io_service.run();
    }
public:
    static boost::asio::io_service& get_io_service() {
        return io_service;
    }
    net_plugin(){
        cout << "main_thread:" << std::this_thread::get_id() << endl;
        my = make_unique<class net_plugin_impl>();
    };
    ~net_plugin(){
    };

    APPBASE_PLUGIN_REQUIRES( (chain_plugin) );


    virtual void set_program_options( options_description& cli, options_description& cfg ) override
    {
        cfg.add_options()
                ("listen-endpoint", bpo::value<string>()->default_value( "127.0.0.1:9876" ), "The local IP address and port to listen for incoming connections.")
                ("remote-endpoint", bpo::value< vector<string> >()->composing(), "The IP address and port of a remote peer to sync with.")
                ("public-endpoint", bpo::value<string>()->default_value( "0.0.0.0:9876" ), "The public IP address and port that should be advertized to peers.")
                ;
    }

    void plugin_initialize( const variables_map& options ) {
        std::cout << "initialize net plugin\n";
        my->resolver = std::make_shared<tcp::resolver>( std::ref(net_plugin::get_io_service()));
        tcp::resolver::query query( tcp::v4(), my->host.c_str(), my->port.c_str());
        my->listen_endpoint = *my->resolver->resolve( query );
        my->acceptor.reset( new tcp::acceptor(net_plugin::get_io_service()));
        my->keepalive_timer = std::make_unique<boost::asio::steady_timer>(std::ref(net_plugin::get_io_service()));
    }
    void plugin_startup()  {
        std::cout << "starting net plugin \n";
        net_thread = thread(&net_plugin::_plugin_startup, this);
    }
    void plugin_shutdown() {
        std::cout << "shutdown net plugin \n";
        net_thread.join();
    }

};

boost::asio::io_service net_plugin::io_service;


void net_plugin_impl::ticker() {
    keepalive_timer->expires_from_now(keepalive_interval);
    keepalive_timer->async_wait([this](boost::system::error_code ec) {
        ticker();
        if (ec) cout << "error in ticker." << endl;
        cout << "ticker is triggered." << this_thread::get_id() << endl;
    });
}

void net_plugin_impl::close(const connection_ptr& c) {
    c->close();
}

bool net_plugin_impl::start_session(const connection_ptr& con) {

    boost::asio::ip::tcp::no_delay nodelay( true );
    boost::system::error_code ec;
    con->socket->set_option( nodelay, ec );
    if (ec) {
        cout << "error in " << __func__ << "," << __LINE__ << endl;
        con->connecting = false;
        close(con);
        return false;
    }
    else {
        start_read_message( con );
        ++started_sessions;
        return true;
    }
}

void net_plugin_impl::start_listen_loop() {
    auto socket = std::make_shared<tcp::socket>( std::ref( net_plugin::get_io_service() ) );
    acceptor->async_accept( *socket, [socket,this]( boost::system::error_code ec ) {
        if( !ec ) {
            if (ec) cout << "error in " << __func__ << "," << __LINE__ << endl;
            else {
                ++num_clients;
                connection_ptr c = std::make_shared<connection>( socket );
                connections.insert( c );
                start_session( c );
            }
        } else {
            cout << "async_accept failed." << endl;
            switch (ec.value()) {
            case ECONNABORTED:
            case EMFILE:
            case ENFILE:
            case ENOBUFS:
            case ENOMEM:
            case EPROTO:
                break;
            default:
                return;
            }
        }
        start_listen_loop();
    });
}

void net_plugin_impl::start_read_message(const connection_ptr& conn) {
    if(!conn->socket) return;
    connection_wptr weak_conn = conn;
    std::size_t minimum_read = conn->_outStandingReadBytes ? *conn->_outStandingReadBytes:message_header_size;
    auto completion_handler = [minimum_read](boost::system::error_code ec, std::size_t bytes_transferred) -> std::size_t {
        if(ec||bytes_transferred >= minimum_read) return 0;
        return minimum_read - bytes_transferred;
    };
    boost::asio::async_read(
            *conn->socket,
            conn->_messageBuffer.get_buffer_sequence_for_boost_async_read(),
            completion_handler,
            [this, weak_conn](boost::system::error_code ec, std::size_t bytes_transferred) {
                auto conn = weak_conn.lock();
                conn->_outStandingReadBytes.reset();
                try{
                    if(!ec) {
                        //指针移动到下次写数据的位置
                        conn->_messageBuffer.advance_write_ptr(bytes_transferred);
                        while(conn->_messageBuffer.bytes_to_read() > 0) {
                            auto bytesInBuffer = conn->_messageBuffer.bytes_to_read();
                            if(bytesInBuffer < message_header_size) {
                                //当前缓冲区的字节数不足4个字节(header_size存储数)
                                //接下来去socket上读取这么多字节，刚好将header读取到
                                conn->_outStandingReadBytes.emplace(message_header_size - bytesInBuffer);
                                break;
                            }
                            // 缓冲区足够四个字节
                            uint32_t messageLength;
                            auto index = conn->_messageBuffer.read_index();
                            conn->_messageBuffer.peek(&messageLength, sizeof(messageLength), index);
                            cout << "messageLength:" << messageLength << endl;
                            // 异常场景
                            if(messageLength > def_send_buffer_size*2 || messageLength == 0) {
                                cerr << "Unexpected length of this message." << endl;
                                conn->close();
                                return;
                            }
                            auto totalMessageLength = messageLength + message_header_size;
                            //当前缓存区已经有本条完整的数据
                            if(bytesInBuffer >= totalMessageLength) {
                                conn->_messageBuffer.advance_read_ptr(message_header_size);
                                if(!conn->process_next_message(*this, messageLength)) {
                                    conn->close();
                                    return;
                                }
                            } else { //当前缓存区没有完整的数据
                                auto outstandingMessageBytes = totalMessageLength - bytesInBuffer;
                                auto availableBufferBytes = conn->_messageBuffer.bytes_to_write();
                                if(availableBufferBytes < outstandingMessageBytes)
                                    conn->_messageBuffer.add_space(outstandingMessageBytes - availableBufferBytes);
                                conn->_outStandingReadBytes.emplace(outstandingMessageBytes);
                                break;
                            }
                        }
                        start_read_message(conn);
                    } else {
                        cerr << "error in read, " << ec.message() << "," << __func__ << "," << __LINE__ << endl;
                        conn->close();
                        return;
                    }
                } catch (...) {
                    cerr << "Catch exception." << __func__ << "," << __LINE__ << endl;
                    conn->close();
                    return;
                }
            });
}


int main( int argc, char** argv ) {
    try {
        appbase::app().register_plugin<net_plugin>();
        appbase::app().register_plugin<chain_plugin>();
        if( !appbase::app().initialize<net_plugin, chain_plugin>( argc, argv ) )
            return -1;
        appbase::app().startup();
        appbase::app().exec();
    } catch ( const boost::exception& e ) {
        std::cerr << boost::diagnostic_information(e) << "\n";
    } catch ( const std::exception& e ) {
        std::cerr << e.what() << "\n";
    } catch ( ... ) {
        std::cerr << "unknown exception\n";
    }
    std::cout << "exited cleanly\n";
    return 0;
}
