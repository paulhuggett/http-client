// Standard library
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <unordered_map>

// Platform
#include <unistd.h>

// Network
#include <netdb.h>
#include <sys/socket.h>

// pstore
#include "pstore/http/buffered_reader.hpp"
#include "pstore/http/headers.hpp"
#include "pstore/http/net_txrx.hpp"
#include "pstore/http/request.hpp"
#include "pstore/os/descriptor.hpp"
#include "pstore/support/base64.hpp"

using namespace pstore;

namespace {

    class gai_error_category : public std::error_category {
    public:
        char const * name () const noexcept override { return "gai error category"; }
        std::string message (int error) const override { return gai_strerror (error); }
    };

    std::error_category const & get_gai_error_category () noexcept {
        static gai_error_category cat;
        return cat;
    }

    std::error_code make_gai_error_code (int const e) noexcept {
        return {e, get_gai_error_category ()};
    }


    // Get host information.
    error_or<addrinfo *> get_host_info (std::string const & host, std::string const & port) {
        using return_type = error_or<addrinfo *>;

        addrinfo hints;
        std::memset (&hints, 0, sizeof (hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo * res = nullptr;
        if (int const r = ::getaddrinfo (host.c_str (), port.c_str (), &hints, &res)) {
            return return_type{make_gai_error_code (r)};
        }
        return return_type{res};
    }

    // request key
    // ~~~~~~~~~~~
    // The value of the Sec-WebSocket-Key header field MUST be a
    // nonce consisting of a randomly selected 16-byte value that has
    // been base64-encoded (see Section 4 of [RFC4648]). The nonce
    // MUST be selected randomly for each connection.

    std::string request_key () {
        static std::random_device rd;
        static std::default_random_engine dre{rd ()};
        static std::uniform_int_distribution<int> uid{std::numeric_limits<std::uint8_t>::min (), std::numeric_limits<std::uint8_t>::max ()};

        std::array<std::uint8_t, 16> nonce;
        std::generate (std::begin (nonce), std::end (nonce), [&] () { return uid (dre); });
        std::string result;
        to_base64 (std::begin (nonce), std::end (nonce), std::back_inserter (result));
        return result;
    }

    // Establish connection with the host
    error_or<socket_descriptor> establish_connection (addrinfo * info) {
        using return_type = error_or<socket_descriptor>;
        assert (info != nullptr);
        int error = 0;

        std::unique_ptr<addrinfo, decltype (&freeaddrinfo)> info_ptr{info, &freeaddrinfo};
        for (; info != nullptr; info = info->ai_next) {
            socket_descriptor clientfd{
                ::socket (info->ai_family, info->ai_socktype, info->ai_protocol)};
            if (clientfd.valid () &&
                ::connect (clientfd.native_handle (), info->ai_addr, info->ai_addrlen) == 0) {
                return return_type{std::move (clientfd)};
            }
            error = errno;
        }
        return return_type{make_error_code (errno_erc{error})};
    }

    using header_map = std::unordered_map<std::string, std::string>;

    // Send GET request
    std::error_code http_get (socket_descriptor const & fd, std::string const & path,
                              header_map const & headers) {
        std::string const ws_key = request_key ();
        static constexpr auto crlf = "\r\n";
        std::ostringstream os;
        os << "GET " << path << " HTTP/1.1" << crlf;
        for (auto const & kvp : headers) {
            os << kvp.first << ':' << kvp.second << crlf;
        }
        os << crlf;

        auto const & str = os.str ();
        if (::send (fd.native_handle (), str.data (), str.length (), 0) < 0) {
            return {errno, std::generic_category ()};
        }
        return {};
    }

    std::error_code http_get (socket_descriptor const & fd, std::string const & host,
                              std::string const & port, std::string const & path) {
        header_map headers;
        headers["Host"] = std::string{host} + ":" + std::string{port};
        return http_get (fd, path, headers);
    }

    // Initiate a WebSocket connection upgrade.
    std::error_code http_ws_get (socket_descriptor const & fd, std::string const & host,
                                 std::string const & port, std::string const & path,
                                 std::string const & ws_key) {
        header_map headers;
        headers["Host"] = host + ":" + port;
        headers["Upgrade"] = "websocket";
        headers["Connection"] = "Upgrade";
        headers["Sec-WebSocket-Key"] = ws_key;
        headers["Sec-WebSocket-Version"] = "13";
        return http_get (fd, path, headers);
    }


    template <typename BufferedReader>
    error_or<socket_descriptor> read_reply (BufferedReader & reader, socket_descriptor & io2,
                                            http::header_info const & /*header_contents*/,
                                            long content_length) {
        using return_type = error_or<socket_descriptor>;

        std::array<char, 256> buffer;
        while (content_length > 0L) {
            auto len = std::min (content_length, static_cast<long> (buffer.size ()));
            content_length -= len;

            auto get_reply = reader.get_span (io2, gsl::make_span (buffer.data (), len));
            if (!get_reply) {
                return return_type{get_reply.get_error ()};
            }
            io2 = std::move (std::get<0> (*get_reply));
            auto const & subspan = std::get<1> (*get_reply);
            if (subspan.size () == 0) {
                break;
            }
            std::fwrite (subspan.data (), sizeof (char), static_cast<std::size_t> (subspan.size ()),
                         stdout);
        }
        return return_type{in_place, std::move (io2)};
    }

    long content_length (std::unordered_map<std::string, std::string> const & headers) {
        auto cl = 0L;
        auto const pos = headers.find ("content-length");
        if (pos != std::end (headers)) {
            // TODO: a more robust string to long.
            cl = std::max (std::stol (pos->second), 0L);
        }
        return cl;
    }

} // end anonymous namespace

int main (int argc, char ** argv) {
    if (argc != 4) {
        std::cerr << "USAGE: " << argv[0] << " <hostname> <port> <request path>\n";
        return EXIT_FAILURE;
    }

    std::unordered_map<std::string, std::string> jar; // cookie jar.

    // Establish connection with <hostname>:<port>
    auto const host = std::string{argv[1]};
    auto const port = std::string{argv[2]};
    auto const path = std::string{argv[3]};
    error_or<socket_descriptor> eo_socket = get_host_info (host, port) >>= establish_connection;
    if (!eo_socket) {
        std::cerr << "Failed to connect to: " << host << ':' << port << ' ' << path << " ("
                  << eo_socket.get_error ().message () << ")\n";
        return EXIT_FAILURE;
    }
    socket_descriptor & clientfd = *eo_socket;

    // Send an HTTP GET request.
    // std::error_code const erc = http_get (clientfd, host, port, path);
#if 0
    std::string const ws_key = request_key ();
    std::error_code const erc = http_ws_get (clientfd, host, port, path, ws_key);
#else
    std::error_code const erc = http_get (clientfd, host, port, path);
#endif
    if (erc) {
        std::cerr << "Failed to send: " << erc.message () << ")\n";
        return EXIT_FAILURE;
    }

    // Get the server's reply.
    auto reader = http::make_buffered_reader<socket_descriptor &> (http::net::refiller);
    error_or_n<socket_descriptor &, http::request_info> eri =
        read_request (reader, std::ref (clientfd));
    if (!eri) {
        std::cerr << "Failed to read: " << eri.get_error ().message () << '\n';
        return EXIT_FAILURE;
    }

    clientfd = std::move (std::get<0> (*eri));
    auto const & request = std::get<http::request_info> (*eri);
    std::cout << "request: " << request.method () << ' ' << request.version () << ' '
              << "error:" << request.uri () << '\n';

    // Scan the HTTP headers and dump the server's response.

    PSTORE_ASSERT (clientfd.valid ());
    std::unordered_map<std::string, std::string> headers;
    error_or<socket_descriptor> const err = read_headers (
        reader, std::ref (clientfd),
        [&] (http::header_info io, std::string const & key, std::string const & value) {
            std::cout << "header: " << key << '=' << value << '\n';
            headers[key] = value;
            return io.handler (key, value);
        },
        http::header_info ()) >>=
        [&] (socket_descriptor & io2, http::header_info const & header_contents) {
            return read_reply (reader, io2, header_contents, content_length (headers));
        };

    return EXIT_SUCCESS;
}
