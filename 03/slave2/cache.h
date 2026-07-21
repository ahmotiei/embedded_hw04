#ifndef CACHE_H
#define CACHE_H

#include <cstdint>
#include <ctime>
#include <string>

enum class CacheGetStatus {
    Hit,
    Miss,
    Error
};

bool cache_init(
    const std::string &host,
    std::uint16_t port,
    std::string &error_message
);

void cache_shutdown();

CacheGetStatus cache_get(
    const std::string &key,
    std::string &value,
    std::string &error_message
);

bool cache_set(
    const std::string &key,
    const std::string &value,
    std::time_t ttl_seconds,
    std::string &error_message
);

#endif
