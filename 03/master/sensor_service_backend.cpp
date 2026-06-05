#include "sensor_service_backend.h"


std::string make_cache_key(
    const std::string &sensor_type,
    const std::string &sensor_id
) {
    return sensor_type + ":" + sensor_id;
}