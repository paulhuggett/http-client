#ifndef CLIENT_CLIENT_HPP
#define CLIENT_CLIENT_HPP

#include <unordered_map>
#include <sstream>
#include <system_error>

#include <netdb.h>

#include "pstore/adt/error_or.hpp"
#include "pstore/os/descriptor.hpp"
#include "pstore/http/headers.hpp"

namespace client {

    class gai_error_category : public std::error_category {
    public:
        char const * name () const noexcept override;
        std::string message (int error) const override;
    };

    std::error_category const & get_gai_error_category () noexcept;
    std::error_code make_gai_error_code (int const e) noexcept;

    // Get host information.
    pstore::error_or<addrinfo *> get_host_info (std::string const & host, std::string const & port);

    // request key
    // ~~~~~~~~~~~
    // The value of the Sec-WebSocket-Key header field MUST be a
    // nonce consisting of a randomly selected 16-byte value that has
    // been base64-encoded (see Section 4 of [RFC4648]). The nonce
    // MUST be selected randomly for each connection.

    std::string request_key ();

    // Establish connection with the host
    pstore::error_or<pstore::socket_descriptor> establish_connection (addrinfo * info);

    using header_map = std::unordered_map<std::string, std::string>;

    // Send GET request
    std::error_code http_get (pstore::socket_descriptor const & fd, std::string const & path,
                              header_map const & headers);

    std::error_code http_get (pstore::socket_descriptor const & fd, std::string const & host,
                              std::string const & port, std::string const & path);

    // Initiate a WebSocket connection upgrade.
    std::error_code http_ws_get (pstore::socket_descriptor const & fd, std::string const & host,
                                 std::string const & port, std::string const & path,
                                 std::string const & ws_key);

    long content_length (std::unordered_map<std::string, std::string> const & headers);

    template <typename BufferedReader>
    pstore::error_or<pstore::socket_descriptor>
    read_reply (BufferedReader & reader, pstore::socket_descriptor & io2,
                pstore::http::header_info const & /*header_contents*/, long content_length) {
        using return_type = pstore::error_or<pstore::socket_descriptor>;

        std::array<char, 256> buffer;
        while (content_length > 0L) {
            auto len = std::min (content_length, static_cast<long> (buffer.size ()));
            content_length -= len;

            auto get_reply = reader.get_span (io2, pstore::gsl::make_span (buffer.data (), len));
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
        return return_type{pstore::in_place, std::move (io2)};
    }

} // end namespace client
#endif // CLIENT_CLIENT_HPP
