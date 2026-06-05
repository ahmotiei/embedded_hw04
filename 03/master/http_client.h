#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>

bool request_slave(
    const std::string &ip,
    int port,
    const std::string &sensor_type,
    const std::string &sensor_id,
    int &http_status,
    std::string &response,
    int timeout_ms = 3000
);

#endif
