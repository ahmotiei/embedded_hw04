#include "mongoose/mongoose.h"
#include "sensor_log_store.hpp"

#include <atomic>
#include <cctype>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

std::atomic_bool g_running{true};

struct ServerContext {
    SensorLogStore store;

    explicit ServerContext(std::vector<std::string> database_paths)
        : store(std::move(database_paths)) {}
};

struct Options {
    std::string listen_url;
    std::vector<std::string> database_paths;
};

void signal_handler(int) {
    g_running.store(false);
}

void print_usage(const char *program) {
    std::cerr
        << "Usage: " << program
        << " --listen <http://IP:PORT> --db <sqlite-path> [--db <sqlite-path> ...]\n";
}

bool parse_arguments(int argc, char **argv, Options &options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--listen" && index + 1 < argc) {
            options.listen_url = argv[++index];
        } else if (argument == "--db" && index + 1 < argc) {
            options.database_paths.emplace_back(argv[++index]);
        } else if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown or incomplete argument: " << argument << '\n';
            print_usage(argv[0]);
            return false;
        }
    }

    if (options.listen_url.empty()) {
        std::cerr << "Missing required --listen option.\n";
        return false;
    }
    if (options.database_paths.empty()) {
        std::cerr << "At least one --db option is required.\n";
        return false;
    }
    return true;
}

bool equals(const struct mg_str &value, const char *expected) {
    const std::size_t length = std::strlen(expected);
    return value.len == length && std::memcmp(value.buf, expected, length) == 0;
}

bool valid_date(const std::string &date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }

    for (std::size_t index = 0; index < date.size(); ++index) {
        if (index == 4 || index == 7) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(date[index]))) {
            return false;
        }
    }

    const int year = std::stoi(date.substr(0, 4));
    const int month = std::stoi(date.substr(5, 2));
    const int day = std::stoi(date.substr(8, 2));

    if (year < 1970 || month < 1 || month > 12 || day < 1) {
        return false;
    }

    static const int days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    int maximum_day = days_per_month[month - 1];
    const bool leap = (year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0));
    if (month == 2 && leap) {
        maximum_day = 29;
    }

    return day <= maximum_day;
}

void reply_json(
    mg_connection *connection,
    int status,
    const std::string &body,
    const char *extra_headers = ""
) {
    std::string headers =
        "Content-Type: application/json; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n";
    headers += extra_headers;
    mg_http_reply(connection, status, headers.c_str(), "%s", body.c_str());
}

std::string error_json(const std::string &message) {
    return "{\n  \"error\": \"" + json_escape(message) + "\"\n}\n";
}

void handle_sensor_logs(
    mg_connection *connection,
    mg_http_message *request,
    ServerContext &context
) {
    if (!equals(request->method, "GET")) {
        reply_json(
            connection,
            405,
            error_json("Only the GET method is allowed"),
            "Allow: GET\r\n"
        );
        return;
    }

    char sensor_name_buffer[256] = "";
    char sensor_id_buffer[128] = "";
    char date_buffer[32] = "";

    const int sensor_name_length = mg_http_get_var(
        &request->query,
        "sensor_name",
        sensor_name_buffer,
        static_cast<int>(sizeof(sensor_name_buffer))
    );
    const int sensor_id_length = mg_http_get_var(
        &request->query,
        "sensor_id",
        sensor_id_buffer,
        static_cast<int>(sizeof(sensor_id_buffer))
    );
    const int date_length = mg_http_get_var(
        &request->query,
        "date",
        date_buffer,
        static_cast<int>(sizeof(date_buffer))
    );

    if (sensor_name_length <= 0 || sensor_id_length <= 0 || date_length <= 0) {
        reply_json(
            connection,
            400,
            error_json("sensor_name, sensor_id and date query parameters are required")
        );
        return;
    }

    const std::string sensor_name(sensor_name_buffer);
    const std::string sensor_id(sensor_id_buffer);
    const std::string date(date_buffer);

    if (!valid_date(date)) {
        reply_json(connection, 400, error_json("date must be a valid YYYY-MM-DD value"));
        return;
    }

    SensorLogQueryResult result;
    std::string query_error;
    if (!context.store.query(sensor_name, sensor_id, date, result, query_error)) {
        reply_json(connection, 500, error_json(query_error));
        return;
    }

    const bool found = !result.values.empty();
    reply_json(
        connection,
        found ? 200 : 404,
        build_sensor_log_json(sensor_name, sensor_id, date, result, found)
    );
}

void event_handler(mg_connection *connection, int event, void *event_data) {
    if (event != MG_EV_HTTP_MSG) {
        return;
    }

    auto *request = static_cast<mg_http_message *>(event_data);
    auto *context = static_cast<ServerContext *>(connection->fn_data);

    if (mg_match(request->uri, mg_str("/api/sensor-logs"), nullptr)) {
        handle_sensor_logs(connection, request, *context);
    } else if (mg_match(request->uri, mg_str("/health"), nullptr)) {
        if (!equals(request->method, "GET")) {
            reply_json(
                connection,
                405,
                error_json("Only the GET method is allowed"),
                "Allow: GET\r\n"
            );
        } else {
            reply_json(connection, 200, "{\n  \"status\": \"ok\"\n}\n");
        }
    } else {
        reply_json(connection, 404, error_json("Route not found"));
    }
}

}  // namespace

int main(int argc, char **argv) {
    Options options;
    if (!parse_arguments(argc, argv, options)) {
        return 2;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    ServerContext context(options.database_paths);
    mg_mgr manager;
    mg_mgr_init(&manager);

    mg_connection *listener = mg_http_listen(
        &manager,
        options.listen_url.c_str(),
        event_handler,
        &context
    );

    if (listener == nullptr) {
        std::cerr << "Could not listen on " << options.listen_url << '\n';
        mg_mgr_free(&manager);
        return 1;
    }

    std::cout << "Sensor log API is listening on " << options.listen_url << '\n';
    for (const std::string &database_path : options.database_paths) {
        std::cout << "SQLite source: " << database_path << '\n';
    }

    while (g_running.load()) {
        mg_mgr_poll(&manager, 200);
    }

    mg_mgr_free(&manager);
    std::cout << "Sensor log API stopped.\n";
    return 0;
}
