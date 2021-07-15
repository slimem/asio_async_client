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

#define VERBOSITY 1

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
            // run io context
            // _thContext =
                // std::thread(
                    // [this] () { _context.run(); }
                // );
                // _context.run();

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

    const std::string& getData() {
        resolveDomain();
        
        _context.run();
        _context.reset();

        return _data;
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
                    // std::cout << "Successfully resolved " << _host << ":" << _port << "\n";
                if (VERBOSITY > 1) {
                    std::cout << "Successfully resolved " << _host << ":" << _port << "\n";
                }
                // stop();
                // exit(1);
                _result = results;
                // connect(_result);
                connect();
            } else {
                std::cerr << "Error      [BINARY]: Domain resolve error: " << ec.message() << "\n";
                stop();
                exit(1);
            }
        }
        );
    }

    void connect() {

        // configure requests according to state
        configureRequests();

        return asio::async_connect(
            _socket,
            _result.begin(),
            _result.end(),
            [this] (const boost::system::error_code& ec, asio::ip::tcp::resolver::iterator it) {
                if (!ec) {
                    if (VERBOSITY > 1) {
                        // std::cout << "Successfully connected to host " << _host << ":" << _port << "\n";
                    }
                    write();
                } else {
                    std::cerr << "Error      [BINARY]: Unable to connect to to host: " << ec.message() << "\n";
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
                        std::ostringstream os;
                        os << _request;
                        std::cout << "\n";
                        splitByLine(os.str(), ">  ");
                        std::cout << "\n";
                    }

                    read();
                } else {
                    std::cerr << "Error      [BINARY]: Unable to send request: " << ec.message() << "\n";
                }

            }
        );
    }

    void read() {
        // response must be cleared to avoid undefined behaviour
        _response = {};
        http::async_read(
            _socket,
            _buffer,
            _response,
            [this] (const boost::system::error_code& ec, std::size_t bytes) {
                boost::ignore_unused(bytes);
                if (!ec) {
                    if (VERBOSITY > 1) {
                        //std::ostringstream os;
                        //os << _response;
                        //std::cout << "\n";
                        //splitByLine(os.str(), "<  ");
                        //std::cout << "\n";
//
                        //std::ostringstream os2;
                        //os2 << _response.body();
                        //std::cout << "\n";
                        //splitByLine(os2.str(), "<  ");
                        //std::cout << "\n";
                    }

                    
                    // we expect a server cookie that we should store for later requests
                    if (_state == State::LOGIN) {
                       // we expect a 302 response (redirection)
                       if (_response.result_int() != 302) {
                           std::cerr << "Error      [BINARY]: Expected redirection response 302, received " << _response.result_int() << "\n";
                       }

                    //    returns a boost::string_view
                       auto&& x = _response[http::field::set_cookie];
                    //    std::cout << x << std::endl;
                        std::ostringstream os;
                        os << x;
                        _sessionCookie = os.str();

                        // another way to iterate over cookies
#if 0
                        auto&& rang = _response.equal_range(boost::beast::string_view("Set-Cookie"));
                        for (auto&& s = rang.first; s != rang.second; s++) {
                            std::cout << "s->name:" << s->name() << ", s->value:" << s->value() << std::endl << std::endl;
                        }
#endif

                    } else if (_state == State::READ_DATA) {

                        if (_response.result_int() != 200) {
                            std::cerr << "Error      [BINARY]: Expected response 200, received " << _response.result_int() << "\n";
                            
                        } else {
                            _data = _response.body();
                        }
                    }

                    boost::system::error_code sec; // socket error code
                    _socket.shutdown(asio::ip::tcp::socket::shutdown_both, sec);
                    if (sec && (sec != boost::system::errc::not_connected)) {
                        std::cerr << "Error      [BINARY]: Unable to shutdown connection: " << sec.message() << "\n";
                        stop();
                        exit(1);
                    }

                    try {
                        if (_state == State::LOGIN) {
                           _state = State::READ_DATA;
                            // resolveDomain();
                            connect();
                            // write();
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
        if (_socket.is_open()) {
            boost::system::error_code ec;
            _socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            if (ec) {
                std::cout << "Could not delete client, reason: " << ec.message() << std::endl;
            }
        }
        // attempt to stop the asio context, then join its thread
        _context.stop();
        // maybe the context will be busy, so we have to wait
        // for it to finish using std::thread.join()
        // if (_thContext.joinable()) {
            // _thContext.join();
        // }
    
        std::cout << "Info      [BIN]: Successfully shut down client.\n";
    }

    void configureRequest(const std::string& target, bool useCookie = false) {
        _request = {};
        _request.version(_version);
        _request.method(http::verb::get);
        // std::string newtarget = "getChartData?chart=temp";
        _request.target(target);
        _request.set(http::field::host, _host);
        if (useCookie) {
            _request.set(http::field::cookie, _sessionCookie);
        } else if (!_user.empty() && !_passwd.empty()) {
           std::string auth64 = _user + ":" + _passwd;
           _request.set(http::field::authorization, "Basic " + Base64Encoder::base64_encode((const uint8_t*)auth64.data(), auth64.length()));
        }
        _request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        /*if (VERBOSITY > 1) {
            std::ostringstream os;
            os << _request;
            splitByLine(os.str(), "> ");
        }*/
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

    const std::string getState() {
        switch (_state.load(std::memory_order_relaxed)) {
        case State::LOGIN:      return std::string{"State::LOGIN"};
        case State::READ_DATA:  return std::string{"State::READ_DATA"};
        default:                return std::string{"State::UNKNOWN"};
        }
    }

    bool isBusy() {
        return !_context.stopped();
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
    asio::ip::tcp::resolver::results_type _result;

    std::string _port {"80"};
    std::string _host;
    std::string _loginTarget;
    std::string _dataTarget;
    std::string _target {"/"};
    std::string _user;
    std::string _passwd;
    int _version {11};  // default version is 1.1
    bool _loginStepRequired {false};

    // this string
    std::string _data;

};

#endif // __ASYNC_CLIENT_H