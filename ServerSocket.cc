// Kyle Boo kb0531@cs.washington.edu Copyright Kyle Boo 2024


/*
 * Copyright ©2024 Hannah C. Tang.  All rights reserved.  Permission is
 * hereby granted to students registered for University of Washington
 * CSE 333 for use solely during Spring Quarter 2024 for purposes of
 * the course.  No other use, copying, distribution, or modification
 * is permitted without prior written consent. Copyrights for
 * third-party components of this work must be honored.  Instructors
 * interested in reusing these course materials should contact the
 * author.
 */

#include <stdio.h>       // for snprintf()
#include <unistd.h>      // for close(), fcntl()
#include <sys/types.h>   // for socket(), getaddrinfo(), etc.
#include <sys/socket.h>  // for socket(), getaddrinfo(), etc.
#include <arpa/inet.h>   // for inet_ntop()
#include <netdb.h>       // for getaddrinfo()
#include <errno.h>       // for errno, used by strerror()
#include <string.h>      // for memset, strerror()
#include <iostream>      // for std::cerr, etc.

#include "./ServerSocket.h"

extern "C" {
  #include "libhw1/CSE333.h"
}

namespace hw4 {

ServerSocket::ServerSocket(uint16_t port) {
  port_ = port;
  listen_sock_fd_ = -1;
}

ServerSocket::~ServerSocket() {
  // Close the listening socket if it's not zero.  The rest of this
  // class will make sure to zero out the socket if it is closed
  // elsewhere.
  if (listen_sock_fd_ != -1)
    close(listen_sock_fd_);
  listen_sock_fd_ = -1;
}

bool ServerSocket::BindAndListen(int ai_family, int* const listen_fd) {
  // Use "getaddrinfo," "socket," "bind," and "listen" to
  // create a listening socket on port port_.  Return the
  // listening socket through the output parameter "listen_fd"
  // and set the ServerSocket data member "listen_sock_fd_"

  // STEP 1:
  struct addrinfo *rp;
  char portnum[6];

  std::snprintf(portnum, sizeof(portnum), "%d", port_);

  // Populate the "hints" addrinfo structure for getaddrinfo().
  // ("man addrinfo")
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = ai_family;       // IPv6 (also handles IPv4 clients)
  hints.ai_socktype = SOCK_STREAM;  // stream
  hints.ai_flags = AI_PASSIVE;      // use wildcard "in6addr_any" address
  hints.ai_flags |= AI_V4MAPPED;    // use v4-mapped v6 if no v6 found
  hints.ai_protocol = IPPROTO_TCP;  // tcp protocol
  hints.ai_canonname = nullptr;
  hints.ai_addr = nullptr;
  hints.ai_next = nullptr;

  // Use portnum as the string representation of our portnumber to
  // pass in to getaddrinfo().  getaddrinfo() returns a list of
  // address structures via the output parameter "result".
  struct addrinfo *result;
  int res = getaddrinfo(nullptr, portnum, &hints, &result);

  // Did addrinfo() fail?
  if (res != 0) {
    std::cerr << "getaddrinfo() failed: " << gai_strerror(res) << std::endl;
    return false;
  }

  // Loop through the returned address structures until we are able
  // to create a socket and bind to one.  The address structures are
  // linked in a list through the "ai_next" field of result.
  int lfd = -1;
  for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
    lfd = socket(rp->ai_family,
                       rp->ai_socktype,
                       rp->ai_protocol);
    if (lfd == -1) {
      // Creating this socket failed.  So, loop to the next returned
      // result and try again.
      std::cerr << "socket() failed: " << strerror(errno) << std::endl;
      continue;
    }

    // Configure the socket; we're setting a socket "option."  In
    // particular, we set "SO_REUSEADDR", which tells the TCP stack
    // so make the port we bind to available again as soon as we
    // exit, rather than waiting for a few tens of seconds to recycle it.
    int optval = 1;
    if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval))) {
      std::cerr << "setsockopt() failed: " << strerror(errno) << std::endl;
      close(lfd);
      continue;
    }


    // Try binding the socket to the address and port number returned
    // by getaddrinfo().
    if (bind(lfd, rp->ai_addr, rp->ai_addrlen) == 0) {
      // Bind worked!  Return to the caller the address family.
      *listen_fd = lfd;
      break;
    }

    // The bind failed.  Close the socket, then loop back around and
    // try the next address/port returned by getaddrinfo().
    close(lfd);
    // lfd = -1;
  }

  // Free the structure returned by getaddrinfo().
  freeaddrinfo(result);

  // If we failed to bind, return failure.
  if (lfd <= 0)
    return false;

  // Success. Tell the OS that we want this to be a listening socket.
  if (listen(lfd, SOMAXCONN) != 0) {
    std::cerr << "Failed to mark socket as listening: "
    << strerror(errno) << std::endl;
    close(lfd);
    return false;
  }
  listen_sock_fd_ = lfd;

  // Return success
  return true;
}

bool ServerSocket::Accept(int* const accepted_fd,
                          std::string* const client_addr,
                          uint16_t* const client_port,
                          std::string* const client_dns_name,
                          std::string* const server_addr,
                          std::string* const server_dns_name) const {
  // Accept a new connection on the listening socket listen_sock_fd_.
  // (Block until a new connection arrives.)  Return the newly accepted
  // socket, as well as information about both ends of the new connection,
  // through the various output parameters.

  // STEP 2:
  struct sockaddr_storage c_addr;
  socklen_t c_addr_len = sizeof(c_addr);
  char ip_str[INET6_ADDRSTRLEN];
  char host[1024];

  int fd;
  while (1) {
    fd = accept(listen_sock_fd_,
                reinterpret_cast<struct sockaddr *>(&c_addr), &c_addr_len);
    if (fd < 0) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;
      }
      std::cerr << "Failure on accept: " << strerror(errno) << std::endl;
      return false;
    }
    break;
  }

  // Get client IP address and port
  if (c_addr.ss_family == AF_INET) {
    struct sockaddr_in *s4 = reinterpret_cast<struct sockaddr_in *>(&c_addr);
    inet_ntop(AF_INET, &s4 -> sin_addr, ip_str, sizeof(ip_str));
    *client_port = ntohs(s4 -> sin_port);
  } else {
    struct sockaddr_in6 *s6 = reinterpret_cast<struct sockaddr_in6 *>(&c_addr);
    inet_ntop(AF_INET6, &s6 -> sin6_addr, ip_str, sizeof(ip_str));
    *client_port = ntohs(s6 -> sin6_port);
  }
  *client_addr = std::string(ip_str);

  // Get client DNS name
  if (getnameinfo(reinterpret_cast<struct sockaddr *>(&c_addr),
                  c_addr_len, host, sizeof(host), nullptr, 0, 0) == 0) {
    *client_dns_name = std::string(host);
  } else {
    *client_dns_name = *client_addr;
  }

  // Get server IP address
  struct sockaddr_storage s_addr;
  socklen_t s_addr_len = sizeof(s_addr);
  if (getsockname(fd, reinterpret_cast<struct sockaddr *>(&s_addr),
                  &s_addr_len) == -1) {
      std::cerr << "Failed to get server address: "
      << strerror(errno) << std::endl;
      close(fd);
      return false;
  }

  // Get server address
  if (s_addr.ss_family == AF_INET) {
    struct sockaddr_in *s4 = reinterpret_cast
                              <struct sockaddr_in *>(&s_addr);
    inet_ntop(AF_INET, &s4 -> sin_addr, ip_str, sizeof(ip_str));
  } else {
    struct sockaddr_in6 *s6 = reinterpret_cast
                              <struct sockaddr_in6 *>(&c_addr);
    inet_ntop(AF_INET6, &s6 -> sin6_addr, ip_str, sizeof(ip_str));
  }
  *server_addr = std::string(ip_str);

  // Get server DNS name
  if (getnameinfo(reinterpret_cast<struct sockaddr *>(&s_addr),
                  s_addr_len, host, sizeof(host),
                  nullptr, 0, NI_NAMEREQD) == 0) {
    *server_dns_name = std::string(host);
  } else {
    *server_dns_name = *server_addr;
  }
  *accepted_fd = fd;
  return true;
}

}  // namespace hw4
