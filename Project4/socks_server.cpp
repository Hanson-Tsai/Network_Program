#include <sys/wait.h>
#include <string.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <fstream>
#include <stdio.h>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/execution_context.hpp>
//ssh hstsai@nplinux9.cs.nctu.edu.tw

using namespace std;
using boost::asio::ip::tcp;

boost::asio::io_context io_context;

struct IP_set{
  string ip[4];
};

vector<IP_set> Firewall_C;
vector<IP_set> Firewall_B;

class socket4_request{
  public:
      int VN;
      int CD;
      string source_IP;
      unsigned short source_PORT;
      unsigned char dest_IP[4];
      unsigned short dest_PORT;
      string dest_IP_str;
      string DomainName;
      bool IsDomainName;
      bool NotAccept;
      
      void parse_request(unsigned char *data_){
        VN = (int)data_[0];
        CD = (int)data_[1];

        dest_PORT =((int)data_[2]) * 256 + (int)data_[3];
        dest_IP[0] = data_[4];
        dest_IP[1] = data_[5];
        dest_IP[2] = data_[6];
        dest_IP[3] = data_[7];
        NotAccept = 0;

        if ((int)data_[4] == 0 && (int)data_[5] == 0 && (int)data_[6] == 0 && (int)data_[7] != 0)
        {
          int count = 0;
          int DomainName_length = 0;
          while((int)data_[8 + count] != 0)
            count++;
          while((int)data_[9 + count + DomainName_length] != 0){
            DomainName += data_[9 + count + DomainName_length];
            DomainName_length++;
          }
          dest_IP_str = DomainName;
          IsDomainName = true;
        }
        else{
          dest_IP_str = to_string((int)data_[4]) + "." + to_string((int)data_[5]) + "." + to_string((int)data_[6]) + "." + to_string((int)data_[7]);
          IsDomainName = false;
        }
      }

      bool check_firewall(){
        firewall_config();
        bool pass = false;
        vector<IP_set>::iterator iter1;
        vector<IP_set>::iterator iter2;
        if (CD == 1){
          if (Firewall_C.size() < 1)
            return false;
          iter1 = Firewall_C.begin();
          iter2 = Firewall_C.end();
        }else if (CD == 2){
          if (Firewall_B.size() < 1)
            return false;
          iter1 = Firewall_B.begin();
          iter2 = Firewall_B.end();
        }

        while (iter1 != iter2){
          int count = 0;
          while (count < 4){
            if ((*iter1).ip[count][0] == '*'){
              count++;
            }else{
              int tmp = (int)dest_IP[count];
              if (atoi(((*iter1).ip[count]).c_str()) != tmp)
                break;
              count++;
            }
          }

          if (count == 4){
            pass = true;
            NotAccept = 0;
            break;
          }
          iter1++;
        }
        return pass;
      }

      void print_info(){
        cerr << "<S_IP>: " << source_IP << endl;
        cerr << "<S_PORT>: " << source_PORT << endl;
        cerr << "<D_IP>: " << dest_IP_str << endl;
        cerr << "<D_PORT>: " << dest_PORT << endl;

        if (CD == 1)
          cerr << "<Command>: CONNECT" << endl;
        else
          cerr << "<Command>: BIND" << endl;

        if (NotAccept == 0)
          cerr << "<Reply>: Accept" << endl;
        else
          cerr << "<Reply>: Reject" << endl;
      }

  private:
      void firewall_config(){
        Firewall_C.clear();
        Firewall_B.clear();
        ifstream file("socks.conf");
        string line;

        while (getline(file, line)){
          stringstream substring1(line);
          string tmp;
          vector<string> token;
          while (getline(substring1, tmp, ' ')){
            token.push_back(tmp);
          }
          if (token.size() < 4){
            IP_set permit_IP;
            stringstream substring2(token[2]);
            string token2;
            for (int i = 0; i < 4; i++){
              getline(substring2, token2, '.');
              permit_IP.ip[i] = token2;
            }

            if (token[1] == "c")
              Firewall_C.push_back(permit_IP);
            else if (token[1] == "b")
              Firewall_B.push_back(permit_IP);
          }
        }
        file.close();
      }
};

class socks
  : public std::enable_shared_from_this<socks>
{
public:
  socks(tcp::socket socket)
    : socket_(std::move(socket)),
      socket_dest(io_context),
      resolver(io_context),
      acceptor(io_context)
  {}

  void start(){
    do_read();
  }

private:
  tcp::socket socket_;
  tcp::socket socket_dest;
  tcp::resolver resolver;
  tcp::acceptor acceptor;
  tcp::resolver::results_type endpoint;
  enum { MAX_LENGTH = 65536 };
  unsigned char data_[MAX_LENGTH];
  char client_buffer[MAX_LENGTH];
  char server_buffer[MAX_LENGTH];
  unsigned char Reply_msg[8];
  socket4_request req_;

  void do_read()
  {
    auto self(shared_from_this());
    bzero(data_, sizeof(data_));
    socket_.async_read_some(boost::asio::buffer(data_, MAX_LENGTH),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec){
            req_.source_IP = socket_.remote_endpoint().address().to_string();
            req_.source_PORT = htons(socket_.remote_endpoint().port());
            req_.parse_request(data_);
            switch (req_.CD)
            {
            case 1: // Connect
              Resolving();
              break;
            case 2: // Bind
              Binding();
              break;
            }
          }
        });
  }

  void Binding(){
    auto self(shared_from_this());
    unsigned short tmp_port = 0;
    
    while (true)
    {
      srand(time(nullptr));
      tmp_port = rand() % 10000 + 10000;
      tcp::endpoint _endpoint(tcp::v4(), tmp_port);
      acceptor.open(_endpoint.protocol());

      boost::system::error_code ec;
      acceptor.bind(_endpoint, ec);

      if (!ec){
        break;
      }
      else{
        acceptor.close();
      }
    }

    req_.dest_PORT = tmp_port;
    acceptor.listen();
    req_.NotAccept = 0;
    setup_reply();
  }

  void Resolving(){
    auto self(shared_from_this());
    string port = to_string(req_.dest_PORT);
    resolver.async_resolve(req_.dest_IP_str, port,
        [this,self](const boost::system::error_code& ec,
        tcp::resolver::results_type results)
        {
          if(!ec){
            endpoint = results;
            Connecting();
          }
          else{
            Rejecting();
          }
        });
  }

  void Connecting(){
    auto self(shared_from_this());
    boost::asio::async_connect(socket_dest, endpoint,
      [this,self](boost::system::error_code ec, tcp::endpoint){
        if (!ec){
          req_.dest_IP_str = socket_dest.remote_endpoint().address().to_string();
          stringstream dst(req_.dest_IP_str);

          for (int i = 0; i < 4; i++){
            string tmp;
            getline(dst, tmp, '.');
            req_.dest_IP[i] = (unsigned char)atoi(tmp.c_str());
          }
          if(req_.check_firewall()){
            setup_reply();
          }else{
            Rejecting();
          }
        }else{
          Rejecting();
        }
      });
  }

  void Rejecting(){
    req_.NotAccept = 1;
    setup_reply();
    socket_.close();
    socket_dest.close();
  }

  void setup_reply(){
    auto self(shared_from_this());
    bzero(Reply_msg,sizeof(Reply_msg));
    Reply_msg[0] = 0x00;
    if(req_.NotAccept == 0)
      Reply_msg[1] = 0x5a;
    else
      Reply_msg[1] = 0x5b;

    switch (req_.CD){
      case 1: //Connect
        Reply_msg[2] = 0x00;
        Reply_msg[3] = 0x00;
        Reply_msg[4] = 0x00;
        Reply_msg[5] = 0x00;
        Reply_msg[6] = 0x00;
        Reply_msg[7] = 0x00;
        Replying();
        break;
      case 2: // Bind
        Reply_msg[2] = req_.dest_PORT / 256;
        Reply_msg[3] = req_.dest_PORT % 256;
        Reply_msg[4] = 0x00;
        Reply_msg[5] = 0x00;
        Reply_msg[6] = 0x00;
        Reply_msg[7] = 0x00;
        pre_bind_reply();
        break;
    }
  }

  void pre_bind_reply(){
    auto self(shared_from_this());
    boost::asio::async_write(socket_,boost::asio::buffer(Reply_msg, sizeof(unsigned char)*8),
      [this,self](boost::system::error_code ec, std::size_t) {
        if (!ec){
          Accepting();
        }
      });
  }

  void Accepting(){
    auto self(shared_from_this());
    boost::system::error_code ec;
    acceptor.accept(socket_dest, ec);
    if (!ec){
      acceptor.close();
      Replying();
    }else{
      acceptor.close();
      Rejecting();
    }
  }

  void Replying(){
    auto self(shared_from_this());
    boost::asio::async_write(socket_,boost::asio::buffer(Reply_msg, sizeof(unsigned char)*8),
      [this,self](boost::system::error_code ec, std::size_t) {
        if (!ec){
          req_.print_info();
          if(req_.NotAccept == 0){
            Reading_from_client();
            Reading_from_server();
          }
        }
      });
  }

  void Reading_from_client(){
    auto self(shared_from_this());
    bzero(client_buffer, sizeof(client_buffer));
    socket_.async_read_some(boost::asio::buffer(client_buffer, MAX_LENGTH),
      [this, self](boost::system::error_code ec, std::size_t length) {
        if (!ec){
          Writing_to_server(length);
        }else{
          socket_.close();
          socket_dest.close();
        }
      });
  }

  void Reading_from_server(){
    auto self(shared_from_this());
    bzero(server_buffer, sizeof(server_buffer));
    socket_dest.async_read_some(boost::asio::buffer(server_buffer, MAX_LENGTH),
      [this, self](boost::system::error_code ec, std::size_t read_length) {
        if (!ec){
          Writing_to_client(read_length);
        }else{
          socket_.close();
          socket_dest.close();
        }
      });
  }

  void Writing_to_client(std::size_t read_length){
    auto self(shared_from_this());
    boost::asio::async_write(socket_,boost::asio::buffer(server_buffer, read_length),
      [this,self](boost::system::error_code ec, std::size_t) {
        if (!ec){
          Reading_from_server();
        }else{
          socket_.close();
          socket_dest.close();
        }
      });
  }

  void Writing_to_server(std::size_t read_length){
    auto self(shared_from_this());
    boost::asio::async_write(socket_dest,boost::asio::buffer(client_buffer, read_length),
      [this,self](boost::system::error_code ec, std::size_t) {
        if (!ec){
          Reading_from_client();
        }else{
          socket_.close();
          socket_dest.close();
        }
      });
  }
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            pid_t pid;
            io_context.notify_fork(boost::asio::io_context::fork_prepare);
            pid = fork();

            if(pid < 0){
              socket.close();
              cerr << "fork error" << endl;
            }

            switch (pid)
            {
            case 0:
              io_context.notify_fork(boost::asio::io_context::fork_child);
              acceptor_.close();
              std::make_shared<socks>(std::move(socket))->start();
              break;
            
            default:
              io_context.notify_fork(boost::asio::io_context::fork_parent);
              socket.close();
              break;
            }
          }
          do_accept();
        });
  }
  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]){

  try{
    if (argc != 2){
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }
    
    int port = atoi(argv[1]);

    server s(io_context, port);

    io_context.run();
  }catch (std::exception& e){
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}