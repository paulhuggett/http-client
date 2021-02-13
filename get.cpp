// Standard library
#include <cassert>
#include <cstring>
#include <iostream>

// Platform
#include <unistd.h>

// Network
#include <netdb.h>
#include <sys/socket.h>

// pstore
#include "pstore/http/buffered_reader.hpp"
#include "pstore/http/net_txrx.hpp"
#include "pstore/http/request.hpp"

// client
#include "client/client.hpp"

using namespace pstore;

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
    error_or<socket_descriptor> eo_socket = http::get_host_info (host, port) >>=
        http::establish_connection;
    if (!eo_socket) {
        std::cerr << "Failed to connect to: " << host << ':' << port << ' ' << path << " ("
                  << eo_socket.get_error ().message () << ")\n";
        return EXIT_FAILURE;
    }
    socket_descriptor & clientfd = *eo_socket;

    // Send an HTTP GET request.
    std::error_code const erc = http::http_get (clientfd, host, port, path);
    if (erc) {
        std::cerr << "Failed to send: " << erc.message () << ")\n";
        return EXIT_FAILURE;
    }

    // Get the server's reply.
    auto reader = pstore::http::make_buffered_reader<pstore::socket_descriptor &> (
        pstore::http::net::refiller);
    pstore::error_or_n<pstore::socket_descriptor &, pstore::http::request_info> eri =
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
            return http::read_reply (reader, io2, header_contents, http::content_length (headers));
        };

    return EXIT_SUCCESS;
}
