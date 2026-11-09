// SPDX-License-Identifier: MIT
#pragma once
#include "controller_service.h"

namespace tech_rs_gateway {
/** @brief HTTP-independent response, suitable for sending after all model locks are released. */
struct ApiResponse {
    int status;
    std::string body;
};

/** @brief Menu request validation and JSON projection with no HTTP/socket dependencies. */
class MenuApi {
  public:
    /**
     * @brief Checks the request token and optional browser Origin against the Host.
     * @param token Received request token, or nullopt if absent.
     * @param origin Optional Origin; when present it must equal http:// followed by host.
     * @param host Received Host header, including port when applicable.
     * @param expected_token Server-side nonempty request token.
     * @return true if the token/origin checks pass.
     * @note This is a request guard, not user authentication. No service state is accessed.
     */
    static bool authorized(const std::optional<std::string>& token,
                           const std::optional<std::string>& origin,
                           const std::optional<std::string>& host,
                           const std::string& expected_token);
    /**
     * @brief Validates menu pagination and serializes an owned service snapshot.
     * @param service Controller service; it handles locking internally.
     * @param group One of MU, MI, MS or SU.
     * @param offset Nonnegative decimal row offset as request text.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @param token Request token exposed in the returned web snapshot.
     * @param heap Current free heap in bytes for diagnostics.
     * @return HTTP 200 with JSON data, or 400 with a structured invalid_request result.
     * @note Returned response owns its body and retains no model lock.
     */
    static ApiResponse menu(ControllerService& service, const std::string& group,
                            const std::string& offset, uint32_t now, const std::string& token,
                            size_t heap);
    /**
     * @brief Validates a web write envelope and submits it to the controller service.
     * @param service Shared controller service.
     * @param authorized Result of the adapter request-token/origin checks.
     * @param key Required menu key, including group and ID.
     * @param value Required integer text in controller units.
     * @param expected Required previous scalar value or complete raw action record.
     * @param action Select experimental action validation rather than a scalar write.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @return Owned response: 202 queued, 403 unauthorized, 400 missing parameters, or 409
     * validation failure.
     * @note No network I/O or direct UART write is performed; acceptance is not readback
     * confirmation.
     */
    static ApiResponse write(ControllerService& service, bool authorized,
                             const std::optional<std::string>& key,
                             const std::optional<std::string>& value,
                             const std::optional<std::string>& expected, bool action, uint32_t now);
};
} // namespace tech_rs_gateway
