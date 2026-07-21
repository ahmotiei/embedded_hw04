#ifndef SENSOR_SERVICE_INTERNAL_H
#define SENSOR_SERVICE_INTERNAL_H

#include "types.h"
#include "sensor_service.h"

#include <string>


extern Config g_config;

extern bool g_cache_available;


std::string make_cache_key(
    const std::string &sensor_type,
    const std::string &sensor_id
);


void store_in_master_cache(
    const std::string &cache_key,
    const std::string &sensor_json
);


QueryStatus query_latest_reading(
    const std::string &sensor_id,
    const std::string &sensor_type,
    SensorReading &reading,
    std::string &error_message
);


SlaveQueryStatus query_slave(
    const std::string &slave_name,
    const std::string &ip,
    int port,
    const std::string &sensor_type,
    const std::string &sensor_id,
    int timeout_ms,
    std::string &response
);


std::string reading_to_json(
    const SensorReading &reading
);


bool extract_sensor_data_json(
    const std::string &response,
    std::string &sensor_json
);


#endif