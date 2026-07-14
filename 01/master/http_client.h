#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>

#include "mongoose.h"


bool request_slave(
    struct mg_mgr *mgr,
    const std::string &ip,
    int port,
    const std::string &type,
    int id,
    std::string &response
);


#endif