#include <iostream>
#include <fstream>
#include <string>
#include <sqlite3.h>

#include "mongoose.h"



int PORT;

std::string DATABASE;



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


        mg_http_reply(
        c,
        404,
        "",
        "{\"error\":\"not found in master\"}");

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
        NULL);



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