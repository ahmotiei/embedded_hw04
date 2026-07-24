#ifndef TYPES_H
#define TYPES_H

#include <string>

struct Config {
    int port = 0;
    std::string database_path;

    std::string slave1_ip;
    int slave1_port = 0;

    std::string slave2_ip;
    int slave2_port = 0;

    std::string shared_slave_ip;
    int slave_timeout_ms = 0;

    std::string memcached_host;
    int memcached_port = 0;
    int cache_ttl_seconds = 0;

    std::string mqtt_broker_host;
    int mqtt_broker_port = 0;
    std::string mqtt_client_id;
    std::string mqtt_request_topic;
    std::string mqtt_response_prefix;
    int mqtt_qos = -1;
    int mqtt_version = 0;
    int mqtt_keepalive = 0;
    int mqtt_reconnect_ms = 0;
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

enum class SlaveQueryStatus {
    Found,
    NotFound,
    Error
};

#endif
