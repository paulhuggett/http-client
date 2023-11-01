#ifndef CLIENT_CLIENT_HPP
#define CLIENT_CLIENT_HPP

#include <unordered_map>
#include <sstream>
#include <system_error>
#include <utility>

#include <netdb.h>

#include "pstore/adt/error_or.hpp"
#include "pstore/os/descriptor.hpp"
#include "pstore/http/headers.hpp"
#include "pstore/http/request.hpp"

#define HTTP_STATUS_CODES                                                                          \
    HTTP_STATUS_CODE (100, continue_code)                                                          \
    HTTP_STATUS_CODE (101, switching_protocols)                                                    \
    HTTP_STATUS_CODE (200, ok)                                                                     \
    HTTP_STATUS_CODE (201, created)                                                                \
    HTTP_STATUS_CODE (202, accepted)                                                               \
    HTTP_STATUS_CODE (203, non_authoritative_information)                                          \
    HTTP_STATUS_CODE (204, no_content)                                                             \
    HTTP_STATUS_CODE (205, reset_content)                                                          \
    HTTP_STATUS_CODE (206, partial_content)                                                        \
    HTTP_STATUS_CODE (300, multiple_choices)                                                       \
    HTTP_STATUS_CODE (301, moved_permanently)                                                      \
    HTTP_STATUS_CODE (302, found)                                                                  \
    HTTP_STATUS_CODE (303, see_other)                                                              \
    HTTP_STATUS_CODE (304, not_modified)                                                           \
    HTTP_STATUS_CODE (305, use_proxy)                                                              \
    HTTP_STATUS_CODE (307, temporary_redirect)                                                     \
    HTTP_STATUS_CODE (400, bad_request)                                                            \
    HTTP_STATUS_CODE (401, unauthorized)                                                           \
    HTTP_STATUS_CODE (402, payment_required)                                                       \
    HTTP_STATUS_CODE (403, forbidden)                                                              \
    HTTP_STATUS_CODE (404, not_found)                                                              \
    HTTP_STATUS_CODE (405, method_not_allowed)                                                     \
    HTTP_STATUS_CODE (406, not_acceptable)                                                         \
    HTTP_STATUS_CODE (407, proxy_authentication_required)                                          \
    HTTP_STATUS_CODE (408, request_time_out)                                                       \
    HTTP_STATUS_CODE (409, conflict)                                                               \
    HTTP_STATUS_CODE (410, gone)                                                                   \
    HTTP_STATUS_CODE (411, length_required)                                                        \
    HTTP_STATUS_CODE (412, precondition_failed)                                                    \
    HTTP_STATUS_CODE (413, request_entity_too_large)                                               \
    HTTP_STATUS_CODE (414, request_uri_too_large)                                                  \
    HTTP_STATUS_CODE (415, unsupported_media_type)                                                 \
    HTTP_STATUS_CODE (416, requested_range_not_satisfiable)                                        \
    HTTP_STATUS_CODE (417, expectation_failed)                                                     \
    HTTP_STATUS_CODE (500, internal_server_error)                                                  \
    HTTP_STATUS_CODE (501, not_implemented)                                                        \
    HTTP_STATUS_CODE (502, bad_gateway)                                                            \
    HTTP_STATUS_CODE (503, service_unavailable)                                                    \
    HTTP_STATUS_CODE (504, gateway_time_out)                                                       \
    HTTP_STATUS_CODE (505, http_version_not_supported)

namespace pstore {
    namespace http {

        class gai_error_category : public std::error_category {
        public:
            char const * name () const noexcept override;
            std::string message (int error) const override;
        };

        std::error_category const & get_gai_error_category () noexcept;
        std::error_code make_gai_error_code (int const e) noexcept;

        // Get host information.
        error_or<addrinfo *> get_host_info (std::string const & host, std::string const & port);

        // request key
        // ~~~~~~~~~~~
        // The value of the Sec-WebSocket-Key header field MUST be a
        // nonce consisting of a randomly selected 16-byte value that has
        // been base64-encoded (see Section 4 of [RFC4648]). The nonce
        // MUST be selected randomly for each connection.

        std::string request_key ();



        // http status code
        // ~~~~~~~~~~~~~~~~
#define HTTP_STATUS_CODE(x, y) y = x,
        enum class http_status_code { HTTP_STATUS_CODES };
#undef HTTP_STATUS_CODE

        // operator<<
        // ~~~~~~~~~~
        std::ostream & operator<< (std::ostream & os, http_status_code sc);

        // str to http status code
        // ~~~~~~~~~~~~~~~~~~~~~~~
        maybe<http_status_code> str_to_http_status_code (std::string const & x);


        //*     _        _             _ _           *
        //*  __| |_ __ _| |_ _  _ ___ | (_)_ _  ___  *
        //* (_-<  _/ _` |  _| || (_-< | | | ' \/ -_) *
        //* /__/\__\__,_|\__|\_,_/__/ |_|_|_||_\___| *
        //*                                          *
        class status_line {
        public:
            status_line (std::string && v, http_status_code const sc, std::string && rp) noexcept
                    : http_version_{std::move (v)}
                    , status_code_{sc}
                    , reason_phrase_{std::move (rp)} {}
            status_line (status_line const &) = default;
            status_line (status_line &&) noexcept = default;

            ~status_line () noexcept = default;

            status_line & operator= (status_line const &) = delete;
            status_line & operator= (status_line &&) noexcept = delete;

            std::string const & http_version () const { return http_version_; }
            http_status_code status_code () const noexcept { return status_code_; }
            std::string const & reason_phrase () const noexcept { return reason_phrase_; }

        private:
            std::string http_version_;
            http_status_code status_code_;
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
                return result_type{std::in_place, io2, *buf};
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
                auto const sc = pstore::http::str_to_http_status_code (status_code);
                if (!sc) {
                    return result_type{std::errc::not_supported}; // FIXME: a proper error code.
                }
                return result_type{
                    std::in_place, io3,
                    status_line{std::move (http_version), *sc, std::move (reason_phrase)}};
            };

            return (reader.gets (io) >>= check_for_eof) >>= extract_status_line;
        }



        // Establish connection with the host
        error_or<socket_descriptor> establish_connection (addrinfo * info);

        using header_map = std::unordered_map<std::string, std::string>;

        // Send GET request
        std::error_code http_get (socket_descriptor const & fd, std::string const & path,
                                  header_map const & headers);

        std::error_code http_get (socket_descriptor const & fd, std::string const & host,
                                  std::string const & port, std::string const & path);

        // Initiate a WebSocket connection upgrade.
        std::error_code http_ws_get (socket_descriptor const & fd, std::string const & host,
                                     std::string const & port, std::string const & path,
                                     std::string const & ws_key);

        long content_length (std::unordered_map<std::string, std::string> const & headers);

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
                std::fwrite (subspan.data (), sizeof (char),
                             static_cast<std::size_t> (subspan.size ()), stdout);
            }
            return return_type{std::in_place, std::move (io2)};
        }

    } // end namespace http
} // end namespace pstore

#endif // CLIENT_CLIENT_HPP
