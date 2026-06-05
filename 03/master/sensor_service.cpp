#include "sensor_service.h"
#include "sensor_service_internal.h"
#include "cache.h"

#include <chrono>


SensorResult lookup_sensor(
    const std::string &sensor_type,
    const std::string &sensor_id
) {
    const auto start =
        std::chrono::steady_clock::now();

    SensorResult result;

    result.success = false;
    result.source = "";
    result.sensor_json = "";
    result.error_message = "";


    const std::string cache_key =
        make_cache_key(
            sensor_type,
            sensor_id
        );


    std::string cached_json;

    if (g_cache_available) {
        std::string cache_error;

        const CacheGetStatus status =
            cache_get(
                cache_key,
                cached_json,
                cache_error
            );

        if (status == CacheGetStatus::Hit) {
            result.success = true;
            result.source = "cache";
            result.sensor_json = cached_json;
        }
    }


    if (!result.success) {

        SensorReading reading;
        std::string database_error;


        const QueryStatus local_status =
            query_latest_reading(
                sensor_id,
                sensor_type,
                reading,
                database_error
            );


        if (local_status == QueryStatus::Found) {

            result.success = true;
            result.source = "master";
            result.sensor_json =
                reading_to_json(reading);

            store_in_master_cache(
                cache_key,
                result.sensor_json
            );
        }
    }


    if (!result.success) {

        std::string response;


        SlaveQueryStatus status =
            query_slave(
                "slave1",
                g_config.slave1_ip,
                g_config.slave1_port,
                sensor_type,
                sensor_id,
                g_config.slave_timeout_ms,
                response
            );


        if (status == SlaveQueryStatus::Found) {

            result.success =
                extract_sensor_data_json(
                    response,
                    result.sensor_json
                );

            if (result.success) {
                result.source = "slave1";

                store_in_master_cache(
                    cache_key,
                    result.sensor_json
                );
            }
        }
    }


    if (!result.success) {

        std::string response;


        SlaveQueryStatus status =
            query_slave(
                "slave2",
                g_config.slave2_ip,
                g_config.slave2_port,
                sensor_type,
                sensor_id,
                g_config.slave_timeout_ms,
                response
            );


        if (status == SlaveQueryStatus::Found) {

            result.success =
                extract_sensor_data_json(
                    response,
                    result.sensor_json
                );

            if (result.success) {
                result.source = "slave2";

                store_in_master_cache(
                    cache_key,
                    result.sensor_json
                );
            }
        }
    }


    if (!result.success) {
        result.error_message =
            "sensor reading not found";
    }


    result.response_time_us =
        std::chrono::duration_cast<
            std::chrono::microseconds
        >(
            std::chrono::steady_clock::now()
            - start
        ).count();


    return result;
}
