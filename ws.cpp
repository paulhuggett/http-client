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

int main (int argc, char ** argv) {

    auto const sc = pstore::http::str_to_http_status_code ("404");
    if (!sc) {
        std::cout << "unrecognized status code\n";
    } else {
        std::cout << *sc << '\n';
    }

    if (argc != 4) {
        std::cerr << "USAGE: " << argv[0] << " <hostname> <port> <request path>\n";
        return EXIT_FAILURE;
    }

    std::unordered_map<std::string, std::string> jar; // cookie jar.

    // Establish connection with <hostname>:<port>
    auto const host = std::string{argv[1]};
    auto const port = std::string{argv[2]};
    auto const path = std::string{argv[3]};
    pstore::error_or<pstore::socket_descriptor> eo_socket =
        pstore::http::get_host_info (host, port) >>= pstore::http::establish_connection;
    if (!eo_socket) {
        std::cerr << "Failed to connect to: " << host << ':' << port << ' ' << path << " ("
                  << eo_socket.get_error ().message () << ")\n";
        return EXIT_FAILURE;
    }
    pstore::socket_descriptor & clientfd = *eo_socket;

    // Send an HTTP GET request.

    std::string const ws_key = pstore::http::request_key ();
    std::error_code const erc = pstore::http::http_ws_get (clientfd, host, port, path, ws_key);
    if (erc) {
        std::cerr << "Failed to send: " << erc.message () << ")\n";
        return EXIT_FAILURE;
    }

    // Get the server's reply.
    auto reader = pstore::http::make_buffered_reader<pstore::socket_descriptor &> (
        pstore::http::net::refiller);
    pstore::error_or_n<pstore::socket_descriptor &, pstore::http::status_line> eri =
        read_status_line (reader, std::ref (clientfd));
    if (!eri) {
        std::cerr << "Failed to read: " << eri.get_error ().message () << '\n';
        return EXIT_FAILURE;
    }

    clientfd = std::move (std::get<0> (*eri));
    auto const & request = std::get<pstore::http::status_line> (*eri);
    std::cout << "http-version: " << request.http_version () << '\n'
              << "status-code: " << request.status_code () << '\n'
              << "reason-phrase: " << request.reason_phrase () << '\n';

    // Scan the HTTP headers and dump the server's response.

    PSTORE_ASSERT (clientfd.valid ());
    std::unordered_map<std::string, std::string> headers;
    pstore::error_or<pstore::socket_descriptor> const err = read_headers (
        reader, std::ref (clientfd),
        [&] (pstore::http::header_info io, std::string const & key, std::string const & value) {
            std::cout << "header: " << key << '=' << value << '\n';
            headers[key] = value;
            return io.handler (key, value);
        },
        pstore::http::header_info ()) >>=
        [&] (pstore::socket_descriptor & io2, pstore::http::header_info const & header_contents) {
            return pstore::http::read_reply (reader, io2, header_contents,
                                             pstore::http::content_length (headers));
        };

    return EXIT_SUCCESS;
}
