#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <sqlite3.h>

#include "cache.h"
#include "mongoose.h"

namespace {

struct Config {
    int port = 0;
    std::string database_path;

    std::string memcached_host;
    int memcached_port = 0;
    int cache_ttl_seconds = 0;
};

struct SensorReading {
    std::string sensor_id;
    std::string sensor_type;
    std::string sensor_name;
    std::string location;
    std::string value;
    std::string unit;
    std::string recorded_at;
};

enum class QueryStatus {
    Found,
    NotFound,
    DatabaseError
};

Config g_config;
bool g_cache_available = false;

std::string trim(const std::string &text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string json_escape(const std::string &text) {
    std::ostringstream output;

    for (const char character : text) {
        switch (character) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                output << character;
                break;
        }
    }

    return output.str();
}

std::string sqlite_text(sqlite3_stmt *statement, int column) {
    const unsigned char *value = sqlite3_column_text(statement, column);

    return value == nullptr
        ? ""
        : reinterpret_cast<const char *>(value);
}

bool parse_port(const std::string &value, int &port) {
    try {
        std::size_t parsed_characters = 0;
        const int parsed_port = std::stoi(value, &parsed_characters);

        if (
            parsed_characters != value.size() ||
            parsed_port <= 0 ||
            parsed_port > 65535
        ) {
            return false;
        }

        port = parsed_port;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool parse_positive_integer(
    const std::string &value,
    int &number
) {
    try {
        std::size_t parsed_characters = 0;
        const int parsed_number =
            std::stoi(value, &parsed_characters);

        if (
            parsed_characters != value.size() ||
            parsed_number <= 0
        ) {
            return false;
        }

        number = parsed_number;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool load_config(
    const std::string &config_path,
    Config &config
) {
    std::ifstream file(config_path);

    if (!file.is_open()) {
        std::cerr
            << "Could not open configuration file: "
            << config_path
            << '\n';

        return false;
    }

    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line.front() == '#') {
            continue;
        }

        const std::size_t separator = line.find('=');

        if (separator == std::string::npos) {
            std::cerr
                << "Ignoring invalid configuration line: "
                << line
                << '\n';

            continue;
        }

        const std::string key =
            trim(line.substr(0, separator));

        std::string value =
            trim(line.substr(separator + 1));

        const std::size_t comment_position =
            value.find('#');

        if (comment_position != std::string::npos) {
            value = trim(value.substr(0, comment_position));
        }

        if (key == "PORT") {
            if (!parse_port(value, config.port)) {
                std::cerr << "Invalid PORT value\n";
                return false;
            }
        } else if (key == "DATABASE") {
            config.database_path = value;
        } else if (key == "MEMCACHED_HOST") {
            config.memcached_host = value;
        } else if (key == "MEMCACHED_PORT") {
            if (!parse_port(value, config.memcached_port)) {
                std::cerr << "Invalid MEMCACHED_PORT value\n";
                return false;
            }
        } else if (key == "CACHE_TTL") {
            if (!parse_positive_integer(
                    value,
                    config.cache_ttl_seconds
                )) {
                std::cerr << "Invalid CACHE_TTL value\n";
                return false;
            }
        }
    }

    if (config.port == 0) {
        std::cerr << "PORT is missing\n";
        return false;
    }

    if (config.database_path.empty()) {
        std::cerr << "DATABASE is missing\n";
        return false;
    }

    if (config.memcached_host.empty()) {
        std::cerr << "MEMCACHED_HOST is missing\n";
        return false;
    }

    if (config.memcached_port == 0) {
        std::cerr << "MEMCACHED_PORT is missing\n";
        return false;
    }

    if (config.cache_ttl_seconds == 0) {
        std::cerr << "CACHE_TTL is missing\n";
        return false;
    }

    return true;
}

QueryStatus query_latest_reading(
    const std::string &sensor_id,
    const std::string &sensor_type,
    SensorReading &reading,
    std::string &error_message
) {
    sqlite3 *database = nullptr;

    const int open_result = sqlite3_open_v2(
        g_config.database_path.c_str(),
        &database,
        SQLITE_OPEN_READONLY,
        nullptr
    );

    if (open_result != SQLITE_OK) {
        error_message = database == nullptr
            ? "Could not open SQLite database"
            : sqlite3_errmsg(database);

        if (database != nullptr) {
            sqlite3_close(database);
        }

        return QueryStatus::DatabaseError;
    }

    static const char *query = R"sql(
        SELECT 
        s.sensor_id,
        s.sensor_type,
        s.sensor_name,
        s.location,
        r.value,
        s.unit,
        r.recorded_at
    FROM sensors s
    JOIN sensor_readings r
    ON s.sensor_id = r.sensor_id
    WHERE s.sensor_id = ?
    AND s.sensor_type = ?
    AND s.is_active = 1
    ORDER BY r.recorded_at DESC
    LIMIT 1;
    )sql";

    sqlite3_stmt *statement = nullptr;

    if (
        sqlite3_prepare_v2(
            database,
            query,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    ) {
        error_message = sqlite3_errmsg(database);
        sqlite3_close(database);
        return QueryStatus::DatabaseError;
    }

    sqlite3_bind_text(
        statement,
        1,
        sensor_id.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        sensor_type.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    const int step_result = sqlite3_step(statement);

    if (step_result == SQLITE_ROW) {
        reading.sensor_id = sqlite_text(statement, 0);
        reading.sensor_type = sqlite_text(statement, 1);
        reading.sensor_name = sqlite_text(statement, 2);
        reading.location = sqlite_text(statement, 3);
        reading.value = sqlite_text(statement, 4);
        reading.unit = sqlite_text(statement, 5);
        reading.recorded_at = sqlite_text(statement, 6);

        sqlite3_finalize(statement);
        sqlite3_close(database);

        return QueryStatus::Found;
    }

    if (step_result != SQLITE_DONE) {
        error_message = sqlite3_errmsg(database);

        sqlite3_finalize(statement);
        sqlite3_close(database);

        return QueryStatus::DatabaseError;
    }

    sqlite3_finalize(statement);
    sqlite3_close(database);

    return QueryStatus::NotFound;
}

std::string reading_to_json(
    const SensorReading &reading
) {
    std::ostringstream json;

    json
        << "{"
        << "\"sensor_id\":\""
        << json_escape(reading.sensor_id)
        << "\","
        << "\"sensor_type\":\""
        << json_escape(reading.sensor_type)
        << "\","
        << "\"sensor_name\":\""
        << json_escape(reading.sensor_name)
        << "\","
        << "\"location\":\""
        << json_escape(reading.location)
        << "\","
        << "\"value\":\""
        << json_escape(reading.value)
        << "\","
        << "\"unit\":\""
        << json_escape(reading.unit)
        << "\","
        << "\"recorded_at\":\""
        << json_escape(reading.recorded_at)
        << "\""
        << "}";

    return json.str();
}

std::string make_cache_key(
    const std::string &sensor_type,
    const std::string &sensor_id
) {
    std::string normalized_type = sensor_type;

    std::transform(
        normalized_type.begin(),
        normalized_type.end(),
        normalized_type.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character)
            );
        }
    );

    return
        "sensor:" +
        normalized_type +
        ":" +
        sensor_id;
}

long long elapsed_microseconds(
    const std::chrono::steady_clock::time_point &start_time
) {
    return std::chrono::duration_cast<
        std::chrono::microseconds
    >(
        std::chrono::steady_clock::now() - start_time
    ).count();
}

void send_sensor_response(
    struct mg_connection *connection,
    const char *storage_source,
    long long response_time_us,
    const std::string &reading_json
) {
    mg_http_reply(
        connection,
        200,
        "Content-Type: application/json\r\n",
        "{\"storage_source\":\"%s\","
        "\"response_time_us\":%lld,"
        "\"data\":%s}\n",
        storage_source,
        response_time_us,
        reading_json.c_str()
    );
}

void send_json_error(
    struct mg_connection *connection,
    int status,
    const char *message
) {
    mg_http_reply(
        connection,
        status,
        "Content-Type: application/json\r\n",
        "{\"error\":\"%s\"}\n",
        message
    );
}

void api_handler(
    struct mg_connection *connection,
    int event,
    void *event_data
) {
    if (event != MG_EV_HTTP_MSG) {
        return;
    }

    const auto request_start =
        std::chrono::steady_clock::now();

    auto *request =
        static_cast<struct mg_http_message *>(event_data);

    if (!mg_match(
            request->uri,
            mg_str("/api/sensor"),
            nullptr
        )) {

        send_json_error(
            connection,
            404,
            "route not found"
        );

        return;
    }

    char sensor_id_buffer[128] = {};
    char sensor_type_buffer[128] = {};

    const int id_length = mg_http_get_var(
        &request->query,
        "id",
        sensor_id_buffer,
        sizeof(sensor_id_buffer)
    );

    const int type_length = mg_http_get_var(
        &request->query,
        "type",
        sensor_type_buffer,
        sizeof(sensor_type_buffer)
    );

    if (
        id_length <= 0 ||
        type_length <= 0
    ) {
        send_json_error(
            connection,
            400,
            "id and type query parameters are required"
        );

        return;
    }

    const std::string sensor_id =
        trim(sensor_id_buffer);

    const std::string sensor_type =
        trim(sensor_type_buffer);

    if (sensor_id.empty() || sensor_type.empty()) {
        send_json_error(
            connection,
            400,
            "id and type must not be empty"
        );

        return;
    }

    const std::string cache_key =
        make_cache_key(sensor_type, sensor_id);

    if (g_cache_available) {
        std::string cached_reading_json;
        std::string cache_error;

        const CacheGetStatus cache_status = cache_get(
            cache_key,
            cached_reading_json,
            cache_error
        );

        if (cache_status == CacheGetStatus::Hit) {
            send_sensor_response(
                connection,
                "cache",
                elapsed_microseconds(request_start),
                cached_reading_json
            );

            return;
        }

        if (cache_status == CacheGetStatus::Error) {
            std::cerr
                << "Memcached read error: "
                << cache_error
                << '\n';
        }
    }

    SensorReading reading;
    std::string database_error;

    const QueryStatus status = query_latest_reading(
        sensor_id,
        sensor_type,
        reading,
        database_error
    );

    if (status == QueryStatus::Found) {
        const std::string reading_json =
            reading_to_json(reading);

        if (g_cache_available) {
            std::string cache_error;

            if (!cache_set(
                    cache_key,
                    reading_json,
                    static_cast<std::time_t>(
                        g_config.cache_ttl_seconds
                    ),
                    cache_error
                )) {
                std::cerr
                    << "Memcached write error: "
                    << cache_error
                    << '\n';
            }
        }

        send_sensor_response(
            connection,
            "sqlite",
            elapsed_microseconds(request_start),
            reading_json
        );

        return;
    }

    if (status == QueryStatus::NotFound) {
        send_json_error(
            connection,
            404,
            "sensor reading not found"
        );

        return;
    }

    std::cerr
        << "SQLite error: "
        << database_error
        << '\n';

    send_json_error(
        connection,
        500,
        "database error"
    );
}

}  // namespace

int main(int argc, char *argv[]) {
    const std::string config_path =
        argc > 1 ? argv[1] : "config";

    if (!load_config(config_path, g_config)) {
        return EXIT_FAILURE;
    }

    std::string cache_error;

    g_cache_available = cache_init(
        g_config.memcached_host,
        static_cast<std::uint16_t>(
            g_config.memcached_port
        ),
        cache_error
    );

    if (!g_cache_available) {
        std::cerr
            << "Memcached initialization failed: "
            << cache_error
            << ". Falling back to SQLite.\n";
    }

    struct mg_mgr manager;
    mg_mgr_init(&manager);

    const std::string listen_address =
        "http://0.0.0.0:" +
        std::to_string(g_config.port);

    if (
        mg_http_listen(
            &manager,
            listen_address.c_str(),
            api_handler,
            nullptr
        ) == nullptr
    ) {
        std::cerr
            << "Could not start Slave on "
            << listen_address
            << '\n';

        mg_mgr_free(&manager);
        return EXIT_FAILURE;
    }

    std::cout
        << "Slave running on port "
        << g_config.port
        << ", database: "
        << g_config.database_path
        << ", cache: "
        << (
            g_cache_available
                ? "enabled"
                : "disabled"
        )
        << '\n';

    while (true) {
        mg_mgr_poll(&manager, 100);
    }

    mg_mgr_free(&manager);
    cache_shutdown();
    return EXIT_SUCCESS;
}