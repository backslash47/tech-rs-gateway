// SPDX-License-Identifier: MIT
#pragma once
#include "esphome/components/web_server_base/web_server_base.h"

/** @brief HTTP adapter: serves owned snapshots and submits requests to ControllerService.
 * No model mutex or UART write is performed by this class. Responses are sent
 * after service methods have returned and released their internal locks.
 */
class DynamicMenuWeb : public AsyncWebHandler {
  public:
    /**
     * @brief Accesses the HTTP adapter.
     * @return Reference to the process-wide instance; do not delete it.
     */
    static DynamicMenuWeb& instance();
    /// Registers routes and creates the web request token once at startup.
    void start();
    /**
     * @brief Checks whether the request path belongs to this adapter.
     * @param request Live server-owned request, not null.
     * @return true for a registered path; method validation occurs in handleRequest().
     */
    bool canHandle(AsyncWebServerRequest* request) const override;
    /**
     * @brief Serves an owned snapshot or stages a validated controller/settings request.
     * @param request Live server-owned request; ownership is not transferred.
     * @note Runs on the HTTP thread. Network responses are sent after service locks have been
     * released; no direct UART write occurs.
     */
    void handleRequest(AsyncWebServerRequest* request) override;

  private:
    std::string token_;
    static void close_connection(AsyncWebServerRequest* request);
    static void reply(AsyncWebServerRequest* request, int code, const char* type,
                      const std::string& body = "");
    void handle_mqtt(AsyncWebServerRequest* request);
    void serve_assets(AsyncWebServerRequest* request);
};
