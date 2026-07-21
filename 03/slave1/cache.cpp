#include "cache.h"

#include <cstdlib>

#include <libmemcached/memcached.h>

namespace {

memcached_st *g_memcached = nullptr;

std::string memcached_error(
    memcached_return_t result
) {
    if (g_memcached == nullptr) {
        return memcached_strerror(nullptr, result);
    }

    return memcached_strerror(g_memcached, result);
}

}  // namespace

bool cache_init(
    const std::string &host,
    std::uint16_t port,
    std::string &error_message
) {
    error_message.clear();

    cache_shutdown();

    g_memcached = memcached_create(nullptr);

    if (g_memcached == nullptr) {
        error_message = "could not allocate memcached client";
        return false;
    }

    memcached_return_t result = MEMCACHED_SUCCESS;

    memcached_server_st *servers =
        memcached_server_list_append(
            nullptr,
            host.c_str(),
            port,
            &result
        );

    if (
        result != MEMCACHED_SUCCESS ||
        servers == nullptr
    ) {
        error_message = memcached_error(result);
        cache_shutdown();
        return false;
    }

    result = memcached_server_push(
        g_memcached,
        servers
    );

    memcached_server_list_free(servers);

    if (result != MEMCACHED_SUCCESS) {
        error_message = memcached_error(result);
        cache_shutdown();
        return false;
    }

    return true;
}

void cache_shutdown() {
    if (g_memcached != nullptr) {
        memcached_free(g_memcached);
        g_memcached = nullptr;
    }
}

CacheGetStatus cache_get(
    const std::string &key,
    std::string &value,
    std::string &error_message
) {
    value.clear();
    error_message.clear();

    if (g_memcached == nullptr) {
        error_message = "memcached client is not initialized";
        return CacheGetStatus::Error;
    }

    std::size_t value_length = 0;
    std::uint32_t flags = 0;
    memcached_return_t result = MEMCACHED_SUCCESS;

    char *stored_value = memcached_get(
        g_memcached,
        key.c_str(),
        key.size(),
        &value_length,
        &flags,
        &result
    );

    if (result == MEMCACHED_NOTFOUND) {
        std::free(stored_value);
        return CacheGetStatus::Miss;
    }

    if (
        result != MEMCACHED_SUCCESS ||
        stored_value == nullptr
    ) {
        error_message = memcached_error(result);
        std::free(stored_value);
        return CacheGetStatus::Error;
    }

    value.assign(stored_value, value_length);
    std::free(stored_value);

    return CacheGetStatus::Hit;
}

bool cache_set(
    const std::string &key,
    const std::string &value,
    std::time_t ttl_seconds,
    std::string &error_message
) {
    error_message.clear();

    if (g_memcached == nullptr) {
        error_message = "memcached client is not initialized";
        return false;
    }

    const memcached_return_t result = memcached_set(
        g_memcached,
        key.c_str(),
        key.size(),
        value.c_str(),
        value.size(),
        ttl_seconds,
        0
    );

    if (result != MEMCACHED_SUCCESS) {
        error_message = memcached_error(result);
        return false;
    }

    return true;
}
