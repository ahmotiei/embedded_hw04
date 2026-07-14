#include "http_client.h"

#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "mongoose.h"

namespace {

struct RequestContext {
    std::string request_path;
    std::string host_header;

    bool completed = false;
    bool transport_error = false;

    int http_status = 0;
    std::string response;
    std::string error_message;
};

std::string url_encode(const std::string &value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;

    for (const unsigned char character : value) {
        if (
            std::isalnum(character) ||
            character == '-' ||
            character == '_' ||
            character == '.' ||
            character == '~'
        ) {
            encoded << static_cast<char>(character);
        } else {
            encoded
                << '%'
                << std::setw(2)
                << std::setfill('0')
                << static_cast<int>(character);
        }
    }

    return encoded.str();
}

void client_handler(
    struct mg_connection *connection,
    int event,
    void *event_data
) {
    auto *context =
        static_cast<RequestContext *>(connection->fn_data);

    if (context == nullptr) {
        return;
    }

    if (event == MG_EV_CONNECT) {
        mg_printf(
            connection,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Accept: application/json\r\n"
            "Connection: close\r\n"
            "\r\n",
            context->request_path.c_str(),
            context->host_header.c_str()
        );

        return;
    }

    if (event == MG_EV_HTTP_MSG) {
        auto *message =
            static_cast<struct mg_http_message *>(event_data);

        context->http_status = mg_http_status(message);

        context->response.assign(
            message->body.buf,
            message->body.len
        );

        context->completed = true;
        connection->is_closing = 1;
        return;
    }

    if (event == MG_EV_ERROR) {
        context->transport_error = true;
        context->completed = true;

        if (event_data != nullptr) {
            context->error_message =
                static_cast<const char *>(event_data);
        }

        return;
    }

    if (event == MG_EV_CLOSE && !context->completed) {
        context->transport_error = true;
        context->completed = true;
        context->error_message =
            "connection closed before a complete HTTP response";
    }
}

}  // namespace

bool request_slave(
    const std::string &ip,
    int port,
    const std::string &sensor_type,
    const std::string &sensor_id,
    int &http_status,
    std::string &response,
    int timeout_ms
) {
    http_status = 0;
    response.clear();

    struct mg_mgr manager;
    mg_mgr_init(&manager);

    RequestContext context;

    context.request_path =
        "/api/sensor?type=" +
        url_encode(sensor_type) +
        "&id=" +
        url_encode(sensor_id);

    context.host_header =
        ip + ":" + std::to_string(port);

    const std::string server_url =
        "http://" + ip + ":" + std::to_string(port);

    struct mg_connection *connection = mg_http_connect(
        &manager,
        server_url.c_str(),
        client_handler,
        &context
    );

    if (connection == nullptr) {
        mg_mgr_free(&manager);
        return false;
    }

    const auto start_time =
        std::chrono::steady_clock::now();

    while (!context.completed) {
        mg_mgr_poll(&manager, 25);

        const auto elapsed_ms =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                std::chrono::steady_clock::now() -
                start_time
            ).count();

        if (elapsed_ms >= timeout_ms) {
            connection->is_closing = 1;
            context.transport_error = true;
            context.completed = true;
            context.error_message =
                "request timed out";
        }
    }

    http_status = context.http_status;
    response = context.response;

    if (
        context.transport_error &&
        !context.error_message.empty()
    ) {
        std::cerr
            << "HTTP client error for "
            << server_url
            << ": "
            << context.error_message
            << '\n';
    }

    const bool received_http_response =
        !context.transport_error &&
        context.http_status > 0;

    mg_mgr_free(&manager);
    return received_http_response;
}