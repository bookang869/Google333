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

#include <stdint.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <map>
#include <string>
#include <vector>

#include "./HttpRequest.h"
#include "./HttpUtils.h"
#include "./HttpConnection.h"

using std::map;
using std::string;
using std::vector;

namespace hw4 {

static const char* kHeaderEnd = "\r\n\r\n";
static const int kHeaderEndLen = 4;

bool HttpConnection::GetNextRequest(HttpRequest* const request) {
  // Use WrappedRead from HttpUtils.cc to read bytes from the files into
  // private buffer_ variable. Keep reading until:
  // 1. The connection drops
  // 2. You see a "\r\n\r\n" indicating the end of the request header.
  //
  // Hint: Try and read in a large amount of bytes each time you call
  // WrappedRead.
  //
  // After reading complete request header, use ParseRequest() to parse into
  // an HttpRequest and save to the output parameter request.
  //
  // Important note: Clients may send back-to-back requests on the same socket.
  // This means WrappedRead may also end up reading more than one request.
  // Make sure to save anything you read after "\r\n\r\n" in buffer_ for the
  // next time the caller invokes GetNextRequest()!

  // STEP 1:
  unsigned char buf[1024];
  size_t header_end;

  while ((header_end = buffer_.find(kHeaderEnd)) == string::npos) {
    int bytes_read = WrappedRead(fd_, buf, sizeof(buf));
    if (bytes_read <= 0) {  // connection closed or error occurred
      return false;
    }
    buffer_.append(reinterpret_cast<char*>(buf), bytes_read);
  }
  string request_str = buffer_.substr(0, header_end + kHeaderEndLen);
  buffer_ = buffer_.substr(header_end + kHeaderEndLen);

  *request = HttpConnection::ParseRequest(request_str);
  return true;
}

bool HttpConnection::WriteResponse(const HttpResponse& response) const {
  string str = response.GenerateResponseString();
  int res = WrappedWrite(fd_,
                         reinterpret_cast<const unsigned char*>(str.c_str()),
                         str.length());
  if (res != static_cast<int>(str.length()))
    return false;
  return true;
}

HttpRequest HttpConnection::ParseRequest(const string& request) const {
  HttpRequest req("/");  // by default, get "/".

  // Plan for STEP 2:
  // 1. Split the request into different lines (split on "\r\n").
  // 2. Extract the URI from the first line and store it in req.URI.
  // 3. For the rest of the lines in the request, track the header name and
  //    value and store them in req.headers_ (e.g. HttpRequest::AddHeader).
  //
  // Hint: Take a look at HttpRequest.h for details about the HTTP header
  // format that you need to parse.
  //
  // You'll probably want to look up boost functions for:
  // - Splitting a string into lines on a "\r\n" delimiter
  // - Trimming whitespace from the end of a string
  // - Converting a string to lowercase.
  //
  // Note: If a header is malformed, skip that line.

  // STEP 2:

  // split requests into different lines
  vector<string> lines;
  boost::algorithm::split(lines, request, boost::is_any_of("\r\n"),
                          boost::token_compress_on);
  if (lines.empty()) {
    return req;
  }
  string request_line = lines[0];

  // split request into its components
  vector<string> request_elements;
  boost::algorithm::split(request_elements, request_line,
                          boost::is_space(), boost::token_compress_on);

  // Extract the URI from the first line and store it in req.URI
  req.set_uri(request_elements[1]);

  // Rest of the lines
  for (size_t i = 1; i < lines.size(); i++) {
    string line = lines[i];
    if (line.empty()) {
      continue;
    }

    std::size_t colon = line.find(':');
    if (colon == string::npos) {
      continue;
    }

    string name = line.substr(0, colon);
    string value = line.substr(colon + 1);

    boost::algorithm::trim(name);
    boost::algorithm::trim(value);
    boost::algorithm::to_lower(name);
    boost::algorithm::to_lower(value);

    req.AddHeader(name, value);
  }
  return req;
}

}  // namespace hw4
