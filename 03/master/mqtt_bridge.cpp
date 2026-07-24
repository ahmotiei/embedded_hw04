#include "mqtt_bridge.h"

#include "sensor_service.h"
#include "types.h"

#include <MQTTClient.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

extern Config g_config;

namespace {

MQTTClient g_client = nullptr;
std::atomic<bool> g_connected{false};
std::chrono::steady_clock::time_point g_last_reconnect_attempt =
    std::chrono::steady_clock::time_point::min();

std::string json_escape(const std::string &text) {
    std::ostringstream output;

    for (const char character : text) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default: output << character; break;
        }
    }

    return output.str();
}

std::string response_topic(const std::string &sensor_id) {
    const std::string suffix = sensor_id.empty() ? "error" : sensor_id;
    return g_config.mqtt_response_prefix + "/" + suffix;
}

std::string extract_json_string(
    const std::string &payload,
    const std::string &key
) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_position = payload.find(pattern);

    if (key_position == std::string::npos) {
        return "";
    }

    const std::size_t colon_position = payload.find(
        ':',
        key_position + pattern.size()
    );

    if (colon_position == std::string::npos) {
        return "";
    }

    std::size_t position = colon_position + 1;

    while (
        position < payload.size() &&
        (payload[position] == ' ' || payload[position] == '\t')
    ) {
        ++position;
    }

    if (position >= payload.size() || payload[position] != '"') {
        return "";
    }

    ++position;
    std::string value;
    bool escaped = false;

    for (; position < payload.size(); ++position) {
        const char character = payload[position];

        if (escaped) {
            switch (character) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(character); break;
            }

            escaped = false;
            continue;
        }

        if (character == '\\') {
            escaped = true;
            continue;
        }

        if (character == '"') {
            return value;
        }

        value.push_back(character);
    }

    return "";
}

bool publish_response(
    const std::string &sensor_id,
    const std::string &response
) {
    if (g_client == nullptr || !MQTTClient_isConnected(g_client)) {
        std::cerr << "MQTT response could not be published: client disconnected\n";
        return false;
    }

    MQTTClient_message message = MQTTClient_message_initializer;
    message.payload = const_cast<char *>(response.c_str());
    message.payloadlen = static_cast<int>(response.size());
    message.qos = g_config.mqtt_qos;
    message.retained = 0;

    MQTTClient_deliveryToken token = 0;
    const int publish_result = MQTTClient_publishMessage(
        g_client,
        response_topic(sensor_id).c_str(),
        &message,
        &token
    );

    if (publish_result != MQTTCLIENT_SUCCESS) {
        std::cerr
            << "MQTT publish failed: "
            << MQTTClient_strerror(publish_result)
            << " (rc=" << publish_result << ")\n";
        return false;
    }

    const int completion_result = MQTTClient_waitForCompletion(
        g_client,
        token,
        3000
    );

    if (completion_result != MQTTCLIENT_SUCCESS) {
        std::cerr
            << "MQTT delivery confirmation failed: "
            << MQTTClient_strerror(completion_result)
            << " (rc=" << completion_result << ")\n";
        return false;
    }

    return true;
}

void connection_lost_callback(void *, char *cause) {
    g_connected.store(false);

    std::cerr << "MQTT connection lost";

    if (cause != nullptr && *cause != '\0') {
        std::cerr << ": " << cause;
    }

    std::cerr << '\n';
}

bool connect_and_subscribe() {
    if (g_client == nullptr) {
        return false;
    }

    MQTTClient_connectOptions options = MQTTClient_connectOptions_initializer;
    options.keepAliveInterval = g_config.mqtt_keepalive;
    options.cleansession = 1;
    options.MQTTVersion = MQTTVERSION_3_1_1;

    const int connect_result = MQTTClient_connect(g_client, &options);

    if (connect_result != MQTTCLIENT_SUCCESS) {
        g_connected.store(false);
        std::cerr
            << "MQTT connection failed: "
            << MQTTClient_strerror(connect_result)
            << " (rc=" << connect_result << ")\n";
        return false;
    }

    const int subscribe_result = MQTTClient_subscribe(
        g_client,
        g_config.mqtt_request_topic.c_str(),
        g_config.mqtt_qos
    );

    if (subscribe_result != MQTTCLIENT_SUCCESS) {
        std::cerr
            << "MQTT subscription failed: "
            << MQTTClient_strerror(subscribe_result)
            << " (rc=" << subscribe_result << ")\n";
        MQTTClient_disconnect(g_client, 1000);
        g_connected.store(false);
        return false;
    }

    g_connected.store(true);

    std::cout
        << "MQTT connected and subscribed to "
        << g_config.mqtt_request_topic
        << " with QoS "
        << g_config.mqtt_qos
        << " using MQTT 3.1.1\n";

    return true;
}

int message_callback(
    void *,
    char *topic_name,
    int,
    MQTTClient_message *message
) {
    const std::string payload(
        static_cast<const char *>(message->payload),
        static_cast<std::size_t>(message->payloadlen)
    );

    const std::string sensor_type =
        extract_json_string(payload, "sensor_type");
    const std::string sensor_id =
        extract_json_string(payload, "sensor_id");

    std::string response;

    if (sensor_type.empty() || sensor_id.empty()) {
        response =
            "{\"sensor_id\":\"" + json_escape(sensor_id) +
            "\",\"source\":\"error\",\"success\":false," 
            "\"error\":\"sensor_type and sensor_id are required\"," 
            "\"response_time_us\":0}";
    } else {
        const SensorResult result = lookup_sensor(sensor_type, sensor_id);

        response = "{";
        response += "\"sensor_id\":\"" + json_escape(sensor_id) + "\",";
        response += "\"source\":\"" + json_escape(result.source) + "\",";
        response += "\"success\":";
        response += result.success ? "true," : "false,";

        if (result.success) {
            response += "\"data\":" + result.sensor_json + ",";
        } else {
            response +=
                "\"error\":\"" +
                json_escape(result.error_message) +
                "\",";
        }

        response +=
            "\"response_time_us\":" +
            std::to_string(result.response_time_us) +
            "}";
    }

    publish_response(sensor_id, response);

    MQTTClient_freeMessage(&message);

    if (topic_name != nullptr) {
        MQTTClient_free(topic_name);
    }

    return 1;
}

}  // namespace

bool mqtt_init() {
    const std::string address =
        "tcp://" +
        g_config.mqtt_broker_host +
        ":" +
        std::to_string(g_config.mqtt_broker_port);

    const int create_result = MQTTClient_create(
        &g_client,
        address.c_str(),
        g_config.mqtt_client_id.c_str(),
        MQTTCLIENT_PERSISTENCE_NONE,
        nullptr
    );

    if (create_result != MQTTCLIENT_SUCCESS) {
        std::cerr
            << "MQTT client creation failed: "
            << MQTTClient_strerror(create_result)
            << " (rc=" << create_result << ")\n";
        g_client = nullptr;
        return false;
    }

    const int callback_result = MQTTClient_setCallbacks(
        g_client,
        nullptr,
        connection_lost_callback,
        message_callback,
        nullptr
    );

    if (callback_result != MQTTCLIENT_SUCCESS) {
        std::cerr
            << "MQTT callback registration failed: "
            << MQTTClient_strerror(callback_result)
            << " (rc=" << callback_result << ")\n";
        MQTTClient_destroy(&g_client);
        return false;
    }

    g_last_reconnect_attempt = std::chrono::steady_clock::now();

    if (!connect_and_subscribe()) {
        MQTTClient_destroy(&g_client);
        return false;
    }

    return true;
}

void mqtt_loop() {
    if (g_client == nullptr) {
        return;
    }

    if (MQTTClient_isConnected(g_client)) {
        g_connected.store(true);
        MQTTClient_yield();
        return;
    }

    g_connected.store(false);

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - g_last_reconnect_attempt
    ).count();

    if (elapsed < g_config.mqtt_reconnect_ms) {
        return;
    }

    g_last_reconnect_attempt = now;
    std::cerr << "Attempting MQTT reconnect...\n";
    connect_and_subscribe();
}

void mqtt_shutdown() {
    if (g_client == nullptr) {
        return;
    }

    if (MQTTClient_isConnected(g_client)) {
        MQTTClient_disconnect(g_client, 1000);
    }

    MQTTClient_destroy(&g_client);
    g_client = nullptr;
    g_connected.store(false);
}
