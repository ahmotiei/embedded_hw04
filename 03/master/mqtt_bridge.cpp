#include "mqtt_bridge.h"

#include "sensor_service.h"
#include "types.h"

#include <MQTTClient.h>

#include <iostream>
#include <string>


extern Config g_config;

static MQTTClient client = nullptr;


static std::string response_topic(
    const std::string &sensor_id
) {
    return g_config.mqtt_response_prefix + "/" + sensor_id;
}


static std::string extract_json_value(
    const std::string &payload,
    const std::string &key
) {
    const std::string pattern = "\"" + key + "\"";

    const std::size_t key_pos = payload.find(pattern);

    if (key_pos == std::string::npos) {
        return "";
    }

    const std::size_t colon = payload.find(
        ':',
        key_pos + pattern.size()
    );

    if (colon == std::string::npos) {
        return "";
    }

    std::size_t start = colon + 1;

    while (
        start < payload.size() &&
        (payload[start] == ' ' || payload[start] == '"')
    ) {
        start++;
    }

    std::size_t end = start;

    while (
        end < payload.size() &&
        payload[end] != '"' &&
        payload[end] != ',' &&
        payload[end] != '}'
    ) {
        end++;
    }

    return payload.substr(start, end - start);
}


static int message_callback(
    void *,
    char *,
    int,
    MQTTClient_message *message
) {
    const std::string payload(
        static_cast<char *>(message->payload),
        message->payloadlen
    );
    std::cout
        << "MQTT message received: "
        << payload
        << std::endl;

    const std::string sensor_type =
        extract_json_value(payload, "sensor_type");

    const std::string sensor_id =
        extract_json_value(payload, "sensor_id");

    if (
        sensor_type.empty() ||
        sensor_id.empty()
    ) {
        MQTTClient_freeMessage(&message);
        return 1;
    }


    const SensorResult result =
        lookup_sensor(
            sensor_type,
            sensor_id
        );


    std::string response = "{";

    response += "\"sensor_id\":\"" + sensor_id + "\",";
    response += "\"source\":\"" + result.source + "\",";
    response += "\"success\":" +
        std::string(result.success ? "true" : "false") + ",";

    if (result.success) {
        response += "\"data\":" + result.sensor_json + ",";
    } else {
        response += "\"error\":\"" +
            result.error_message + "\",";
    }

    response += "\"response_time_us\":" +
        std::to_string(result.response_time_us);

    response += "}";


    MQTTClient_message mqtt_message =
        MQTTClient_message_initializer;

    mqtt_message.payload =
        const_cast<char *>(response.c_str());

    mqtt_message.payloadlen =
        static_cast<int>(response.size());

    mqtt_message.qos =
        g_config.mqtt_qos;

    mqtt_message.retained = 0;


    MQTTClient_deliveryToken token;

    MQTTClient_publishMessage(
        client,
        response_topic(sensor_id).c_str(),
        &mqtt_message,
        &token
    );

    MQTTClient_waitForCompletion(
        client,
        token,
        1000
    );


    MQTTClient_freeMessage(&message);

    return 1;
}


bool mqtt_init() {
    const std::string address =
        "tcp://" +
        g_config.mqtt_broker_host +
        ":" +
        std::to_string(
            g_config.mqtt_broker_port
        );


    int create_rc =
        MQTTClient_create(
            &client,
            address.c_str(),
            g_config.mqtt_client_id.c_str(),
            MQTTCLIENT_PERSISTENCE_NONE,
            nullptr
        );


    if (create_rc != MQTTCLIENT_SUCCESS) {
        std::cerr
            << "MQTT client creation failed\n";

        return false;
    }


    MQTTClient_setCallbacks(
        client,
        nullptr,
        nullptr,
        message_callback,
        nullptr
    );


    std::cout
        << "MQTT callback registered"
        << std::endl;


    MQTTClient_connectOptions options =
        MQTTClient_connectOptions_initializer;


    options.keepAliveInterval =
        g_config.mqtt_keepalive;


    const int rc =
        MQTTClient_connect(
            client,
            &options
        );


    if (rc != MQTTCLIENT_SUCCESS) {

        std::cerr
            << "MQTT connection failed rc="
            << rc
            << "\n";

        return false;
    }


    int subscribe_rc =
        MQTTClient_subscribe(
            client,
            g_config.mqtt_request_topic.c_str(),
            g_config.mqtt_qos
        );


    std::cout
        << "MQTT subscribe topic: "
        << g_config.mqtt_request_topic
        << " rc="
        << subscribe_rc
        << std::endl;


    std::cout
        << "MQTT connected\n";


    return true;
}


void mqtt_loop()
{
    MQTTClient_yield();
}


void mqtt_shutdown() {
    if (client == nullptr) {
        return;
    }

    MQTTClient_disconnect(
        client,
        1000
    );

    MQTTClient_destroy(
        &client
    );

    client = nullptr;
}
