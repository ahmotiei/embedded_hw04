#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <sqlite3.h>

#include "cache.h"
#include "http_client.h"
#include "mongoose.h"
#include "sensor_service.h"
#include "types.h"
#include "sensor_service_backend.h"
#include "mqtt_bridge.h"


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
        const int parsed_number = std::stoi(
            value,
            &parsed_characters
        );

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

bool parse_mqtt_qos(
    const std::string &value,
    int &qos
) {
    try {
        std::size_t parsed_characters = 0;

        const int parsed_qos = std::stoi(
            value,
            &parsed_characters
        );

        if (
            parsed_characters != value.size() ||
            parsed_qos < 0 ||
            parsed_qos > 2
        ) {
            return false;
        }

        qos = parsed_qos;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool parse_mqtt_version(
    const std::string &value,
    int &version
) {
    try {
        std::size_t parsed_characters = 0;

        const int parsed_version = std::stoi(
            value,
            &parsed_characters
        );

        if (
            parsed_characters != value.size() ||
            (
                parsed_version != 4 &&
                parsed_version != 5
            )
        ) {
            return false;
        }

        version = parsed_version;
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
        } else if (key == "SLAVE_IP") {
            config.shared_slave_ip = value;
        } else if (key == "SLAVE1_IP") {
            config.slave1_ip = value;
        } else if (key == "SLAVE2_IP") {
            config.slave2_ip = value;
        } else if (key == "SLAVE1_PORT") {
            if (!parse_port(value, config.slave1_port)) {
                std::cerr << "Invalid SLAVE1_PORT value\n";
                return false;
            }
        } else if (key == "SLAVE2_PORT") {
            if (!parse_port(value, config.slave2_port)) {
                std::cerr << "Invalid SLAVE2_PORT value\n";
                return false;
            }
        } else if (key == "SLAVE_TIMEOUT_MS") {
            if (!parse_positive_integer(
                    value,
                    config.slave_timeout_ms
                )) {

                std::cerr
                    << "Invalid SLAVE_TIMEOUT_MS value\n";

                return false;
            }
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
        } else if (key == "MQTT_BROKER_HOST") {
            config.mqtt_broker_host = value;
        } else if (key == "MQTT_BROKER_PORT") {
            if (!parse_port(
                    value,
                    config.mqtt_broker_port
                )) {
                std::cerr
                    << "Invalid MQTT_BROKER_PORT value\n";

                return false;
            }
        } else if (key == "MQTT_CLIENT_ID") {
            config.mqtt_client_id = value;
        } else if (key == "MQTT_REQUEST_TOPIC") {
            config.mqtt_request_topic = value;
        } else if (key == "MQTT_RESPONSE_PREFIX") {
            config.mqtt_response_prefix = value;
        } else if (key == "MQTT_QOS") {
            if (!parse_mqtt_qos(
                    value,
                    config.mqtt_qos
                )) {
                std::cerr
                    << "Invalid MQTT_QOS value. "
                    << "Allowed values are 0, 1 and 2.\n";

                return false;
            }
        } else if (key == "MQTT_VERSION") {
            if (!parse_mqtt_version(
                    value,
                    config.mqtt_version
                )) {
                std::cerr
                    << "Invalid MQTT_VERSION value. "
                    << "Allowed values are 4 and 5.\n";

                return false;
            }
        } else if (key == "MQTT_KEEPALIVE") {
            if (!parse_positive_integer(
                    value,
                    config.mqtt_keepalive
                )) {
                std::cerr
                    << "Invalid MQTT_KEEPALIVE value\n";

                return false;
            }
        } else if (key == "MQTT_RECONNECT_MS") {
            if (!parse_positive_integer(
                    value,
                    config.mqtt_reconnect_ms
                )) {
                std::cerr
                    << "Invalid MQTT_RECONNECT_MS value\n";

                return false;
            }
        }
    }

    // Advanced shared-IP mode is used only when a
    // node-specific IP has not been provided.
    if (config.slave1_ip.empty()) {
        config.slave1_ip = config.shared_slave_ip;
    }

    if (config.slave2_ip.empty()) {
        config.slave2_ip = config.shared_slave_ip;
    }

    if (config.port == 0) {
        std::cerr << "PORT is missing\n";
        return false;
    }

    if (config.database_path.empty()) {
        std::cerr << "DATABASE is missing\n";
        return false;
    }

    if (
        config.slave1_ip.empty() ||
        config.slave2_ip.empty()
    ) {
        std::cerr
            << "Slave IP configuration is missing. "
            << "Set SLAVE_IP or both SLAVE1_IP and SLAVE2_IP.\n";

        return false;
    }

    if (
        config.slave1_port == 0 ||
        config.slave2_port == 0
    ) {
        std::cerr << "Slave port configuration is missing\n";
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

        if (config.cache_ttl_seconds <= 0) {
        std::cerr << "CACHE_TTL is missing\n";
        return false;
    }

    if (config.mqtt_broker_host.empty()) {
        std::cerr << "MQTT_BROKER_HOST is missing\n";
        return false;
    }

    if (config.mqtt_broker_port == 0) {
        std::cerr << "MQTT_BROKER_PORT is missing\n";
        return false;
    }

    if (config.mqtt_client_id.empty()) {
        std::cerr << "MQTT_CLIENT_ID is missing\n";
        return false;
    }

    if (config.mqtt_request_topic.empty()) {
        std::cerr << "MQTT_REQUEST_TOPIC is missing\n";
        return false;
    }

    if (config.mqtt_response_prefix.empty()) {
        std::cerr << "MQTT_RESPONSE_PREFIX is missing\n";
        return false;
    }

    if (
        config.mqtt_keepalive <= 0 ||
        config.mqtt_reconnect_ms <= 0
    ) {
        std::cerr
            << "MQTT timing configuration is invalid\n";

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
        FROM sensors AS s
        INNER JOIN sensor_readings AS r
            ON r.sensor_id = s.sensor_id
        WHERE s.sensor_type = ?1 COLLATE NOCASE
          AND s.sensor_id = ?2
          AND s.is_active = 1
        ORDER BY r.recorded_at DESC, r.id DESC
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
        sensor_type.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        sensor_id.c_str(),
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
    const char *source,
    long long response_time_us,
    const std::string &sensor_json
) {
    mg_http_reply(
        connection,
        200,
        "Content-Type: application/json\r\n",
        "{\"source\":\"%s\"," 
        "\"response_time_us\":%lld,"
        "\"data\":%s}\n",
        source,
        response_time_us,
        sensor_json.c_str()
    );
}

bool extract_sensor_data_json(
    const std::string &slave_response,
    std::string &sensor_json
) {
    sensor_json.clear();

    const std::string key = "\"data\"";
    std::size_t key_position = slave_response.find(key);

    if (key_position == std::string::npos) {
        return false;
    }

    std::size_t colon_position = slave_response.find(
        ':',
        key_position + key.size()
    );

    if (colon_position == std::string::npos) {
        return false;
    }

    std::size_t object_start = colon_position + 1;

    while (
        object_start < slave_response.size() &&
        std::isspace(
            static_cast<unsigned char>(
                slave_response[object_start]
            )
        )
    ) {
        ++object_start;
    }

    if (
        object_start >= slave_response.size() ||
        slave_response[object_start] != '{'
    ) {
        return false;
    }

    int brace_depth = 0;
    bool inside_string = false;
    bool escaped = false;

    for (
        std::size_t index = object_start;
        index < slave_response.size();
        ++index
    ) {
        const char character = slave_response[index];

        if (inside_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inside_string = false;
            }

            continue;
        }

        if (character == '"') {
            inside_string = true;
        } else if (character == '{') {
            ++brace_depth;
        } else if (character == '}') {
            --brace_depth;

            if (brace_depth == 0) {
                sensor_json = slave_response.substr(
                    object_start,
                    index - object_start + 1
                );

                return true;
            }
        }
    }

    return false;
}

void store_in_master_cache(
    const std::string &cache_key,
    const std::string &sensor_json
) {
    if (!g_cache_available) {
        return;
    }

    std::string cache_error;

    if (!cache_set(
            cache_key,
            sensor_json,
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

SlaveQueryStatus query_slave(
    const std::string &node_name,
    const std::string &ip,
    int port,
    const std::string &sensor_type,
    const std::string &sensor_id,
    int timeout_ms,
    std::string &response
) {
    int http_status = 0;

    std::cout
        << "Querying "
        << node_name
        << " at "
        << ip
        << ":"
        << port
        << '\n';

    const bool received_response = request_slave(
        ip,
        port,
        sensor_type,
        sensor_id,
        http_status,
        response,
        timeout_ms
    );

    if (!received_response) {
        std::cerr
            << node_name
            << " did not return a valid HTTP response\n";

        return SlaveQueryStatus::Error;
    }

    if (http_status == 200 && !response.empty()) {
        return SlaveQueryStatus::Found;
    }

    if (http_status == 404) {
        return SlaveQueryStatus::NotFound;
    }

    std::cerr
        << node_name
        << " returned HTTP status "
        << http_status
        << '\n';

    return SlaveQueryStatus::Error;
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

void request_handler(
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

    // The Master cache is always checked before the local SQLite
    // database and before either Slave is contacted.
    if (g_cache_available) {
        std::string cached_sensor_json;
        std::string cache_error;

        const CacheGetStatus cache_status = cache_get(
            cache_key,
            cached_sensor_json,
            cache_error
        );

        if (cache_status == CacheGetStatus::Hit) {
            send_sensor_response(
                connection,
                "cache",
                elapsed_microseconds(request_start),
                cached_sensor_json
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

    SensorReading local_reading;
    std::string database_error;

    const QueryStatus local_status =
        query_latest_reading(
            sensor_id,
            sensor_type,
            local_reading,
            database_error
        );

    bool distributed_query_incomplete = false;

    if (local_status == QueryStatus::Found) {
        const std::string sensor_json =
            reading_to_json(local_reading);

        store_in_master_cache(cache_key, sensor_json);

        send_sensor_response(
            connection,
            "master",
            elapsed_microseconds(request_start),
            sensor_json
        );

        return;
    }

    if (local_status == QueryStatus::DatabaseError) {
        distributed_query_incomplete = true;

        std::cerr
            << "Master SQLite error: "
            << database_error
            << '\n';
    }

    std::string slave_response;

    SlaveQueryStatus slave_status = query_slave(
        "slave1",
        g_config.slave1_ip,
        g_config.slave1_port,
        sensor_type,
        sensor_id,
        g_config.slave_timeout_ms,
        slave_response
    );

    if (slave_status == SlaveQueryStatus::Found) {
        std::string sensor_json;

        if (!extract_sensor_data_json(
                slave_response,
                sensor_json
            )) {
            std::cerr
                << "Could not extract sensor data from Slave1 response\n";

            distributed_query_incomplete = true;
        } else {
            store_in_master_cache(cache_key, sensor_json);

            send_sensor_response(
                connection,
                "slave1",
                elapsed_microseconds(request_start),
                sensor_json
            );

            return;
        }
    }

    if (slave_status == SlaveQueryStatus::Error) {
        distributed_query_incomplete = true;
    }

    slave_response.clear();

    slave_status = query_slave(
        "slave2",
        g_config.slave2_ip,
        g_config.slave2_port,
        sensor_type,
        sensor_id,
        g_config.slave_timeout_ms,
        slave_response
    );

    if (slave_status == SlaveQueryStatus::Found) {
        std::string sensor_json;

        if (!extract_sensor_data_json(
                slave_response,
                sensor_json
            )) {
            std::cerr
                << "Could not extract sensor data from Slave2 response\n";

            distributed_query_incomplete = true;
        } else {
            store_in_master_cache(cache_key, sensor_json);

            send_sensor_response(
                connection,
                "slave2",
                elapsed_microseconds(request_start),
                sensor_json
            );

            return;
        }
    }

    if (slave_status == SlaveQueryStatus::Error) {
        distributed_query_incomplete = true;
    }

    if (distributed_query_incomplete) {
        send_json_error(
            connection,
            503,
            "distributed query could not be completed"
        );

        return;
    }

    send_json_error(
        connection,
        404,
        "sensor reading not found in any node"
    );
}

 // namespace

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

    if(!mqtt_init())
    {
        std::cerr
            <<
            "MQTT initialization failed\n";
    }

    if (!g_cache_available) {
        std::cerr
            << "Memcached initialization failed: "
            << cache_error
            << " -- continuing without Master cache\n";
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
            request_handler,
            nullptr
        ) == nullptr
    ) {
        std::cerr
            << "Could not start Master on "
            << listen_address
            << '\n';

        mqtt_shutdown();
        mg_mgr_free(&manager);
        cache_shutdown();
        return EXIT_FAILURE;
    }

    std::cout
        << "Master running on port "
        << g_config.port
        << ", database: "
        << g_config.database_path
        << ", cache: "
        << (g_cache_available ? "enabled" : "disabled")
        << '\n';

    if (g_cache_available) {
        std::cout
            << "Master Memcached: "
            << g_config.memcached_host
            << ':'
            << g_config.memcached_port
            << ", TTL: "
            << g_config.cache_ttl_seconds
            << " seconds\n";
    }

    std::cout
        << "Slave1: "
        << g_config.slave1_ip
        << ':'
        << g_config.slave1_port
        << '\n';

    std::cout
        << "Slave2: "
        << g_config.slave2_ip
        << ':'
        << g_config.slave2_port
        << '\n';

    std::cout
        << "MQTT Broker: "
        << g_config.mqtt_broker_host
        << ':'
        << g_config.mqtt_broker_port
        << '\n';

    std::cout
        << "MQTT Client ID: "
        << g_config.mqtt_client_id
        << '\n';

    std::cout
        << "MQTT Request Topic: "
        << g_config.mqtt_request_topic
        << '\n';

    std::cout
        << "MQTT Response Prefix: "
        << g_config.mqtt_response_prefix
        << '\n';

    std::cout
        << "MQTT Version: "
        << (
            g_config.mqtt_version == 4
                ? "3.1.1"
                : "5"
        )
        << ", QoS: "
        << g_config.mqtt_qos
        << ", Keepalive: "
        << g_config.mqtt_keepalive
        << " seconds\n";

    while (true) {
        mg_mgr_poll(&manager, 100);
        mqtt_loop();
    }

    mqtt_shutdown();
    mg_mgr_free(&manager);
    cache_shutdown();
    return EXIT_SUCCESS;
}