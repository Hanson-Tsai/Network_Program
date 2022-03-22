#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <fstream>
#include <string.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/array.hpp>
#include <vector>

#define MAXSERVER 5

using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std;
//ssh hstsai@nplinux9.cs.nctu.edu.tw
//git clone https://hstsai@bitbucket.org/nycu-np-2021/310552038_np_project3.git
//cp 310552038_np_project3/http_server.cpp np_project3_demo_sample/src/310552038/http_server.cpp
//cp 310552038_np_project3/console.cpp np_project3_demo_sample/src/310552038/console.cpp
//cp 310552038_np_project3/Makefile np_project3_demo_sample/src/310552038/Makefile
//net/gcs/110/310552038/310552038_np_project3

typedef struct host_info{
    string ip;
    string port;
    string testcase;
    bool serving;
}host_info;

typedef struct Socks_info{
    string ip;
    string port;
}Socks_info;

const string formation =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
    "<meta charset=\"UTF-8\" />\n"
    "<title>NP Project 3 Console</title>\n"
    "<link\n"
    "rel=\"stylesheet\"\n"
    "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
    "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
    "crossorigin=\"anonymous\"\n"
    "/>\n"
    "<link\n"
    "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
    "rel=\"stylesheet\"\n"
    "/>\n"
    "<link\n"
    "rel=\"icon\"\n"
    "type=\"image/png\"\n"
    "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n"
    "/>\n"
    "<style>\n"
    "* {\n"
        "font-family: 'Source Code Pro', monospace;\n"
        "font-size: 1rem !important;\n"
   "}\n"
   "body {\n"
        "background-color: #212529;\n"
    "}\n"
    "pre {\n"
        "color: #cccccc;\n"
    "}\n"
    "b {\n"
        "color: #01b468;\n"
    "}\n"
    "</style>\n"
"</head>\n";

const string ending = 
    "</font>\n"
"</body>\n"
"</html>\n";

host_info host_infos[MAXSERVER];
Socks_info socks_info;

void parse_QUERY(void);
void print_header(void);
void print_table(string &header, string &server_id);

void parse_QUERY(){
    int index = 0;
    vector<string> host_argv;
    string QUERY = getenv("QUERY_STRING");

    boost::split(host_argv, QUERY, boost::is_any_of("&"), boost::token_compress_on);
    vector<string>::iterator iter = host_argv.begin();
    
    while(iter != host_argv.end()){
        string var = (*iter).substr(0, (*iter).find("="));
        string token = (*iter).substr(var.size()+1);
        if(!token.empty()){
            if(var.front() == 'h'){
                host_infos[index].serving = true;
                host_infos[index].ip = token;
            }else if(var.front() == 'p'){
                host_infos[index].port = token;
            }else if(var.front() == 'f'){
                host_infos[index++].testcase = token;
            }else if(var == "sh"){
                socks_info.ip = token;
            }else if(var == "sp"){
                socks_info.port = token;
            }
        }
        iter++;
    }
}

void print_header(void){
    cout << "Content-type: text/html\r\n\r\n";

    boost::format table(
        "<table class=\"table table-dark table-bordered\">\n"
            "<thead>\n"
                "<tr id=\"tableHead\">\n"
                    "%1%"
                "</tr>\n"
            "</thead>\n"
            "<tbody>\n"
                "<tr id=\"tableBody\">\n"
                    "%2%"
                "</tr>\n"
            "</tbody>\n"
        "</table>\n");

    string header;
    string server_id;

    print_table(header, server_id);
    
    cout<< formation + (table%header%server_id).str() + ending;
    cout.flush();
}

void print_table(string &header, string &server_id){
    int index = 0;
    while(index < 5){
        if(host_infos[index].serving){
            string tmp = "c" + to_string(index);
            header += "<th scope=\"col\">" + host_infos[index].ip + ":" + host_infos[index].port + "</th>\n";
            server_id  += "<td><pre id=\"" + tmp + "\" class=\"mb-0\"></pre></td>\n";
        }
        index++;
    }
}

class sock4_reply
{
    public:
        unsigned char VN;
        unsigned char status;
        unsigned char dest_Port_High;
        unsigned char dest_Port_Low;
        address_v4::bytes_type dest_IP{};
        
        sock4_reply() {}
        boost::array<mutable_buffer, 5> buffers(){
            boost::array<mutable_buffer, 5> reply_msg = {
                buffer(&VN, 1),
                buffer(&status, 1),
                buffer(&dest_Port_High, 1),
                buffer(&dest_Port_Low, 1),
                buffer(dest_IP),
            };
            return reply_msg;
        }
};

class sock4_request
{
    public:
        enum sock4_type { CONNECT = 0x01};
        unsigned char VN;
        unsigned char CD;
        unsigned short dest_Port;
        unsigned char dest_Port_High;
        unsigned char dest_Port_Low;
        address_v4::bytes_type dest_IP;
        string user_ID;
        unsigned char Null;

        sock4_request(sock4_type command, tcp::endpoint& endpoint)
            :VN(0x04), CD(command), dest_Port(endpoint.port())
            {
                if (endpoint.protocol() != tcp::v4()){
                    throw boost::system::system_error(error::address_family_not_supported);
                }

                dest_Port_High = (dest_Port >> 8) & 0xff;
                dest_Port_Low = dest_Port & 0xff;

                dest_IP = endpoint.address().to_v4().to_bytes();
                user_ID = "";
                Null = 0x00;
            }

        boost::array<mutable_buffer, 7> buffers(){
            boost::array<mutable_buffer, 7> request_msg = {
                buffer(&VN, 1),
                buffer(&CD, 1),
                buffer(&dest_Port_High, 1),
                buffer(&dest_Port_Low, 1),
                buffer(dest_IP),
                buffer(user_ID),
                buffer(&Null, 1)
            };
            return request_msg;
        }
};

class client : public enable_shared_from_this<client>{
    public:
        client(boost::asio::io_context& io_context, string id, tcp::resolver::query sock_query, tcp::resolver::query http_query, string testcase)
            :resolver_p(io_context), socket_p(io_context), id_p(id), sock_query_p(move(sock_query)), http_query_p(move(http_query)){
                file_p.open("test_case/" + testcase, ios::in);
            }
        void start(){
            resolveing();
        }

    private:
        fstream file_p;
        tcp::resolver resolver_p;
        tcp::socket socket_p;
        string id_p;
        tcp::resolver::query http_query_p;
        tcp::resolver::query sock_query_p;
        
        enum {max_length = 10000};
        char data_p[max_length];

        void resolveing(){
            tcp::resolver::iterator socks_iter = resolver_p.resolve(sock_query_p);
            socket_p.connect(*socks_iter);

            tcp::endpoint http_endpoint = *resolver_p.resolve(http_query_p);
            sock4_request sock_req(sock4_request::sock4_type::CONNECT, http_endpoint);
            write(socket_p, sock_req.buffers(), transfer_all());

            sock4_reply sock_reply;
            read(socket_p, sock_reply.buffers(), transfer_all());

            receiving();
        }

        void receiving(){
            auto self(shared_from_this());
            socket_p.async_receive(boost::asio::buffer(data_p, max_length),
                [this, self](boost::system::error_code ec, std::size_t input_length){
                    if(!ec){
                        receive_handle(input_length);
                    }else{
                        socket_p.close();
                    }
                    receiving();
                });
        }

        void receive_handle(size_t input_length){
            bool is_cmd = false;
            string input(data_p, data_p + input_length);
            send_shell(input ,is_cmd);
            if((int)input.find("% ", 0) < 0){
                //do nothing
            }else{
                is_cmd = true;
                string CMD;
                getline(file_p, CMD);
                CMD += "\n";
                send_shell(CMD, is_cmd);
                socket_p.write_some(boost::asio::buffer(CMD));
            }
        }

        void send_shell(string input, bool is_cmd){
            replace_for_html(input);
            if(is_cmd){
                string tmp = "c" + id_p;
                boost::format msg("<script>document.all('%1%').innerHTML += '<font color = \"orange\">%2%</font>';</script>");
                cout << msg%tmp%input;
                cout.flush();
            }else{
                string tmp = "c" + id_p;
                boost::format msg("<script>document.all('%1%').innerHTML += '%2%';</script>");
                cout << msg%tmp%input;
                cout.flush();
            }
        }

        void replace_for_html(string &input){
            boost::algorithm::replace_all(input,"&","&amp;");
            boost::algorithm::replace_all(input,"<","&lt;");
            boost::algorithm::replace_all(input,">","&gt;");
            boost::algorithm::replace_all(input,"\"","&quot;");
            boost::algorithm::replace_all(input,"\'","&apos;");
            boost::algorithm::replace_all(input,"\r\n","\n");
            boost::algorithm::replace_all(input,"\n","<br>");
        }
};

int main(){
    parse_QUERY();
    print_header();

    try{
        boost::asio::io_context io_context;
        for(int i=0; i < MAXSERVER; i++){
            if(host_infos[i].serving){
                tcp::resolver::query sock_query(socks_info.ip, socks_info.port);
                tcp::resolver::query http_query(host_infos[i].ip, host_infos[i].port);
                make_shared<client>(io_context, to_string(i) , move(sock_query), move(http_query), host_infos[i].testcase)->start();
            }
        }
        io_context.run();
    }catch(exception& e){
        cerr << "Exception : " << e.what() << "\n";
    }
    return 0;
}