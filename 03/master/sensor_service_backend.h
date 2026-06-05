#ifndef SENSOR_SERVICE_BACKEND_H
#define SENSOR_SERVICE_BACKEND_H

#include "types.h"

#include <string>


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


#endif