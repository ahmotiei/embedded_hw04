#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include <string>


struct SensorResult {

    bool success;

    std::string source;

    std::string sensor_json;

    std::string error_message;

    long long response_time_us;
};


SensorResult lookup_sensor(
    const std::string &sensor_type,
    const std::string &sensor_id
);


#endif