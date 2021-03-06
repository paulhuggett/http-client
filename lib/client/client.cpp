#include "client/client.hpp"

#include <cassert>
#include <cstring>
#include <random>

#include "pstore/support/base64.hpp"
#include "pstore/support/error.hpp"

namespace pstore {
    namespace http {

        char const * gai_error_category::name () const noexcept { return "gai error category"; }
        std::string gai_error_category::message (int error) const { return gai_strerror (error); }

        std::error_category const & get_gai_error_category () noexcept {
            static gai_error_category cat;
            return cat;
        }

        std::error_code make_gai_error_code (int const e) noexcept {
            return {e, get_gai_error_category ()};
        }



        // operator<<
        // ~~~~~~~~~~
#define HTTP_STATUS_CODE(sc, name)                                                                 \
    case http_status_code::name: result = #name; break;
        std::ostream & operator<< (std::ostream & os, http_status_code sc) {
            auto result = "unknown";
            switch (sc) { HTTP_STATUS_CODES }
            return os << result;
        }
#undef HTTP_STATUS_CODE

        // str to http status code
        // ~~~~~~~~~~~~~~~~~~~~~~~
#define HTTP_STATUS_CODE(x, y) {#x, http_status_code::y},
        maybe<http_status_code> str_to_http_status_code (std::string const & x) {
            static std::unordered_map<std::string, http_status_code> const map{{HTTP_STATUS_CODES}};
            auto const pos = map.find (x);
            if (pos == map.end ()) {
                // no such status code
                return {};
            }
            return maybe<http_status_code>{pos->second};
        }
#undef HTTP_STATUS_CODE


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

        std::string request_key () {
            static std::random_device rd;
            static std::default_random_engine dre{rd ()};
            static std::uniform_int_distribution<int> uid{
                std::numeric_limits<std::uint8_t>::min (),
                std::numeric_limits<std::uint8_t>::max ()};

            std::array<std::uint8_t, 16> nonce;
            std::generate (std::begin (nonce), std::end (nonce), [&] () { return uid (dre); });
            std::string result;
            to_base64 (std::begin (nonce), std::end (nonce), std::back_inserter (result));
            return result;
        }

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

        long content_length (std::unordered_map<std::string, std::string> const & headers) {
            auto cl = 0L;
            auto const pos = headers.find ("content-length");
            if (pos != std::end (headers)) {
                // TODO: a more robust string to long.
                cl = std::max (std::stol (pos->second), 0L);
            }
            return cl;
        }

    } // end namespace http
} // end namespace pstore
