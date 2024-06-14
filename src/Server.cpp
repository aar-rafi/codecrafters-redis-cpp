#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <vector>
#include <assert.h>
#include <thread>
#include "RESPparser.hpp"
#include <unordered_map>

using namespace std;

const int buff_size = 2048;
unordered_map<string, string> db;
unordered_map<string, chrono::time_point<chrono::system_clock, chrono::milliseconds>> db_ttl;
string role = "master";
bool is_slave = false;

void handle_client(int newsockfd)
{
  char buffer[buff_size];
  while (1)
  {
    memset(buffer, 0, buff_size);
    int n = read(newsockfd, buffer, buff_size - 1);
    if (n < 0)
    {
      cerr << "Failed to read from socket\n";
      return;
    }
    if (n == 0)
    {
      cout << "Connection closed\n";
      return;
    }
    cout << "Received message: " << buffer << "\n";

    RESP parsed_msg = parseResp(string(buffer));
    string command = parsed_msg.msgs[0];
    // uppercase command
    for (int i = 0; i < command.length(); i++)
    {
      command[i] = toupper(command[i]);
    }
    string response;
    if (command == "ECHO")
    {
      response = "$" + to_string(parsed_msg.msgs[1].length()) + "\r\n" + parsed_msg.msgs[1] + "\r\n";
    }
    else if (command == "SET")
    {
      db[parsed_msg.msgs[1]] = parsed_msg.msgs[2];
      if (parsed_msg.msgs.size() == 5)
      {
        int ttl = stoi(parsed_msg.msgs[4]);
        db_ttl[parsed_msg.msgs[1]] = chrono::time_point_cast<chrono::milliseconds>(chrono::system_clock::now()) + chrono::milliseconds(ttl);
      }
      response = "+OK\r\n";
    }
    else if (command == "GET")
    {
      string key = parsed_msg.msgs[1];
      if (db.find(key) == db.end())
      {
        response = "$-1\r\n";
      }
      else if (db_ttl.find(key) != db_ttl.end() && db_ttl[key] < chrono::time_point_cast<chrono::milliseconds>(chrono::system_clock::now()))
      {
        response = "$-1\r\n";
        db.erase(key);
        db_ttl.erase(key);
      }
      else
      {
        string s = db[key];
        response = "$" + to_string(s.length()) + "\r\n" + s + "\r\n";
      }
    }
    else if (command == "INFO")
    {
      // if(parsed_msg.msgs[1]=="replication")
      string s = "\r\nmaster_replid:8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb\r\nmaster_repl_offset:0";
      response = "$" + to_string(5 + role.length() + s.length()) + "\r\n" + "role:" + role + s + "\r\n";
    }
    else
    {
      response = "+PONG\r\n";
    }
    n = write(newsockfd, response.c_str(), response.length());
    if (n < 0)
    {
      cerr << "Failed to write to socket\n";
      break;
    }
    cout << "Sent response: " << response;
  }
}

int main(int argc, char **argv)
{ // Flush after every cout / cerr
  cout << unitbuf;
  cerr << unitbuf;
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  cout << "Logs from your program will appear here!\n";

  int port = 6379;
  int master_port = 6379;
  string master_ip = "127.0.0.1";
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--port") == 0)
    {
      if (i + 1 > argc)
      {
        cerr << "Port number not provided\n";
        break;
      }
      port = atoi(argv[i + 1]);
      for (i = i + 1; i < argc; i++)
      {
        if (strcmp(argv[i], "--replicaof") == 0)
        {
          if (i + 1 > argc)
          {
            cerr << "master ip not provided\n";
          }
          role = "slave";
          is_slave = true;
          string temp = argv[i + 1];
          temp = temp.substr(0, temp.find(" "));
          master_ip = (temp == "localhost") ? "127.0.0.1" : temp;
          temp = argv[i + 1];
          temp = temp.substr(temp.find(" ") + 1);
          master_port = stoi(temp);
          break;
        }
      }
      break;
    }
  }

  if (is_slave)
  {
    int master_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (master_fd < 0)
    {
      cerr << "Failed to create master socket\n";
      return 1;
    }
    struct sockaddr_in master_addr;
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(6379);
    master_addr.sin_addr.s_addr = inet_addr(master_ip.c_str());
    if (connect(master_fd, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0)
    {
      cerr << "Failed to connect to master\n";
      return 1;
    }
    string sent = "*1\r\n$4\r\nPING\r\n";
    send(master_fd, sent.c_str(), sent.length(), 0);
  }

  cout << "Server running on port " << port << "role" << role << "\n";

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    cerr << "Failed to create server socket\n";
    return 1;
  }
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
  {
    cerr << "Failed to bind to port 6379\n";
    return 1;
  }

  int connection_backlog = 5; // queue for incoming connections
  if (listen(server_fd, connection_backlog) != 0)
  {
    cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  while (true)
  {
    int newsockfd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
    cout << "Accepted connection\n";
    thread thrd(handle_client, newsockfd);
    thrd.detach();
  }

  close(server_fd);
  return 0;
}