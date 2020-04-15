#include <seastar/core/app-template.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/future.hh>
#include <seastar/net/api.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/distributed.hh>
#include <boost/asio.hpp>
#include <redisclient/redissyncclient.h>

#include <iostream>

using namespace seastar;
using namespace net;

using namespace redisclient;

class rediscli
{
public:
    bool init(const std::string &ip = "127.0.0.1", const uint16_t &port = 6379, const std::string &passwd = "")
    {
        if (_init)
            return _init;
        _ip = ip;
        _port = port;
        _passwd = passwd;
        _init = _connect();
        return _init;
    }
    bool command(const std::string &cmd, const std::deque<RedisBuffer> &args, RedisValue &result)
    {
        bool ret = false;
        int try_times = 0;
        do
        {
            try
            {
                result = _cli->command(cmd, args);
                ret = result.isOk();
                if (!ret)
                {
                    std::cerr << "failed to excute command. error = " << result.toString().c_str() << "\n";
                }
                break;
            }
            catch (std::exception &e)
            {
                std::cerr << "exception on excute redis command " << e.what() << "\n";
            }
            if (!_cli->isConnected())
                _connect();

            try_times++;
        } while (try_times < 3);

        return ret;
    }

private:
    bool _connect()
    {
        _cli.reset(new RedisSyncClient(_io_service));

        bool ret = false;
        try
        {
            boost::asio::ip::address addr = boost::asio::ip::address::from_string(_ip);
            boost::asio::ip::tcp::endpoint endpoint(addr, _port);
            boost::system::error_code ec;
            _cli->connect(endpoint, ec);
            if (ec)
            {
                std::cerr << "Can't connect to redis: " << ec.message() << std::endl;
                return ret;
            }
            if (!_passwd.empty())
            {
                RedisValue result = _cli->command("AUTH", {_passwd});
                ret = result.isOk();
            }
            else
            {
                ret = true;
            }
        }
        catch (boost::system::error_code &e)
        {
            std::cerr << "excepion = " << e.message().c_str() << "\n";
        }
        catch (...)
        {
            std::cerr << "excepion" << std::endl;
        }
        return ret;
    }

private:
    std::string _ip;
    uint16_t _port;
    std::string _passwd;
    bool _init = false;
    boost::asio::io_service _io_service;
    std::shared_ptr<RedisSyncClient> _cli;
};

class tcp_server;

class protocol
{
public:
    
    future<> handle(/*tcp_server *server,*/ input_stream<char> &in, output_stream<char> &out)
    {
        // if(!server)
        //     return make_ready_future<>();

        // lw_shared_ptr<rediscli> cli(server->cli());

        return in.read().then([&out/*, &cli*/](auto buf) {
                            // std::string str(buf.begin(), buf.end());
                            // RedisValue val;
                            // cli->command("set", {str, "1"}, val);
                            return out.write(std::move(buf));
                        })
            .then_wrapped([this, &out](auto &&f) -> future<> {
                try
                {
                    f.get();
                }
                catch (std::bad_alloc &e)
                {
                    return out.write("msg_out_of_memory");
                }
                return make_ready_future<>();
            });
    }
};

class tcp_server
{
public:
    struct connection
    {
        connected_socket _socket;
        socket_address _addr;
        input_stream<char> _in;
        output_stream<char> _out;
        protocol _proto;

    public:
        connection(connected_socket &&socket, socket_address addr)
            : _socket(std::move(socket)), _in(_socket.input()), _out(_socket.output())
        {
        }
    };
    tcp_server(ipv4_addr addr = ipv4_addr("127.0.0.1", 1325)) : _addr(addr)
    {
    }
    void start()
    {
        _cli = make_lw_shared<rediscli>();
        _cli->init();

        listen_options lo;
        lo.reuse_address = true;
        _listener = seastar::api_v2::server_socket(seastar::listen(make_ipv4_address(_addr), lo));
        _task = keep_doing([this] {
            return _listener->accept().then([this](accept_result ar) mutable {
                connected_socket socket = std::move(ar.connection);
                socket_address addr = std::move(ar.remote_address);
                std::cout << "accept " << addr << "\n";
                auto conn = make_lw_shared<connection>(std::move(socket), addr);
                (void)do_until([conn] { return conn->_in.eof(); }, [conn] { return conn->_proto.handle(conn->_in, conn->_out).then([conn] {
                                                                                return conn->_out.flush();
                                                                            }); })
                    .finally([conn] {
                        std::cout << "close " << conn->_addr << "\n";
                        return conn->_out.close().finally([conn] {});
                    });
            });
        });
    }
    future<> stop()
    {
        _listener->abort_accept();
        return _task->handle_exception([](std::exception_ptr e) {
            std::cerr << "exception in tcp_server " << e << '\n';
        });
    }
    bool storage(const std::string &key){
        RedisValue val;
        _cli->command("set", {key, "1"}, val);
        return true;
    }
    lw_shared_ptr<rediscli> cli(){
        return _cli;
    }
private:
    ipv4_addr _addr;
    compat::optional<future<>> _task;
    lw_shared_ptr<seastar::api_v2::server_socket> _listener;
    lw_shared_ptr<rediscli> _cli;
};

namespace bpo = boost::program_options;

int main(int argc, char **argv)
{
    app_template app;
    app.add_options()("ip", bpo::value<std::string>()->default_value("127.0.0.1"), "bind server ip")("port", bpo::value<uint16_t>()->default_value(1325), "server port");

    return app.run_deprecated(argc, argv, [&]() {
        auto &&config = app.configuration();
        uint16_t port = config["port"].as<uint16_t>();
        std::string ip = config["ip"].as<std::string>();
        auto server = new distributed<tcp_server>;
        (void)server->start(ipv4_addr(ip, port)).then([server = std::move(server), port]() mutable {
                                                    engine().at_exit([server] {
                                                        return server->stop();
                                                    });
                                                    // Start listening in the background.
                                                    (void)server->invoke_on_all(&tcp_server::start);
                                                })
            .then([port] {
                std::cout << "Seastar TCP server listening on port " << port << " ...\n";
            });
    });
    // rediscli cli;
    // cli.init();
    // RedisValue val;
    // for (int i = 0; i < 100000; i++)
    //     cli.command("set", {std::to_string(i), std::to_string(i)}, val);
}