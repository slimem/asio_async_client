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
#include <future>
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

class Base64Encoder {
public:
    static std::string base64_encode(const uint8_t* buf, unsigned int bufLen) {
        const char base64_chars[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string ret;
        int i = 0;
        int j = 0;
        uint8_t char_array_3[3];
        uint8_t char_array_4[4];

        while (bufLen--) {
            char_array_3[i++] = *(buf++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for(i = 0; (i <4) ; i++) {
                    ret += base64_chars[char_array_4[i]];
                }
                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 3; ++j) {
                char_array_3[j] = '\0';
            }

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (j = 0; (j < i + 1); j++) {
                ret += base64_chars[char_array_4[j]];
            }
            while((i++ < 3)) {
                ret += '=';
            }
        }
        return ret;
    }
};

class AsyncClient :
    public std::enable_shared_from_this<AsyncClient> {

    enum class State {
        LOGIN,
        READ_DATA,
        UNKNOWN,
    };

    void resolveDomain() {
        _resolver.async_resolve(
        _host.c_str(),
        _port.c_str(),
        [this] (const boost::system::error_code& ec, asio::ip::tcp::resolver::results_type results) {
            if (!ec) {
                std::cout << "Successfully resolved " << _host << ":" << _port << "\n";
                // connect(results);

            } else {
                std::cerr << "Error      [BINARY]: Domain resolve error: " << ec.message() << "\n";
            }
        }
        );
    }

public:
    explicit AsyncClient(
        const std::string& host,
        const std::string& user,
        const std::string& passwd,
        const std::string& version,
        bool loginRequired = false
    ) : 
        _resolver(_context),
        _socket(_context),
        _host(host),
        _user(user),
        _passwd(passwd) {

        if (loginRequired) {
            _state = State::LOGIN;
        } else {
            _state = State::READ_DATA;
        }

        if (version == "1.0") {
            _version = 10;
        }

        try {
        // resolve demain asynchronously
        // resolveDomain().get();
        // std::cout << "AFTER RESLOVING\n";

        // run io context
        _thContext =
            std::thread(
                [this] () { _context.run(); }
            );
        
        } catch (std::exception& e) {
            std::cerr << "Error      [BINARY]: Could not start client. Reason:\n" << e.what() << "\n";
            stop();
            // return false;
        }
    }

    ~AsyncClient() { std::cout << "Stopping server...\n";stop(); }



    auto connect(asio::ip::tcp::resolver::results_type& results) {
        return asio::async_connect(
            _socket,
            results.begin(),
            results.end(),
            [this] (const boost::system::error_code& ec, asio::ip::tcp::resolver::iterator it) {
                if (!ec) {
                    // std::cout << "Successfully connected to host " << _host << ":" << _port << "\n";
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
                        // std::cout << ">\n" << _request << "-----------\n";
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
        std::string auth64 = _user + ":" + _passwd;
        _request.set(http::field::authorization, "Basic " + Base64Encoder::base64_encode((const uint8_t*)auth64.data(), auth64.length()));
        _request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        try {
            // resolve demain asynchronously
            // resolveDomain();

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

    void prepareRequest() {
        _request.version(_version);
        _request.method(http::verb::get);
        _request.target(_target);
        _request.set(http::field::host, _host);
        std::string auth64 = _user + ":" + _passwd;
        _request.set(http::field::authorization, "Basic " + Base64Encoder::base64_encode((const uint8_t*)auth64.data(), auth64.length()));
        _request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
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
    std::atomic<State> _state {State::UNKNOWN};

    std::string _port {"80"};
    std::string _host;
    std::string _target {"/"};
    std::string _user;
    std::string _passwd;
    int _version {11};  // default version is 1.1

};

#endif // __ASYNC_CLIENT_H