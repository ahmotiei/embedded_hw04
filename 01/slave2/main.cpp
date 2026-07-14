#include <iostream>
#include <fstream>
#include <cstdlib>
#include <sqlite3.h>

#include "mongoose.h"



std::string DB;
int PORT;



void load_config()
{

std::ifstream file("config");


std::string line;


while(getline(file,line))
{


if(line.find("PORT=")==0)

PORT=stoi(line.substr(5));


if(line.find("DATABASE=")==0)

DB=line.substr(9);


}


}




bool query_sensor(
int id,
std::string type,
double &value)
{


sqlite3 *db;


sqlite3_open(
DB.c_str(),
&db);



std::string sql=
"SELECT value FROM sensors "
"WHERE id="+std::to_string(id)+
" AND type='"+type+
"' ORDER BY timestamp DESC LIMIT 1;";



sqlite3_stmt *stmt;


sqlite3_prepare_v2(
db,
sql.c_str(),
-1,
&stmt,
NULL);



bool result=false;


if(sqlite3_step(stmt)==SQLITE_ROW)
{

value=
sqlite3_column_double(stmt,0);

result=true;

}



sqlite3_finalize(stmt);

sqlite3_close(db);


return result;


}



void api_handler(
struct mg_connection *c,
int ev,
void *ev_data)
{


if(ev!=MG_EV_HTTP_MSG)
return;



auto *hm=
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



if(query_sensor(
atoi(id),
type,
value))
{


mg_http_reply(
c,
200,
"Content-Type: application/json\r\n",
"{\"value\":%.2f}",
value);


}


else
{


mg_http_reply(
c,
404,
"",
"{\"error\":\"not found\"}");

}



}




int main()
{


load_config();



struct mg_mgr mgr;


mg_mgr_init(&mgr);



std::string url=
"http://0.0.0.0:"
+std::to_string(PORT);



mg_http_listen(
&mgr,
url.c_str(),
api_handler,
NULL);



std::cout
<<"Slave running port "
<<PORT
<<std::endl;



while(true)

mg_mgr_poll(
&mgr,
1000);



}
