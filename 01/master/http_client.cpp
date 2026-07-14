#include "http_client.h"

#include <iostream>


struct slave_request_context
{
    std::string *response;
    bool completed;
    bool success;
};



static void slave_callback(
    struct mg_connection *c,
    int ev,
    void *ev_data)
{

    slave_request_context *ctx =
        (slave_request_context *) c->fn_data;


    if(ev == MG_EV_HTTP_MSG)
    {

        std::cout << "HTTP RESPONSE RECEIVED FROM SLAVE" << std::endl;


        struct mg_http_message *hm =
            (struct mg_http_message *) ev_data;


        ctx->response->assign(
            hm->body.buf,
            hm->body.len
        );


        std::cout
        << "SLAVE RESPONSE: "
        << *(ctx->response)
        << std::endl;



        // اگر پاسخ شامل value بود یعنی داده پیدا شده
        if(ctx->response->find("value") != std::string::npos)
        {
            ctx->success = true;
        }
        else
        {
            ctx->success = false;
        }


        ctx->completed = true;


        c->is_closing = 1;
    }
    



    else if(ev == MG_EV_ERROR)
    {

        ctx->success = false;
        ctx->completed = true;

    }

}





bool request_slave(
    struct mg_mgr *mgr,
    const std::string &ip,
    int port,
    const std::string &type,
    int id,
    std::string &response)
{


    std::string url =
        "http://" +
        ip +
        ":" +
        std::to_string(port) +
        "/api/sensor?id=" +
        std::to_string(id) +
        "&type=" +
        type;



    std::cout
        << "Requesting: "
        << url
        << std::endl;



    slave_request_context ctx;

    ctx.response = &response;
    ctx.completed = false;
    ctx.success = false;



    struct mg_connection *conn =
        mg_http_connect(
            mgr,
            url.c_str(),
            slave_callback,
            &ctx
        );

    if(conn == nullptr)
    {
        return false;
    }


    mg_printf(
        conn,
        "GET /api/sensor?id=%d&type=%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n\r\n",
        id,
        type.c_str(),
        ip.c_str()
    );



    if(conn == nullptr)
    {
        return false;
    }



    int timeout = 0;


    while(!ctx.completed && timeout < 50)
    {

        mg_mgr_poll(
            mgr,
            100
        );


        timeout++;

    }



    if(!ctx.completed)
    {
        conn->is_closing = 1;
        return false;
    }

    std::cout 
    << "RETURN FROM SLAVE = "
    << ctx.success
    << std::endl;

    return ctx.success;

}