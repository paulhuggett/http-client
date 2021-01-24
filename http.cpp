#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

#include <unistd.h>

// Network
#include <netdb.h>
#include <sys/socket.h>

//#include "descriptor.hpp"

#include "pstore/os/descriptor.hpp"
#include "pstore/http/buffered_reader.hpp"
#include "pstore/http/net_txrx.hpp"
#include "pstore/http/request.hpp"
#include "pstore/http/headers.hpp"

using namespace pstore;

namespace {

    // Get host information.
    struct addrinfo * get_host_info (char const * host, char const * port) {
        addrinfo hints;
        std::memset (&hints, 0, sizeof (hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo * getaddrinfo_res = nullptr;
        if (int const r = getaddrinfo (host, port, &hints, &getaddrinfo_res)) {
            std::cerr << "get_host_info: " << gai_strerror (r) << '\n';
            return nullptr;
        }
        return getaddrinfo_res;
    }


    // Establish connection with the host
    socket_descriptor establish_connection (struct addrinfo * info) {
        if (info == nullptr) {
            return socket_descriptor{};
        }

        auto const free = [] (addrinfo * p) { ::freeaddrinfo (p); };
        std::unique_ptr<addrinfo, decltype (free)> info_ptr{info, free};
        for (; info != nullptr; info = info->ai_next) {
            socket_descriptor clientfd{
                ::socket (info->ai_family, info->ai_socktype, info->ai_protocol)};
            if (!clientfd.valid ()) {
                perror ("socket");
                continue;
            }

            if (::connect (clientfd.native_handle (), info->ai_addr, info->ai_addrlen) < 0) {
                perror ("connect");
                continue;
            }
            return clientfd;
        }

        return socket_descriptor{};
    }

    // Send GET request
    void http_get (socket_descriptor const & clientfd, char const * host, char const * port,
                   char const * path) {
        constexpr auto crlf = "\r\n";
        std::ostringstream os;
        os << "GET " << path << " HTTP/1.0" << crlf << "Host: " << host << ':' << port << crlf
           << crlf;
        auto const & str = os.str ();
        ::send (clientfd.native_handle (), str.data (), str.length (), 0);
    }

    template <typename BufferedReader>
    error_or<socket_descriptor> read_reply (BufferedReader & reader, socket_descriptor & io2,
                                            http::header_info const & /*header_contents*/) {
        std::array<char, 256> buffer;
        for (;;) {
            auto get_reply = reader.get_span (io2, gsl::make_span (buffer));
            if (!get_reply) {
                return error_or<socket_descriptor> (get_reply.get_error ());
            }
            io2 = std::move (std::get<0> (*get_reply));
            auto const & subspan = std::get<1> (*get_reply);
            if (subspan.size () == 0) {
                return error_or<socket_descriptor> (in_place, std::move (io2));
            }
            std::fwrite (subspan.data (), sizeof (char), static_cast<std::size_t> (subspan.size ()),
                         stdout);
        }
    }

} // end anonymous namespace

int main (int argc, char ** argv) {
    if (argc != 4) {
        std::cerr << "USAGE: " << argv[0] << " <hostname> <port> <request path>\n";
        return EXIT_FAILURE;
    }

    // Establish connection with <hostname>:<port>
    auto * host = argv[1];
    auto * port = argv[2];
    auto * path = argv[3];
    socket_descriptor clientfd{establish_connection (get_host_info (host, port))};
    if (!clientfd.valid ()) {
        std::cerr << "Failed to connect to: " << host << ':' << port << ' ' << path << '\n';
        return EXIT_FAILURE;
    }

    // Send an HTTP GET request.
    http_get (clientfd, host, port, path);


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
    PSTORE_ASSERT (clientfd.valid ());
    error_or<socket_descriptor> const err = read_headers (
        reader, std::ref (clientfd),
        [] (http::header_info io, std::string const & key, std::string const & value) {
            std::cout << "header: " << key << '=' << value << '\n';
            return io.handler (key, value);
        },
        http::header_info ()) >>=
        [&] (socket_descriptor & io2, http::header_info const & header_contents) {
            return read_reply (reader, io2, header_contents);
        };

    return EXIT_SUCCESS;
}
