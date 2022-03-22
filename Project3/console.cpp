#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <fstream>
#include <string.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <vector>

#define MAXSERVER 5

using boost::asio::ip::tcp;
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

void parse_QUERY(void);
void print_header(void);
void print_table(string &header, string &server_id);

void parse_QUERY(){
    int index = -1;
    vector<string> host_argv;
    string QUERY = getenv("QUERY_STRING");

    boost::split(host_argv, QUERY, boost::is_any_of("&"), boost::token_compress_on);
    for(int i=0; i < MAXSERVER*3; i++){
        switch (i%3)
        {
        case 0:
            index++;
            host_infos[index].ip = host_argv[i].substr(3, host_argv[i].length());
            break;
        case 1:
            host_infos[index].port = host_argv[i].substr(3, host_argv[i].length());
            break;
        case 2:
            host_infos[index].testcase = host_argv[i].substr(3, host_argv[i].length());
            break;
        }
    }
    
    for(int i=0; i < MAXSERVER; i++){
        if(host_infos[i].ip.length() != 0){
            host_infos[i].serving = true;
        }else{
            host_infos[i].serving = false;
        }
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
        "<\table>\n");

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

class client : public enable_shared_from_this<client>{
    public:
        client(boost::asio::io_context& io_context, string id, tcp::resolver::query q, string testcase)
            :resolver_p(io_context), socket_p(io_context), id_p(id), q_p(move(q)){
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
        tcp::resolver::query q_p;
        
        enum {max_length = 10000};
        char data_p[max_length];

        void resolveing(){
            auto self(shared_from_this());
            resolver_p.async_resolve(q_p,
                [this, self](boost::system::error_code ec, tcp::resolver::iterator iter){
                    if(!ec){
                        resolveing_handle(iter);
                    }else{
                        cerr << "Resolve error" << endl;
                    }
                });
        }

        void resolveing_handle(tcp::resolver::iterator iter){
            auto self(shared_from_this());
            socket_p.async_connect(*iter,
                [this, self](boost::system::error_code ec){
                    if(!ec){
                        connect_handle();
                    }else{
                        cerr << "Resolve handler error" << endl;
                    }
                });
        }

        void connect_handle(){
            auto self(shared_from_this());
            socket_p.async_receive(boost::asio::buffer(data_p, max_length),
                [this, self](boost::system::error_code ec, std::size_t input_length){
                    if(!ec){
                        receive_handle(input_length);
                    }else{
                        socket_p.close();
                    }
                    connect_handle();
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
                tcp::resolver::query q(host_infos[i].ip, host_infos[i].port);
                make_shared<client>(io_context, to_string(i) ,move(q), host_infos[i].testcase)->start();
            }
        }
        io_context.run();
    }catch(exception& e){
        cerr << "Exception : " << e.what() << "\n";
    }
    return 0;
}