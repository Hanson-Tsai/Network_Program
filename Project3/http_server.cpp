#include <sys/wait.h>
#include <string.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>


using namespace std;
using boost::asio::ip::tcp;
//ssh hstsai@nplinux9.cs.nctu.edu.tw
//git clone https://hstsai@bitbucket.org/nycu-np-2021/310552038_np_project3.git
//cp 310552038_np_project3/http_server.cpp np_project3_demo_sample/src/310552038/http_server.cpp
//cp 310552038_np_project3/Makefile np_project3_demo_sample/src/310552038/Makefile
//net/gcs/110/310552038/310552038_np_project3

char **request_argv;

class enviro{
    public:
        string get_env_variables(int index);
        void parse_request(string request);
        
    private:
        string env_variables[9];
};

class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket))
  {
  }

  void start()
  {
    do_read();
  }

private:
  void connection(){
    dup2(socket_.native_handle(), STDOUT_FILENO);
    cout << "HTTP/1.1 200 OK\r\n" << flush;
    socket_.close();
  }

  void do_read()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            enviro env;
            string request = data_;
#ifdef TEST
            cout << "HTTP Request："<< endl;
            cout << request << endl;
#endif
            env.parse_request(request);

            int pid = fork();
            if(pid == 0){
              string temp, PATH;
              setenv("REQUEST_METHOD", env.get_env_variables(0).data(), 1);
              setenv("REQUEST_URI", env.get_env_variables(1).data(), 1);
              setenv("QUERY_STRING", env.get_env_variables(2).data(), 1);
              setenv("SERVER_PROTOCOL", env.get_env_variables(3).data(), 1);
              setenv("HTTP_HOST", env.get_env_variables(4).data(), 1);
              setenv("SERVER_ADDR", socket_.local_endpoint().address().to_string().c_str(), 1);
              setenv("SERVER_PORT", env.get_env_variables(6).data(), 1);
              setenv("REMOTE_ADDR", socket_.remote_endpoint().address().to_string().c_str(), 1);
              setenv("REMOTE_PORT", to_string(htons(socket_.remote_endpoint().port())).c_str(), 1);
              connection();
              temp = env.get_env_variables(1) + "?";
              PATH = "." + temp.substr(0, temp.find('?', 0));
              if(execv(PATH.data(), request_argv) == -1){
                cerr<<"Fail to exec cgi"<<endl;
              }
              exit(0);
            }else if(pid < 0){
              while(waitpid(-1, NULL , WNOHANG) > 0){}
            }else{
              socket_.close();
              waitpid(pid, NULL, WNOHANG);
            }
          }
        });
  }

  void do_write(std::size_t length)
  {
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(data_, length),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            do_read();
          }
        });
  }

  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
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
            std::make_shared<session>(std::move(socket))->start();
          }

          do_accept();
        });
  }

  tcp::acceptor acceptor_;
};

string enviro::get_env_variables(int index){
    return this->env_variables[index];
}

void enviro::parse_request(string request){
    request[request.length()] = '\0';

    int index1 = request.find('\n', 0);
    string first_line = request.substr(0, index1-1);
    
    int start = 0;
    int end = first_line.find(' ', 0);
    env_variables[0] = first_line.substr(start, end-start);
#ifdef TEST
    cout << "REQUEST_METHOD: " << env_variables[0] << endl;
#endif
    start = end + 1;
    switch (end = first_line.find('?', start))
    {
    case -1:
      end = first_line.find(' ', start);
      env_variables[1] = first_line.substr(start, end-start);
      env_variables[2] = "";
      break;
    default:
      end = first_line.find('?', start);
      env_variables[1] = first_line.substr(start, end-start);
      start = end + 1;
      end = first_line.find(' ', start);
      env_variables[2] = first_line.substr(start, end-start);
      env_variables[1] = env_variables[1] + "?" + env_variables[2];
      break;
    }
#ifdef TEST
    cout << "REQUEST_URI: " << env_variables[1] << endl;
    cout << "QUERY_STRING: " << env_variables[2] << endl;
#endif
    int index2 = request.find('\n', index1+1);
    string second_line = request.substr(index1+1, index2-index1-2);

    start = end + 1;
    env_variables[3] = first_line.substr(start);
#ifdef TEST
    cout << "SERVER_PROTOCOL: " << env_variables[3] << endl;
#endif
    second_line = second_line.substr(second_line.find(' ', 0)+1);
    env_variables[4] = second_line;
#ifdef TEST    
    cout << "HTTP_HOST: " << env_variables[4] << endl;
#endif    
    env_variables[5] = second_line.substr(0, second_line.find(':', 0));
#ifdef TEST    
    cout << "SERVER_ADDR: " << env_variables[5] << endl;
#endif    
    env_variables[6] = second_line.substr(second_line.find(':', 0)+1);
#ifdef TEST    
    cout << "SERVER_PORT: " << env_variables[6] << endl;
#endif
}

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }
    
    request_argv = argv;

    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}