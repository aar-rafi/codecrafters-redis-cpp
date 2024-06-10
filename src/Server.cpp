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

const size_t k_max_msg = 4096;

enum
{
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2,
};

struct Conn
{
  int fd = -1;
  uint32_t state = 0;
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  size_t wbuf_size = 0;
  uint8_t wbuf[4 + k_max_msg];
  size_t wbuf_sent = 0;
};

static void fd_set_nb(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
  {
    std::cerr << "Failed to get flags\n";
    exit(1);
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    std::cerr << "Failed to set non-blocking" << strerror(errno) << '\n';
    exit(1);
  }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn)
{
  if (fd2conn.size() <= (size_t)conn->fd)
  {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

static int accept_new_conn(int _server_fd, std::vector<Conn *> &fd2conn)
{
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  int newsockfd = accept(_server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
  if (newsockfd < 0)
  {
    std::cerr << "Failed to accept client connection\n";
    return -1;
  }
  std::cout << "Client connected\n";

  fd_set_nb(newsockfd);

  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn)
  {
    std::cerr << "Failed to allocate memory\n";
    close(newsockfd);
    return -1;
  }
  conn->fd = newsockfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;

  conn_put(fd2conn, conn);

  return 0;
}

static void state_res(Conn *conn);
static void state_req(Conn *conn);

static bool try_one_request(Conn *conn)
{
  // // try to parse a request from the buffer
  // if (conn->rbuf_size < 4)
  // {
  //   // not enough data in the buffer. Will retry in the next iteration
  //   return false;
  // }
  // uint32_t len = 0;
  // memcpy(&len, &conn->rbuf[0], 4);
  // if (len > k_max_msg)
  // {
  //   std::cerr << "too long\n";
  //   conn->state = STATE_END;
  //   return false;
  // }
  // if (4 + len > conn->rbuf_size)
  // {
  //   // not enough data in the buffer. Will retry in the next iteration
  //   return false;
  // }

  // // got one request, do something with it
  // printf("client says: %.*s\n", len, &conn->rbuf[4]);

  // generating echoing response
  // memcpy(&conn->wbuf[0], &len, 4);
  // memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
  // conn->wbuf_size = 4 + len;

  std::string res = "+PONG\r\n";
  memcpy(&conn->wbuf[0], res.c_str(), res.length());
  conn->wbuf_size = res.length();

  // remove the request from the buffer.
  // note: frequent memmove is inefficient.
  // note: need better handling for production code.
  // size_t remain = conn->rbuf_size - 4 - len;
  // if (remain)
  // {
  //   memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  // }
  // conn->rbuf_size = remain;

  // change state
  conn->state = STATE_RES;
  state_res(conn);

  // continue the outer loop if the request was fully processed
  return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn)
{
  // // try to fill the buffer
  // assert(conn->rbuf_size < sizeof(conn->rbuf));
  // ssize_t rv = 0;
  // do
  // {
  //   size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
  //   rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  // } while (rv < 0 && errno == EINTR);

  // if (rv < 0 && errno == EAGAIN)
  // {
  //   // got EAGAIN, stop.
  //   return false;
  // }
  // if (rv < 0)
  // {
  //   std::cerr << "read() error";
  //   conn->state = STATE_END;
  //   return false;
  // }
  // if (rv == 0)
  // {
  //   if (conn->rbuf_size > 0)
  //   {
  //     std::cerr << "unexpected EOF";
  //   }
  //   else
  //   {
  //     std::cerr << "EOF";
  //   }
  //   conn->state = STATE_END;
  //   return false;
  // }

  // conn->rbuf_size += (size_t)rv;
  // assert(conn->rbuf_size <= sizeof(conn->rbuf));

  memset(&conn->rbuf[0], 0, 256);
  int n = read(conn->fd, &conn->rbuf[0], 255);
  if (n < 0)
  {
    std::cerr << "Failed to read from socket\n";
    conn->state = STATE_END;
    return false;
  }
  std::cout << "Received message: " << &conn->rbuf[0] << "\n";

  // Try to process requests one by one.
  // Why is there a loop? Please read the explanation of "pipelining".
  while (try_one_request(conn))
  {
  }
  return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn)
{
  while (try_fill_buffer(conn))
  {
  }
}

static bool try_flush_buffer(Conn *conn)
{
  //   ssize_t rv = 0;
  //   do
  //   {
  //     size_t remain = conn->wbuf_size - conn->wbuf_sent;
  //     std::cout << "remain: " << conn->wbuf_size << "\n";
  //     rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
  //     // rv = write(conn->fd, res.c_str(), res.length());
  //   } while (rv < 0 && errno == EINTR);
  //   if (rv < 0 && errno == EAGAIN)
  //   {
  //     // got EAGAIN, stop.
  //     return false;
  //   }
  //   if (rv < 0)
  //   {
  //     std::cerr << "write() error";
  //     conn->state = STATE_END;
  //     return false;
  //   }
  //   conn->wbuf_sent += (size_t)rv;
  //   assert(conn->wbuf_sent <= conn->wbuf_size);
  //   if (conn->wbuf_sent == conn->wbuf_size)
  //   {
  //     // response was fully sent, change state back
  //     conn->state = STATE_REQ;
  //     conn->wbuf_sent = 0;
  //     conn->wbuf_size = 0;
  //     return false;
  //   }
  //   // still got some data in wbuf, could try to write again
  // return true;
  //
  // std::string read_buffer = &conn->rbuf;
  //   if (strcmp(read_buffer, "*1\r\n$4\r\nPING\r\n") == 0)
  //   {
  std::string response = "+PONG\r\n";
  int n = write(conn->fd, response.c_str(), response.length());
  // int n = write(conn->fd, &conn->wbuf[0], conn->wbuf_size);
  if (n < 0)
  {
    std::cerr << "Failed to write to socket\n";
    conn->state = STATE_END;
    return false;
  }
  std::cout << "Sent response: " << response;
  conn->state = STATE_REQ;
  conn->wbuf_sent = 0;
  conn->wbuf_size = 0;
  return false;
  // }
}

static void state_res(Conn *conn)
{
  while (try_flush_buffer(conn))
  {
  }
}

static void connection_io(Conn *conn)
{
  if (conn->state == STATE_REQ)
  {
    state_req(conn);
  }
  else if (conn->state == STATE_RES)
  {
    state_res(conn);
  }
  else
  {
    assert(0); // not expected
  }
}

int main(int argc, char **argv)
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
  {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }

  int connection_backlog = 5; // queue for incoming connections
  if (listen(server_fd, connection_backlog) != 0)
  {
    std::cerr << "listen failed\n";
    return 1;
  }

  // char buffer[256];
  // while (1)
  // {
  //   memset(buffer, 0, 256);
  //   int n = read(newsockfd, buffer, 255);
  //   if (n < 0)
  //   {
  //     std::cerr << "Failed to read from socket\n";
  //     break;
  //   }
  //   std::cout << "Received message: " << buffer << "\n";

  //   if (strcmp(buffer, "*1\r\n$4\r\nPING\r\n") == 0)
  //   {
  //     std::string response = "+PONG\r\n";
  //     n = write(newsockfd, response.c_str(), response.length());
  //     if (n < 0)
  //     {
  //       std::cerr << "Failed to write to socket\n";
  //       break;
  //     }
  //     std::cout << "Sent response: " << response;
  //   }
  // }

  std::vector<Conn *> fd2conn; // using the fd's as index
  fd_set_nb(server_fd);
  std::vector<struct pollfd> poll_args;

  while (true)
  {
    poll_args.clear();
    struct pollfd pfd = {server_fd, POLLIN, 0}; // listening fd is at first position
    poll_args.push_back(pfd);

    for (Conn *conn : fd2conn)
    {
      if (!conn)
      {
        continue;
      }
      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }

    int ret = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if (ret < 0)
    {
      std::cerr << "poll failed\n";
      return 1;
    }

    size_t poll_size = poll_args.size();
    for (size_t i = 1; i < poll_size; ++i)
    {
      if (poll_args[i].revents)
      {
        Conn *conn = fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn->state == STATE_END)
        {
          fd2conn[conn->fd] = nullptr;
          (void)close(conn->fd);
          free(conn);
        }
      }
    }
  }

  if (poll_args[0].revents)
  {
    (void)accept_new_conn(server_fd, fd2conn);
  }

  return 0;
}
