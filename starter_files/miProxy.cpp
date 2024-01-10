#include <arpa/inet.h>
#include <memory.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>

using std::map;
using std::vector;
using std::string;
using std::cerr;
using std::cout;
using std::endl;
using std::ofstream;
using std::to_string;
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
const int SERVER_LISTEN_PORT = 80;
const int BODY_SIZE = 400000;
const int SEND_LIMIT = 4096;
const int BUFFER_SIZE = 4096;
namespace miniProxy{
  const int HTTP_BUFF_SIZE = 4096;
// Different types of chunk
typedef enum : uint8_t {
  T_GET = (1 << 0),      // GET Request
  T_RESPOND = (1 << 1),  // Non-Get HTTP Message
  T_END = (1 << 2)       // End Message
} HTTPType;

typedef enum : uint8_t{
  T_F4M = (1 << 0),      // GET f4m Request
  T_CHUNK = (1 << 1),    // GET data chunk Request
  T_OTHERS = (1 << 2),   // Get others request
  T_NONE = (1<<3)        // Not a Get Message
}GetRequestType;

class HTTPMessage {
 public:
 // Construct a HTTPMessage from a given socket. 
  static HTTPMessage fromSocket(int socket);
 // Check the type of the HTTP Message:
  bool is_get_message();
  bool is_respond_message();
  bool is_end_message();
  bool is_chunk_request();
  bool is_f4m_request();
 // Get the size of the HTTP Message Body. Return 0 if the HTTP type is GET
  int get_body_length();
 // Get the size of the HTTP Message header.
  int get_header_length();
  // Get the size of the whole message
  int size();
  // Get he whole header of the HTTP Message
  char* get_message_header();
  
  char* get_buffer();
  
  int get_buffer_length();
  // Send the whole http message to the socket_dst while keep reading from socket_src.
  void finish_sending(int socket_src,int socket_dst);

  // Change the name of the URI
  const void set_uri_name (const char* new_name);
  
  // Change the bitrate of the message
  string change_bitrate(int bitrate);

 private:
  // Constructor
  HTTPMessage(char* ,char*,HTTPType,GetRequestType,int header_length,int body_length,int);

  // Fields
  char* http_header; // Header content
  char* buffer;  // Some body contents(if any)
  HTTPType http_type;
  GetRequestType request_type;
  int header_length; // Length of HTTP header
  int body_length;   // Length of HTTP Body
  int buffer_length;
};
class RecordRow{
public:
    RecordRow();
    RecordRow(string ip_addr);
    // Update the bitrate for a given throughput
    void update_bitrate(double throughput,double alpha);

    void set_bitrates_from_socket(int socket);



    double cur_bitrate;
    double cur_throughput;
    vector<double> bitrates;
    string ip;
};

class SocketRecord{
public:
    // Constructor
    SocketRecord();

    // Add the new mapping relationship. Return false if the key already exists
    bool set_bitrates_from_socket(int socket,int record_owner);

    // Remove a record from the records
    void remove_record(int socket);

    // Get a bitrate for a given socket
    double get_bitrate(int socket);

    // Get the cur_throughput of the socket
    double get_throughput(int socket);

    // Set the cur_throughput of the socket
    void set_throughput(int socket,double new_throughput,double alpha);

    void add_record(int socket,string addr);

    string get_ip(int socket);
private:
    // Use a Hashtable to record the mapping : <socket>-><RecordRow>
    map<int,RecordRow> records;
    
};
class miProxy {
 public:
  // Constructor
  miProxy(int listen_port, char* server_addr, float alpha, char* log_addr)
      : listen_port(listen_port),
        alpha(alpha),
        records(),
        server_addr(server_addr) {
    if (init(log_addr, server_addr) == false) {
      cerr << "Failed to initialize the miProxy\n";
      exit(-1);
    }
  }

  // Destructor. Release resources
  ~miProxy() {
    if (log_file.is_open()) {
      flush(log_file);
      log_file.close();
    }
  }

  // Major operation unit. Start functioning.
  void start() {
    // Start up socket select
    fd_set listen_pool, read_fds;

    FD_ZERO(&listen_pool);
    FD_SET(socket_proxy, &listen_pool);

    int max_socket_fd = socket_proxy;
    // cout << "[INFO] Start serving\n";
    while (true) {
      read_fds = listen_pool;
      //log_file << "[INFO] Waiting...\n";
      //flush(log_file);
      // Wait for connection on the socket using select
      if (select(FD_SETSIZE, &read_fds, NULL, NULL, NULL) == -1) {
        log_file << "[Error] Select broken down\n";
        flush(log_file);
        cerr << "Error in select() function. existing...\n"
             << strerror(errno) << endl;
        exit(-1);
      }
      //log_file << "[INFO] Select functioning well\n";
      //flush(log_file);
      //  If there's a connection request from the client, connect and start
      //  receiving message
      for (int a_socket = 0; a_socket <= max_socket_fd; a_socket++) {
        if (FD_ISSET(a_socket, &read_fds)) {
          if (a_socket == socket_proxy) {
            //log_file << "[INFO] New connection request, accepting\n";
            //  New connection request, establish new connection
            accept_new_connection(listen_pool, max_socket_fd);
          } else {
            //log_file << "[INFO] Information Request, accepting\n";
            //  Receive a HTTP Message from the client
            //  Handle the message accordingly
            HTTPMessage message = HTTPMessage::fromSocket(a_socket);
            // printf("[INFO] Received Message:
            // %s\n",message.get_message_header());//PRINTF

            // Handle Accordingly
            if (message.is_chunk_request()) {
              //log_file << "[INFO] Chunk Request\n";
              double bitrate = records.get_bitrate(a_socket);
              string chunk_name = message.change_bitrate(bitrate);
              double duration = 0;
              // Wait the message from server and promt the message to the
              message.finish_sending(a_socket, socket_server);
              double tp = recv_and_prompt(socket_server, a_socket, duration);
              //log_file << "[INFO] throughput: " << tp << endl;
              records.set_throughput(a_socket, tp, alpha);
              if (!log_file.is_open()) {
                log_file << "[Error] Log File is closed!\n";
                exit(-1);
              }
              log_file << records.get_ip(a_socket) << " " << chunk_name << " "
                       << server_addr << " " << duration << " " << tp << " "
                       << records.get_throughput(a_socket) << " " << bitrate
                       << "\n";
              std::flush(log_file);
            } else if (message.is_f4m_request()) {
              //log_file << "[INFO] f4m Request\n";
              //  Keep the returned f4m file and return a XXX_nolist.f4m
              //  containing no available bitrates
              message.finish_sending(a_socket, socket_server);
              records.set_bitrates_from_socket(socket_server, a_socket);
              // Change the URI of the message, return the changed response
              message.set_uri_name("/vod/big_buck_bunny_nolist.f4m");
              message.finish_sending(a_socket, socket_server);
              deliever_message(a_socket);
            } else if (message.is_end_message()) {
              //log_file << "[INFO] End Request\n";
              //  Close the socket and remove it from the listen_pool
              FD_CLR(a_socket, &listen_pool);
              close(a_socket);
              if (a_socket == max_socket_fd) {
                while (!FD_ISSET(max_socket_fd, &listen_pool)) {
                  max_socket_fd -= 1;
                }
              }
              records.remove_record(a_socket);
              //log_file << "[INFO] Connection closed.\n";
            } else {
              // Other Messages: Directly Pass to the server
              //log_file << "[INFO] Other Request\n";
              message.finish_sending(a_socket, socket_server);
              // Wait the message from server and promt the message to the
              deliever_message(a_socket);
            }

          }  // End of if-client-send-message
        }
      }  // End of socket find loop
      // cout << "[INFO] Wating for requests...\n";
    }  // ENd of select loop
  }    // end of function

  // Helper functions:
 private:
  /**
   * @brief Initialization.
   * Including construct the connection to the server, start the listening port
   * and open log file
   *
   * @return true if all initialization completed
   * @return false otherwise
   */
  bool init(char* log_addr, char* server_addr) {
    //****************** Create Log file ******************
    log_file.open(log_addr, std::ios::trunc);
    if (!log_file.is_open()) {
      cerr << "Error opening log file\n";
      return false;
    }

    //****************** Connect to the server ******************
    struct addrinfo hints;
    struct addrinfo* host_addr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // Be able to bind()
    hints.ai_socktype = SOCK_STREAM;  // Socket transport pattern
    hints.ai_flags = AI_PASSIVE;      // IPv4 and IPv6 are both ok
    int status = getaddrinfo(server_addr, to_string(SERVER_LISTEN_PORT).c_str(),
                             &hints, &host_addr);
    if (status != 0) {
      cerr << "Failed to read the address of the server." << endl;
      return false;
    }

    socket_server = socket(host_addr->ai_family, host_addr->ai_socktype,
                           host_addr->ai_protocol);
    if (socket_server == -1) {
      log_file << "[Error] Failed to initialize the socket to server.\n";
      freeaddrinfo(host_addr);
      return false;
    }

    // Set the socket options for reuse
    int optval = 1;
    if (setsockopt(socket_server, SOL_SOCKET, SO_REUSEADDR, &optval,
                   sizeof(optval)) == -1) {
      cerr << "Failed to set socket options for reuse.\n";
      freeaddrinfo(host_addr);
      close(socket_server);
      return false;
    }

    // Try to connect server for 5 times
    for (int numOfTril = 0; numOfTril < 5; numOfTril++) {
      if (connect(socket_server, host_addr->ai_addr, host_addr->ai_addrlen) ==
          0)
        break;
      if (numOfTril == 4) {
        cout << "Failed to connect the server after 5 trials\n";
        freeaddrinfo(host_addr);
        return false;
      }
    }
    // cout << "[INFO] Connection established to server\n";
    //****************** Start listening from client ******************

    struct addrinfo hints_as_server;
    struct addrinfo* serving_info;
    memset(&hints_as_server, 0, sizeof(hints_as_server));
    hints_as_server.ai_family = AF_UNSPEC;      // Be able to bind()
    hints_as_server.ai_socktype = SOCK_STREAM;  // Socket transport pattern
    hints_as_server.ai_flags = AI_PASSIVE;      // IPv4 and IPv6 are both ok
    hints_as_server.ai_protocol = IPPROTO_TCP;  // Protocol type

    int status_server = getaddrinfo(INADDR_ANY, to_string(listen_port).c_str(),
                                    &hints_as_server, &serving_info);
    if (status_server != 0) {
      cout << "Failed to initialize listening port" << endl;
      return false;
    }
    // Create socket and bind to the address
    socket_proxy = socket(serving_info->ai_family, serving_info->ai_socktype,
                          serving_info->ai_protocol);
    if (socket_proxy == -1) {
      cout << "Failed to initialize the socket for proxy\n";
      freeaddrinfo(serving_info);
      return -1;
    }

    int allow_multi_binding = 1;
    setsockopt(socket_proxy, SOL_SOCKET, SO_REUSEADDR, &allow_multi_binding,
               sizeof(int));
    // Binding:
    if (bind(socket_proxy, serving_info->ai_addr, serving_info->ai_addrlen) <
        0) {
      cout << "Failed to bind the proxy socket to the address\n";
      close(socket_proxy);
      freeaddrinfo(serving_info);
      return false;
    };

    // Listening:
    if (listen(socket_proxy, listen_port) == -1) {
      cout << "Failed to listen\n";
      close(socket_proxy);
      freeaddrinfo(serving_info);
      return false;
    }

    return true;
  }

  void deliever_message(int socket_fd) {
    HTTPMessage message = HTTPMessage::fromSocket(socket_server);
    //log_file << "[INFO] Received message from the server.\n";
    //  printf("Header: \n%s\nBody length: %d\n", message.get_message_header(),
    //        message.get_body_length());
    message.finish_sending(socket_server, socket_fd);
    //log_file << "[INFO] Finish sending.\n";
  }

  ssize_t find_content_length(const std::string &header) {
    std::string content_length_prefix = "Content-Length: ";
    size_t pos = header.find(content_length_prefix);
    if (pos == std::string::npos) {
        return -1;
    }

    size_t start_pos = pos + content_length_prefix.length();
    size_t end_pos = header.find("\r\n", start_pos);
    if (end_pos == std::string::npos) {
        return -1;
    }

    return std::stoi(header.substr(start_pos, end_pos - start_pos));
  }

  double recv_and_prompt_2(int socket_src, int socket_dst, double& timer) {
    auto time_start = high_resolution_clock::now();
    int total_bytes_received = p2p(socket_src,socket_dst);
    auto time_end = high_resolution_clock::now();
    std::chrono::duration<double> duration_second;
    duration_second = duration_cast<duration<double>>(time_end - time_start);
    timer = duration_second.count();
    double bitrate = 8 * (total_bytes_received) / timer / 1000;
    return bitrate;
  }

  double recv_and_prompt(int socket_src, int socket_dst, double& timer) {
    auto time_start = high_resolution_clock::now();
    HTTPMessage message = HTTPMessage::fromSocket(socket_src);
    char buffer[BODY_SIZE];
    // Receive the rest of the content:
    int remaining_size =
        message.get_body_length() - message.get_buffer_length();

    int total_bytes_read = 0, bytes_current = 0;
    do {
      bytes_current = recv(socket_src, buffer + total_bytes_read, BODY_SIZE, 0);
      if (bytes_current < 0) {
        log_file << "[Error] Failed to read from the server,Ending\n";
        exit(-1);
      } else if (bytes_current == 0) {
        log_file << "[Error] Server disconnected. Ending\n";
        exit(-1);
      }
      total_bytes_read += bytes_current;
    } while (total_bytes_read < remaining_size);
    auto time_end = high_resolution_clock::now();

    // Send the header
    int bytes_sent = 0;
    int bytes_cur = 0;

    do {
      bytes_cur = send(socket_dst, message.get_message_header()+bytes_sent,
                       message.get_header_length(), 0);
      if (bytes_cur <= 0) {
        log_file << "[Error] Failed to send the header to " << socket_dst
                 << std::endl;
        exit(-1);
      }
      bytes_sent += bytes_cur;
    } while (bytes_sent < message.get_header_length());

    // Send the body (if any) already in the buffer
    if (message.get_buffer_length() > 0) {
      bytes_sent = 0;
      bytes_cur = 0;
      do {
        bytes_cur = send(socket_dst, message.get_buffer()+bytes_sent,
                         message.get_buffer_length(), 0);
        if (bytes_cur <= 0) {
          log_file << "[Error] Failed to send the header to " << socket_dst
                   << std::endl;
          exit(-1);
        }
        bytes_sent += bytes_cur;
      } while (bytes_sent < message.get_buffer_length());
    }

    // Read and send the remaining body (if any)
    bytes_sent = 0;
    bytes_cur = 0;

    do {
      bytes_cur = send(socket_dst, buffer + bytes_sent, remaining_size, 0);
      if (bytes_cur <= 0) {
        log_file << "[Error] Failed to send the header to " << socket_dst
                 << std::endl;
        exit(-1);
      }
      bytes_sent += bytes_cur;
    } while (bytes_sent < remaining_size);

    std::chrono::duration<double> duration_second;
    duration_second = duration_cast<duration<double>>(time_end - time_start);
    //log_file << "[INFO] All message sent\n";
    timer = duration_second.count();
    // Kbps
    double bitrate = 8 *
                     (message.get_body_length() + message.get_header_length()) /
                     duration_second.count() / 1000;
    return bitrate;
  }

  int p2p(int socket_src,int socket_dst){
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received, bytes_sent;
    ssize_t total_bytes_received = 0;

    // Read HTTP header
    std::string header;
    while (header.find("\r\n\r\n") == std::string::npos) {
      bytes_received = recv(socket_src, buffer, BUFFER_SIZE-1, 0);
      if (bytes_received <= 0) {
        std::cerr << "Error receiving data from socket_src: " << strerror(errno)
                  << std::endl;
        return -1;
      }
      buffer[bytes_received] = '\0';
      header += buffer;
      total_bytes_received+=bytes_received;
    }
    
    // Find content length
    ssize_t content_length = find_content_length(header);
    if (content_length < 0) {
      std::cerr << "Content-Length not found in the HTTP header" << std::endl;
      return -1;
    }

    int bytes_overread = total_bytes_received-header.find("\r\n\r\n")-4;
    // Calculate the remaining bytes to read
    ssize_t remaining_bytes =content_length - bytes_overread;

    // Send the header to socket_dst
    deliver(socket_dst,header.c_str(),header.length());
    

    // Read and send the body
    while (remaining_bytes > 0) {
      bytes_received =
          recv(socket_src, buffer, BUFFER_SIZE, 0);
      if (bytes_received <= 0) {
        std::cerr << "Error receiving data from socket_src: " << strerror(errno)
                  << std::endl;
        exit(-1);
      }

      total_bytes_received += bytes_received;
      remaining_bytes -= bytes_received;

      deliver(socket_dst,buffer,bytes_received);
    }
    return total_bytes_received;
  }

  /**
   * @brief Establish a new connection in miProxy. This function is used when
   * a connect() request is made towards the socket_proxy.
   *
   * @param listen_pool  the pool that contains all sockets that are established
   * @param max_socket_fd the current maximum fd of socket
   * @return true if new connection established
   * @return false otherwise
   */
  bool accept_new_connection(fd_set& listen_pool, int& max_socket_fd) {
    // Handle new connections from the client
    struct sockaddr_storage client_addr;
    socklen_t address_size = sizeof(client_addr);
    int client_fd =
        accept(socket_proxy, (struct sockaddr*)&client_addr, &address_size);
    if (client_fd == -1) {
      // log_file << "[ERROR] Failed to accept the connection request\n";
      // flush(log_file);
      exit(-1);
    } else {
      if (client_fd > max_socket_fd) max_socket_fd = client_fd;
      char client_address[INET_ADDRSTRLEN];
      // Read the ip address of the client:
      struct sockaddr_in* s = (struct sockaddr_in*)&client_addr;
      inet_ntop(AF_INET, &s->sin_addr, client_address, sizeof client_address);
      records.add_record(client_fd, client_address);
      FD_SET(client_fd, &listen_pool);  // Add new fd into listen pool
      // log_file << "[INFO] New connection established! " << client_fd << " "
      //          << client_address << std::endl;
      return true;
    }
  }

  const void deliver(int socket_fd,const char* content,int size){
    int bytes_sent=0,total_bytes_sent=0;
    do{
      bytes_sent = send(socket_fd,content+total_bytes_sent,size,0);
      if(bytes_sent<=0){
        cerr<<"[Error] Failed to deliver message to socket "<<socket_fd<<". "<<strerror(errno)<<endl;
        exit(-1);
      }
      total_bytes_sent+=bytes_sent;
    }while(total_bytes_sent<size);
  }


  // Fields:
 private:
  int listen_port;       // The serving port for the proxy
  int socket_server;     // Socket used for connecting to the web server
  int socket_proxy;      // Socket used for listening
  float alpha;           // For EWMA coefficient
  ofstream log_file;     // Address to the log file
  SocketRecord records;  // Records of the socket information
  char* server_addr;
};

int main(int argc, char* argv[]) {
  if (argc != 6) {
    cout << "Wrong parameter for miProxy. Usage: ./miProxy --nodns "
            "<listen-port> <www-ip> <alpha> <log>\n";
    exit(-1);
  }

  int listen_port = atoi(argv[2]);
  char* server_ip = argv[3];
  float alpha = atof(argv[4]);
  char* log_addr = argv[5];

  // cout << "[INFO] Listen port: " << listen_port << "\n"
  //      << "Serving ip:" << server_ip << "\n"
  //      << "Alpha: " << alpha << "\n"
  miProxy proxy(listen_port, server_ip, alpha, log_addr);

  proxy.start();
}



// Constructor
HTTPMessage::HTTPMessage(char *header, char *buffer, HTTPType type,
                         GetRequestType req_type, int h_length, int b_length,
                         int buffer_length)
    : http_header(header),
      buffer(buffer),
      http_type(type),
      request_type(req_type),
      header_length(h_length),
      body_length(b_length),
      buffer_length(buffer_length) {}

HTTPMessage HTTPMessage::fromSocket(int socket) {
  char buffer[HTTP_BUFF_SIZE];
  int received;
  string received_data;
  string header_end = "\r\n\r\n";
  size_t header_end_pos;

  // Keep reading data from the socket until the end of the header is found
  while ((header_end_pos = received_data.find(header_end)) == string::npos) {
    received = recv(socket, buffer, sizeof(buffer) - 1, 0);

    if (received < 0) {
      cout << "[Error] Failed to read from the socket " << socket
           << " ending...\n";
      exit(-1);
    } else if (received == 0) {
      cout << "[Error] Browser closed the connection.\n";
      return (HTTPMessage(nullptr, nullptr, T_END, T_NONE, 0, 0, 0));
    }

    buffer[received] = '\0';
    received_data.append(buffer, received);
  }

  int header_length = header_end_pos + header_end.length();

  char *http_header = new char[header_length + 1];
  strncpy(http_header, received_data.c_str(), header_length);
  http_header[header_length] = '\0';

  // Find the Content-Length header
  string content_length_str = "Content-Length: ";
  size_t content_length_pos = received_data.find(content_length_str);
  int body_length = 0;

  if (content_length_pos != string::npos) {
    content_length_pos += content_length_str.length();
    body_length = std::stoi(received_data.substr(content_length_pos));
  }

  // Find over-read body content
  int body_received = received_data.length() - header_length;
  char *body_buffer = nullptr;

  if (body_received > 0) {
    body_buffer = new char[body_received];
    memcpy(body_buffer, received_data.c_str() + header_length, body_received);
  }

  HTTPType http_type = T_END;
  GetRequestType request_type = T_NONE;

  if (strncmp(http_header, "GET", 3) == 0) {
    http_type = T_GET;

    // Detect request type
    char *uri_start = strstr(http_header, "/") + 1;
    char *uri_end = strstr(uri_start, " ");
    string uri(uri_start, uri_end - uri_start);

    if (uri.find(".f4m") != string::npos) {
      request_type = T_F4M;
    } else if (uri.find("vod") != string::npos) {
      request_type = T_CHUNK;
    } else {
      request_type = T_OTHERS;
    }
  } else {
    http_type = T_RESPOND;
  }
  return HTTPMessage(http_header, body_buffer, http_type, request_type,
                     header_length, body_length, body_received);
}

// Check the type of the HTTP Message:
bool HTTPMessage::is_get_message() { return http_type == T_GET; }

bool HTTPMessage::is_respond_message() { return http_type == T_RESPOND; }

bool HTTPMessage::is_end_message() { return http_type == T_END; }

bool HTTPMessage::is_chunk_request() { return request_type == T_CHUNK; };

bool HTTPMessage::is_f4m_request() { return request_type == T_F4M; };

// Get the size of the HTTP Message Body. Return 0 if the HTTP type is GET
int HTTPMessage::get_body_length() {
  if (http_type == T_GET) {
    return 0;
  } else {
    return body_length;
  }
}

// Get the size of the HTTP Message header.
int HTTPMessage::get_header_length() { return header_length; }

// Get the size of the whole message
int HTTPMessage::size() { return body_length + header_length; }

// Get the whole content of the message.
char *HTTPMessage::get_message_header() { return http_header; }

void HTTPMessage::finish_sending(int socket_src, int socket_dst) {
  // Send the header
  int bytes_sent = 0;
  int bytes_cur = 0;
  do {
    bytes_cur = send(socket_dst, http_header+bytes_sent, header_length, 0);
    if (bytes_cur <= 0) {
      cout << "[Error] Failed to send the header to " << socket_dst
           << std::endl;
      exit(-1);
    }
    bytes_sent += bytes_cur;
  } while (bytes_sent < header_length);

  // Send the body (if any) already in the buffer
  if (buffer_length > 0) {
    bytes_sent = 0;
    bytes_cur = 0;
    do {
      bytes_cur = send(socket_dst, buffer+bytes_sent, buffer_length, 0);
      if (bytes_cur <= 0) {
        cout << "[Error] Failed to send the header to " << socket_dst
             << std::endl;
        exit(-1);
      }
      bytes_sent += bytes_cur;
    } while (bytes_sent < buffer_length);
  }

  // Read and send the remaining body (if any)
  char transfer_buffer[HTTP_BUFF_SIZE];
  int remaining_body_length = body_length - buffer_length;
  
  while (remaining_body_length > 0) {
    bytes_sent = 0;
    bytes_cur = 0;
    int received = recv(socket_src, transfer_buffer, HTTP_BUFF_SIZE, 0);
    if (received <= 0) {
      cout << "[Error] Failed to read from the server. Ending...\n";
      exit(-1);
    }

    do{
      bytes_cur=send(socket_dst, transfer_buffer+bytes_sent, received, 0);
      if (received <= 0) {
        cerr << "[Error] Failed to read from the server. Ending...\n"<<strerror(errno)<<endl;
        exit(-1);
      }
      bytes_sent+=bytes_cur;
    }while(bytes_sent<received);
    remaining_body_length -= received;
  }
  }

const void HTTPMessage::set_uri_name(const char *new_name) {
  if (http_type != T_GET) {
    return;
  }
  char *uri_start = strstr(http_header, "/");
  if (uri_start == nullptr) {
    return;
  }
  uri_start++;

  char *uri_end = strstr(uri_start, " ");
  if (uri_end == nullptr) {
    return;
  }

  string header_before_uri(http_header, uri_start - http_header);
  string header_after_uri(uri_end);

  string new_header = header_before_uri + string(new_name) + header_after_uri;
  delete[] http_header;
  header_length = new_header.length();

  http_header = new char[header_length + 1];
  strncpy(http_header, new_header.c_str(), header_length);
  http_header[header_length] = '\0';
}

char *HTTPMessage::get_buffer() { return buffer; }

// Change the bitrate of the message
string HTTPMessage::change_bitrate(int bitrate) {
  if (!is_get_message() || !is_chunk_request()) {
    cout<<"[Error] Wrong message type for change_bitrate() function.Ending\n";
    exit(-1);
  }

  char *name_start = strstr(http_header, "S");
  if (name_start == nullptr) {
    cout<<"[Error] Wrong chunk type for change_bitrate() function.Ending\n";
    exit(-1);
  }

  char *name_end = strstr(name_start+1, " ");
  

  // Extract the original URI path and query string. "/vod/XXSegXX-FragXX"
  string name(name_start, name_end - name_start);
  string new_uri="/vod/" + std::to_string(bitrate) + name;
  //cout<<"[INFO] Selected bitrate: "<<bitrate<<std::endl;
  //cout<<"[INFO] Extracted URI: "<<new_uri<<std::endl;

  set_uri_name(new_uri.c_str());
  return std::to_string(bitrate)+name;
}

int HTTPMessage::get_buffer_length() { return buffer_length; }
// RecordRow Implementation
RecordRow::RecordRow(string ip_addr)
    : cur_bitrate(cur_bitrate), cur_throughput(0),bitrates(),ip(ip_addr){}

RecordRow::RecordRow() {}

void RecordRow::update_bitrate(double throughput,double alpha) {
  //cout<<"[INFO] Previous tp: "<<cur_throughput<<std::endl;
  cur_throughput = alpha * throughput + (1-alpha)*cur_throughput;
  //cout<<"[INFO] After tp: "<<cur_throughput<<std::endl;
  double estimated_bitrate = cur_throughput/1.5;
  for(auto& bitrate:bitrates){
    if(bitrate<=estimated_bitrate) cur_bitrate=bitrate;
  }
}

void RecordRow::set_bitrates_from_socket(int socket_fd){
  // Read HTTP response from the socket
  const int buffer_size = 4096;
  char buffer[buffer_size];
  string http_response = "";
  int bytes_read = 0;
  HTTPMessage message = HTTPMessage::fromSocket(socket_fd);
  //printf("[INFO] Get the header already:\n%s\n", message.get_message_header());

  int body_size = message.get_body_length();
  http_response += string(message.get_buffer(), message.get_buffer_length());
  bytes_read += message.get_buffer_length();
  int cur_read = 0;

  // Read the XML content:
  while (bytes_read < body_size) {
    cur_read = recv(socket_fd, buffer, buffer_size, 0);
    if (cur_read <= 0) {
       std::cerr << "[Error] Failed to receive the XML file from the server. Ending\n"<<strerror(errno)<<std::endl;
      exit(-1);
    }
    bytes_read += cur_read;
    http_response += string(buffer, cur_read);
  }
  //cout << "[INFO] Read all XML body:\n" << http_response << std::endl;
  // Parse XML content for available bitrates
  std::vector<double> bitrates;
  int pos = 0;
  while ((pos = http_response.find("<media", pos)) != string::npos) {
    int bitrateStart = http_response.find("bitrate=\"", pos) + 9;
    int bitrateEnd = http_response.find("\"", bitrateStart);
    if (bitrateStart != string::npos && bitrateEnd != string::npos) {
      string bitrateStr =
          http_response.substr(bitrateStart, bitrateEnd - bitrateStart);
      double bitrate = std::stod(bitrateStr);
      bitrates.push_back(bitrate);
    }
    pos = bitrateEnd;
  }
  //cout << "[INFO] Successfully received bitrates\n";
  // for (auto& bitrate : bitrates) {
  //   cout << "[INFO][AvailableRate] " << bitrate << std::endl;
  // }
  cur_bitrate=bitrates[0];
  bitrates.swap(this->bitrates);
}

// SocketRecord Implementation
SocketRecord::SocketRecord() {}

bool SocketRecord::set_bitrates_from_socket(int socket_src, int record_owner) {
  if (records.find(record_owner) == records.end()) {
    std::cerr<<"[Error] Socket "<<socket<<" does not exists.Ending\n"<<strerror(errno)<<std::endl;;
    exit(-1);
  }
  records[record_owner].set_bitrates_from_socket(socket_src);
  
  return true;
}

void SocketRecord::remove_record(int socket) { records.erase(socket); }

double SocketRecord::get_bitrate(int socket) {
  if (records.find(socket) == records.end()) {
    std::cerr << "[Error] Can not find the socket " << socket
         << " in the records. Ending\n"<<strerror(errno)<<std::endl;;
    exit(-1);
  }
  return records[socket].cur_bitrate;
}

double SocketRecord::get_throughput(int socket) {
  if (records.find(socket) == records.end()) {
    std::cerr<<"[Error] Socket "<<socket<<" does not exists.Ending\n"<<strerror(errno)<<std::endl;;
    exit(-1);
  }
  return records[socket].cur_throughput;
}

void SocketRecord::set_throughput(int socket, double new_throughput,double alpha) {
  if (records.find(socket) == records.end()) {
    std::cerr<<"[Error] Socket "<<socket<<" does not exists.Ending\n"<<strerror(errno)<<std::endl;;
    exit(-1);
  }
  records[socket].update_bitrate(new_throughput,alpha);
}


void SocketRecord::add_record(int socket,string addr){
  if (records.find(socket) != records.end()) {
    std::cerr<<"[Error] Socket "<<socket<<" exists already.Ending\n"<<strerror(errno)<<std::endl;;
    exit(-1);
  }
  records[socket]=RecordRow(addr);
  //cout << "[INFO] Added socket " << socket << " to the records\n";
}


string SocketRecord::get_ip(int socket){
  if (records.find(socket) == records.end()) {
    std::cerr<<"[Error] Socket "<<socket<<" does not exists.Ending\n"<<strerror(errno)<<std::endl;;
    exit(-1);
  }
  return records[socket].ip;
}
}
