#ifndef __ASYNC_CLIENT_H
#define __ASYNC_CLIENT_H

#include <iostream>
#include <string>
#include <thread>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace asio = boost::asio;
namespace http = boost::beast::http;

class AsyncClient :
    public std::enable_shared_from_this<AsyncClient> {

public:
    explicit AsyncClient(
        const std::string& host,
        const std::string& port,
        const std::string& target,
        const std::string& version
    ) : _host(host), _port(port), _target(target) {
        if (version == "1.0") {
            _version = 10;
        } else {
            _version = 11; // default version is 1.1
        }

    }

bool start() {
    try {

    } catch (std::exception& e) {
        std::cerr << "Error      [BINARY]: Could not start client. Reason:\n" << e.what() << "\n";
        stop();
        return false;
    }
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
    http::request<http::empty_body> _request;
    http::response<http::string_body> _response;


    std::string _host;
    std::string _port;
    std::string _target;
    int _version;

};

#endif // __ASYNC_CLIENT_H