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

public:

    explicit AsyncClient(
        const std::string& host,
        const std::string& loginTarget,
        const std::string& dataTarget,
        const std::string& user,
        const std::string& passwd,
        const std::string& version,
        bool loginStepRequired = false
    ) : 
        _resolver(_context),
        _socket(_context),
        _host(host),
        _loginTarget(loginTarget),
        _dataTarget(dataTarget),
        _user(user),
        _passwd(passwd),
        _loginStepRequired(loginStepRequired) {

        if (_loginStepRequired) {
            _state = State::LOGIN;
            if (_loginTarget.empty()) {
                std::cerr << "Error      [BINARY]: Login required but no login target was provided..\n";
                exit(1);
            }
        } else {
            _state = State::READ_DATA;
        }

        if (version == "1.0") {
            _version = 10;
        }



        try {
        // resolve demain asynchronously
        // std::cout << "AFTER RESLOVING\n";
        std::cout << "started thread!\n";
        // run io context
        _thContext =
            std::thread(
                [this] () { _context.run(); }
            );
        
        resolveDomain();
        } catch (std::exception& e) {
            std::cerr << "Error      [BINARY]: Could not start client. Reason:\n" << e.what() << "\n";
            stop();
            // return false;
        }
    }

    ~AsyncClient() {
        std::cout << "Stopping client...\n";
        stop();
    }

    void startCommunication() {
        resolveDomain();
    }

    void resolveDomain() {
        _resolver.async_resolve(
        _host.c_str(),
        _port.c_str(),
        [this] (const boost::system::error_code& ec, asio::ip::tcp::resolver::results_type results) {
            if (!ec) {
                    std::cout << "Successfully resolved " << _host << ":" << _port << "\n";
                if (VERBOSITY > 1) {
                    std::cout << "Successfully resolved " << _host << ":" << _port << "\n";
                }
                // stop();
                // exit(1);
                // configureRequests();
                connect(results);
            } else {
                std::cerr << "Error      [BINARY]: Domain resolve error: " << ec.message() << "\n";
                stop();
                exit(1);
            }
        }
        );
    }

    void connect(asio::ip::tcp::resolver::results_type& results) {
        return asio::async_connect(
            _socket,
            results.begin(),
            results.end(),
            [this] (const boost::system::error_code& ec, asio::ip::tcp::resolver::iterator it) {
                if (!ec) {
                    if (VERBOSITY > 1) {
                        std::cout << "Successfully connected to host " << _host << ":" << _port << "\n";
                    }
                    write();
                } else {
                    std::cerr << "Error      [BINARY]: Unable to connecto to host: " << ec.message() << "\n";
                    stop();
                    exit(1);
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
                    std::cerr << "Error      [BINARY]: Unable to send request: " << ec.message() << "\n";
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
                        std::cout << "thread" << std::this_thread::get_id() <<  "RESULT " << _response.result_int() << "\n";
                        std::cout << "<\n" << _response.body() << "-----------\n";
                    }

                    // we expect a server cookie that we should store for later requests
                    if (_state == State::LOGIN) {
                        // returns a boost::string_view
                        auto&& x = _response[http::field::set_cookie];
                        _sessionCookie = x.data();
                        // _state = State::READ_DATA;
                    } else if (_state == State::READ_DATA) {
                        // we read the data instead
                        _response.body();
                    }

                    boost::system::error_code sec; // socket error code
                    _socket.shutdown(asio::ip::tcp::socket::shutdown_both, sec);
                    if (sec && (sec != boost::system::errc::not_connected)) {
                        std::cerr << "Error      [BINARY]: Unable to shutdown connection: " << sec.message() << "\n";
                        stop();
                        exit(1);
                    }

                    // stop();
                    // exit(1);

                    try {
                        if (_state == State::LOGIN) {
                            _state = State::READ_DATA;
                            // resolveDomain();
                            std::cout << "reached here\n";
                        }
                    } catch (std::exception& e) {
                        std::cerr << "Error      [BINARY]: Could not reslove domain. Reason:\n" << e.what() << "\n";
                        stop();
                    }
                }
            }
        );
    }

    bool configureRequests() {
        // start with a get request
        // _request.version(_version);
        // _request.method(http::verb::get);
        // _request.target(_target);
        // _request.set(http::field::host, _host);
        // std::string auth64 = _user + ":" + _passwd;
        // _request.set(http::field::authorization, "Basic " + Base64Encoder::base64_encode((const uint8_t*)auth64.data(), auth64.length()));
        // _request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        if (_state == State::LOGIN) {
            // No cookie for first login: We are going to receive the cookie on login from the server
            configureRequest(_loginTarget, false);
        } else if (_state == State::READ_DATA) {
            if (_loginStepRequired) {
                // authenticate with cookie to get data
                configureRequest(_dataTarget, true);
            } else {
                // no login target: this is a REST API communication no need to authenticate then store cookie
                // so we use the data target directly
                configureRequest(_dataTarget, false);
            }
        } else {
            std::cerr << "Error      [BINARY]: Unknown state\n";
            stop();
            exit(1);
        }

        try {
            // resolve demain asynchronously
            // std::cout << "resolving domain..\n";
            
            // resolveDomain();
            // std::cout << "resolving domain..\n";

        } catch (std::exception& e) {
            std::cerr << "Error      [BINARY]: Could not resolve domain. Reason:\n" << e.what() << "\n";
            stop();
            return false;
        }
        return true;
    }

    void stop() {
        // if (_socket.is_open()) {
        //  boost::system::error_code ec;
        //  _socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        //  if (ec) {
        //      std::cout << "Could not delete client, reason: " << ec.message() << std::endl;
        //  }
        // }
        // attempt to stop the asio context, then join its thread
        _context.stop();
        // maybe the context will be busy, so we have to wait
        // for it to finish using std::thread.join()
        if (_thContext.joinable()) {
            _thContext.join();
        }
    
        std::cout << "Info      [BIN]: Successfully shut down client.\n";
    }

    void configureRequest(const std::string& target, bool useCookie = false) {
        _request.version(_version);
        _request.method(http::verb::get);
        _request.target(_target);
        _request.set(http::field::host, _host);
        if (!_user.empty() && !_passwd.empty()) {
            std::string auth64 = _user + ":" + _passwd;
            _request.set(http::field::authorization, "Basic " + Base64Encoder::base64_encode((const uint8_t*)auth64.data(), auth64.length()));
        }
        _request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        if (useCookie) {
            _request.set(http::field::cookie, _sessionCookie);
        }

        if (VERBOSITY > 1) {
            std::ostringstream os;
            os << _request;
            splitByLine(os.str(), "> ");
        }
        // stop();
        // exit(1);
    }

    void splitByLine(std::string input, const std::string prefix) {
        // size_t start = 0;
        size_t pos = 0;
        while ((pos = input.find("\n")) != std::string::npos) {
            std::string sub = input.substr(0, pos-1);
            if (sub != "\n" && !sub.empty()) 
                std::cout << prefix << sub << "\n";
            input.erase(0, pos + 1);
        }
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
    std::string _loginTarget;
    std::string _dataTarget;
    std::string _target {"/"};
    std::string _user;
    std::string _passwd;
    int _version {11};  // default version is 1.1
    bool _loginStepRequired {false};

};

#endif // __ASYNC_CLIENT_H