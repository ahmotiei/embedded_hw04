#include <iostream>
#include <fstream>
#include <string>
#include <sqlite3.h>

#include "mongoose.h"
#include "http_client.h"



int PORT;

std::string DATABASE;

std::string SLAVE1_IP;
int SLAVE1_PORT;


std::string SLAVE2_IP;
int SLAVE2_PORT;

void load_config()
{

    std::ifstream file("config");

    std::string line;


    while(getline(file,line))
    {

        if(line.find("PORT=")==0)
            PORT=stoi(line.substr(5));


        else if(line.find("DATABASE=")==0)
            DATABASE=line.substr(9);

        else if(line.find("SLAVE1_IP=")==0)
            SLAVE1_IP=line.substr(10);


        else if(line.find("SLAVE1_PORT=")==0)
            SLAVE1_PORT=stoi(line.substr(12));


        else if(line.find("SLAVE2_IP=")==0)
            SLAVE2_IP=line.substr(10);


        else if(line.find("SLAVE2_PORT=")==0)
            SLAVE2_PORT=stoi(line.substr(12));

    }

}




bool query_database(
        int id,
        std::string type,
        double &value)
{


    sqlite3 *db;


    sqlite3_open(
        DATABASE.c_str(),
        &db);



    std::string sql =
    "SELECT value FROM sensors "
    "WHERE id="+std::to_string(id)+
    " AND type='"+type+"' "
    "ORDER BY timestamp DESC LIMIT 1;";



    sqlite3_stmt *stmt;


    sqlite3_prepare_v2(
        db,
        sql.c_str(),
        -1,
        &stmt,
        NULL);



    bool found=false;



    if(sqlite3_step(stmt)==SQLITE_ROW)
    {

        value =
        sqlite3_column_double(stmt,0);

        found=true;

    }



    sqlite3_finalize(stmt);

    sqlite3_close(db);



    return found;

}


void handler(
struct mg_connection *c,
int ev,
void *ev_data)
{

    struct mg_mgr *mgr =
    (struct mg_mgr *) c->fn_data;



    if(ev!=MG_EV_HTTP_MSG)
        return;



    auto *hm =
    (struct mg_http_message*)ev_data;



    char id[20];

    char type[50];



    mg_http_get_var(
        &hm->query,
        "id",
        id,
        sizeof(id));



    mg_http_get_var(
        &hm->query,
        "type",
        type,
        sizeof(type));



    double value;



    if(query_database(
        atoi(id),
        type,
        value))
    {


        mg_http_reply(
        c,
        200,
        "Content-Type: application/json\r\n",
        "{"
        "\"source\":\"master\","
        "\"value\":%.2f"
        "}",
        value);


    }

    else
    {

        std::string response;



        if(request_slave(
            mgr,
            SLAVE1_IP,
            SLAVE1_PORT,
            type,
            atoi(id),
            response))
        {

            mg_http_reply(
            c,
            200,
            "Content-Type: application/json\r\n",
            "{\"source\":\"slave1\",\"data\":%s}",
            response.c_str()
            );


            return;

        }



        if(request_slave(
            mgr,
            SLAVE2_IP,
            SLAVE2_PORT,
            type,
            atoi(id),
            response))
        {

            mg_http_reply(
            c,
            200,
            "Content-Type: application/json\r\n",
            "{\"source\":\"slave2\",\"data\":%s}",
            response.c_str()
            );


            return;

        }



        mg_http_reply(
        c,
        404,
        "Content-Type: application/json\r\n",
        "{\"error\":\"sensor not found\"}");

    }


}





int main()
{


    load_config();


    struct mg_mgr mgr;


    mg_mgr_init(&mgr);



    std::string url =
    "http://0.0.0.0:"
    +std::to_string(PORT);



    mg_http_listen(
        &mgr,
        url.c_str(),
        handler,
        &mgr);



    std::cout
    <<"Master running on "
    <<PORT
    <<std::endl;



    while(true)
    {
        mg_mgr_poll(
            &mgr,
            1000);
    }



}