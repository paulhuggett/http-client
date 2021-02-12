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

namespace pstore {
    namespace http {

        class status_line {
        public:
            status_line (std::string && v, std::string && sc, std::string && rp) noexcept
                    : http_version_{std::move (v)}
                    , status_code_{std::move (sc)}
                    , reason_phrase_{std::move (rp)} {}
            status_line (status_line const &) = default;
            status_line (status_line &&) noexcept = default;

            ~status_line () noexcept = default;

            status_line & operator= (status_line const &) = delete;
            status_line & operator= (status_line &&) noexcept = delete;

            std::string const & http_version () const { return http_version_; }
            std::string const & status_code () const noexcept { return status_code_; }
            std::string const & reason_phrase () const noexcept { return reason_phrase_; }

        private:
            std::string http_version_;
            std::string status_code_;
            std::string reason_phrase_;
        };

        // read status line
        // ~~~~~~~~~~~~~~~~
        /// \tparam Reader  The buffered_reader<> type from which data is to be read.
        /// \param reader  An instance of Reader: a buffered_reader<> from which data is read,
        /// \param io  The state passed to the reader's refill function.
        /// \returns  Type error_or_n<Reader::state_type, status_line>. Either an error or the
        /// updated reader state value and an instance of status_line containing the HTTP version,
        /// status code and reason phrase.
        template <typename Reader>
        error_or_n<typename Reader::state_type, status_line>
        read_status_line (Reader & reader, typename Reader::state_type io) {
            using state_type = typename Reader::state_type;

            auto check_for_eof = [] (state_type io2, maybe<std::string> const & buf) {
                using result_type = error_or_n<state_type, std::string>;
                if (!buf) {
                    return result_type{details::out_of_data_error ()};
                }
                return result_type{in_place, io2, *buf};
            };

            auto extract_status_line = [] (state_type io3, std::string const & s) {
                using result_type = error_or_n<state_type, status_line>;

                std::istringstream is{s};
                std::string http_version;
                std::string status_code;
                std::string reason_phrase;

                is >> http_version >> status_code >> reason_phrase;
                if (http_version.length () == 0 || status_code.length () == 0 ||
                    reason_phrase.length () == 0) {
                    return result_type{details::out_of_data_error ()};
                }
                return result_type{in_place, io3,
                                   status_line{std::move (http_version), std::move (status_code),
                                               std::move (reason_phrase)}};
            };

            return (reader.gets (io) >>= check_for_eof) >>= extract_status_line;
        }

    } // end namespace http
} // end namespace pstore


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
    error_or<socket_descriptor> eo_socket = client::get_host_info (host, port) >>=
        client::establish_connection;
    if (!eo_socket) {
        std::cerr << "Failed to connect to: " << host << ':' << port << ' ' << path << " ("
                  << eo_socket.get_error ().message () << ")\n";
        return EXIT_FAILURE;
    }
    socket_descriptor & clientfd = *eo_socket;

    // Send an HTTP GET request.

    std::string const ws_key = client::request_key ();
    std::error_code const erc = client::http_ws_get (clientfd, host, port, path, ws_key);
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
    auto const & request = std::get<http::status_line> (*eri);
    std::cout << "http-version: " << request.http_version () << '\n'
              << "status-code: " << request.status_code () << '\n'
              << "reason-phrase: " << request.reason_phrase () << '\n';

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
        http::header_info ()) >>= [&] (socket_descriptor & io2,
                                       http::header_info const & header_contents) {
        return client::read_reply (reader, io2, header_contents, client::content_length (headers));
    };

    return EXIT_SUCCESS;
}
