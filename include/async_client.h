#ifndef __ASYNC_CLIENT_H
#define __ASYNC_CLIENT_H

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <memory>
#include <chrono>
// #include <boost/beast/core/detail/config.hpp>
// #include <boost/beast/core/basic_stream.hpp>
// #include <boost/beast/core/rate_policy.hpp>
// #include <boost/asio/executor.hpp>
// #include <boost/asio/ip/tcp.hpp>
// #include <boost/asio.hpp>
// #include <boost/beast.hpp>

#define VERBOSITY 2

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

class AsyncClient :
    public std::enable_shared_from_this<AsyncClient> {

public:
    explicit AsyncClient(
        const std::string& host,
        const std::string& port,
        const std::string& target,
        const std::string& version
    ) : 
        _resolver(_context),
        _socket(_context),
        _host(host),
        _port(port),
        _target(target) {
            
        
        std::cout << "Called constructor...\n";
        if (version == "1.0") {
            _version = 10;
        } else {
            _version = 11; // default version is 1.1
        }
    }

    ~AsyncClient() { std::cout << "Stopping server...\n";stop(); }

void resolveDomain() {
    _resolver.async_resolve(
    _host.c_str(),
    _port.c_str(),
    [this] (const boost::system::error_code& ec, asio::ip::tcp::resolver::results_type results) {
        if (!ec) {
            std::cout << "Successfully resolved " << _host << ":" << _port << "\n";
            connect(results);

        } else {
            std::cerr << "Error      [BINARY]: Domain resolve error: " << ec.message() << "\n";
        }
    }
    );
}

void connect(asio::ip::tcp::resolver::results_type& results) {
    std::cout << "Results\n";
    asio::async_connect(
        _socket,
        results.begin(),
        results.end(),
        [this] (const boost::system::error_code& ec, asio::ip::tcp::resolver::iterator it) {
            if (!ec) {
                std::cout << "Successfully connected to host " << _host << ":" << _port << "\n";
                write();
            } else {
                std::cerr << "Error      [BINARY]: Unable to connecto to host: " << ec.message() << "\n";
            }
        }
    );
}

void write() {
    http::async_write(
        _socket,
        _request,
        [this] (const boost::system::error_code& ec, std::size_t bytes) {
            boost::ignore_unused(bytes);
            if (!ec) {
                if (VERBOSITY > 1) {
                    std::cout << ">\n" << _request << "-----------\n";
                }
                read();
            } else {
                std::cerr << "Error      [BINARY]: Unable send request: " << ec.message() << "\n";
            }

        }
    );
}

void read() {
    http::async_read(
        _socket,
        _buffer,
        _response,
        [this] (const boost::system::error_code& ec, std::size_t bytes) {
            boost::ignore_unused(bytes);
            if (!ec) {
                if (VERBOSITY > 1) {
                    std::cout << "RESULT " << _response.result_int() << "\n";
                    std::cout << "<\n" << _response.body() << "-----------\n";

                }
                
                // returns a boost::string_view
                auto x = _response[http::field::set_cookie];
                _sessionCookie = x.data();

                boost::system::error_code sec; // socket error code
                _socket.shutdown(asio::ip::tcp::socket::shutdown_both, sec);
                if (sec && (sec != boost::system::errc::not_connected)) {
                    std::cerr << "Error      [BINARY]: Unable to shutdown connection: " << sec.message() << "\n";
                }
            }
        }
    );
}

bool start() {
    // start with a get request
    _request.version(_version);
    _request.method(http::verb::get);
    _request.target(_target);
    _request.set(http::field::host, _host);
    _request.set(http::field::authorization, "Basic Z3Vlc3Q6dnBndWVzdA==");
    _request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    try {
        // resolve demain asynchronously
        resolveDomain();

        // run io context
        _thContext =
            std::thread(
                [this] () { _context.run(); }
            );
        
    } catch (std::exception& e) {
        std::cerr << "Error      [BINARY]: Could not start client. Reason:\n" << e.what() << "\n";
        stop();
        return false;
    }
    return true;
}

void stop() {
    // attempt to stop the asio context, then join its thread
    _context.stop();
    // maybe the context will be busy, so we have to wait
    // for it to finish using std::thread.join()
    if (_thContext.joinable()) {
        _thContext.join();
    }
    
    std::cout << "Info      [BIN]: Successfully shut down client.\n";
}

private:

    asio::io_context _context;
    std::thread _thContext; // thread used by io context
    asio::ip::tcp::resolver _resolver; // domain lookup
    asio::ip::tcp::socket _socket;
    http::request<http::empty_body> _request;
    http::response<http::string_body> _response;
    beast::flat_buffer _buffer; // data buffer
    std::string _sessionCookie;
    // beast::AsyncWriteStream _strm;

    std::string _host;
    std::string _port;
    std::string _target;
    int _version;

};

#endif // __ASYNC_CLIENT_H