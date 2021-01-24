// Standard library
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
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

using namespace pstore;

namespace {

    class gai_error_category : public std::error_category {
    public:
        char const * name () const noexcept override {
            return "gai error category";
        }
        std::string message (int error) const override {
            return gai_strerror (error);
        }
    };

    std::error_category const & get_gai_error_category () noexcept {
        static gai_error_category cat;
        return cat;
    }

    std::error_code make_gai_error_code (int const e) noexcept {
        return {e, get_gai_error_category ()};
    }


    // Get host information.
    error_or<addrinfo *> get_host_info (char const * host, char const * port) {
        using return_type = error_or<addrinfo *>;

        addrinfo hints;
        std::memset (&hints, 0, sizeof (hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo * res = nullptr;
        if (int const r = ::getaddrinfo (host, port, &hints, &res)) {
            return return_type {make_gai_error_code (r)};
        }
        return return_type{res};
    }


    // Establish connection with the host
    error_or<socket_descriptor> establish_connection (addrinfo * info) {
        using return_type = error_or<socket_descriptor>;
        assert (info != nullptr);
        int error = 0;

        auto const free = [] (addrinfo * p) { ::freeaddrinfo (p); };
        std::unique_ptr<addrinfo, decltype (free)> info_ptr{info, free};
        for (; info != nullptr; info = info->ai_next) {
            socket_descriptor clientfd{
                ::socket (info->ai_family, info->ai_socktype, info->ai_protocol)};
            if (!clientfd.valid ()) {
                error = errno;
                continue;
            }

            if (::connect (clientfd.native_handle (), info->ai_addr, info->ai_addrlen) < 0) {
                error = errno;
                continue;
            }
            return return_type{std::move (clientfd)};
        }

        return return_type{std::error_code {error, std::generic_category()}};
    }

    // Send GET request
    std::error_code http_get (socket_descriptor const & clientfd, char const * host, char const * port,
                   char const * path) {
        static constexpr auto crlf = "\r\n";
        std::ostringstream os;
        os << "GET " << path << " HTTP/1.0" << crlf   //
           << "Host: " << host << ':' << port << crlf //
           << crlf;
        auto const & str = os.str ();
        if (::send (clientfd.native_handle (), str.data (), str.length (), 0) < 0) {
            return {errno, std::generic_category ()};
        }
        return {};
    }


    template <typename BufferedReader>
    error_or<socket_descriptor> read_reply (BufferedReader & reader, socket_descriptor & io2,
                                            http::header_info const & /*header_contents*/, long content_length) {
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

} // end anonymous namespace

int main (int argc, char ** argv) {
    if (argc != 4) {
        std::cerr << "USAGE: " << argv[0] << " <hostname> <port> <request path>\n";
        return EXIT_FAILURE;
    }

    std::unordered_map<std::string, std::string> jar; // cookie jar.

    // Establish connection with <hostname>:<port>
    auto const * host = argv[1];
    auto const * port = argv[2];
    auto const * path = argv[3];
    error_or<socket_descriptor> eo_socket = get_host_info (host, port) >>= establish_connection;
    if (!eo_socket) {
        std::cerr << "Failed to connect to: " << host << ':' << port << ' ' << path << " (" << eo_socket.get_error().message() << ")\n";
        return EXIT_FAILURE;
    }
    socket_descriptor & clientfd = *eo_socket;

    // Send an HTTP GET request.
    std::error_code const erc = http_get (clientfd, host, port, path);
    if (erc) {
        std::cerr << "Failed to send: " << erc.message() << ")\n";
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
              << request.uri () << '\n';

    // Scan the HTTP headers and dump the server's response.
    auto content_length = 0L;
    PSTORE_ASSERT (clientfd.valid ());
    error_or<socket_descriptor> const err = read_headers (
        reader, std::ref (clientfd),
        [&] (http::header_info io, std::string const & key, std::string const & value) {
            std::cout << "header: " << key << '=' << value << '\n';
            if (key == "content-length") {
                // TODO: more robust str->int.
                content_length = std::max (std::stol (value), 0L);
            }
            return io.handler (key, value);
        },
        http::header_info ()) >>=
        [&] (socket_descriptor & io2, http::header_info const & header_contents) {
            return read_reply (reader, io2, header_contents, content_length);
        };

    return EXIT_SUCCESS;
}
