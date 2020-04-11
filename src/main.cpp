#include <seastar/core/app-template.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/future.hh>
#include <seastar/net/api.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/distributed.hh>

#include <iostream>

using namespace seastar;
using namespace net;

class protocol
{

public:
    future<> handle(input_stream<char> &in, output_stream<char> &out)
    {
        return in.read().then([&out](auto buf) {
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
        listen_options lo;
        lo.reuse_address = true;
        _listener = seastar::api_v2::server_socket(seastar::listen(make_ipv4_address(_addr), lo));
        // Run in the background until eof has reached on the input connection.
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

private:
    ipv4_addr _addr;
    compat::optional<future<>> _task;
    lw_shared_ptr<seastar::api_v2::server_socket> _listener;
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
}