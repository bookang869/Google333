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

#include <boost/algorithm/string.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <sstream>

#include "./FileReader.h"
#include "./HttpConnection.h"
#include "./HttpRequest.h"
#include "./HttpUtils.h"
#include "./HttpServer.h"
#include "./libhw3/QueryProcessor.h"

using std::cerr;
using std::cout;
using std::endl;
using std::list;
using std::map;
using std::string;
using std::stringstream;
using std::unique_ptr;

namespace hw4
{
  ///////////////////////////////////////////////////////////////////////////////
  // Constants, internal helper functions
  ///////////////////////////////////////////////////////////////////////////////
  static const char *kThreegleStr =
      "<html><head><title>333gle</title></head>\n"
      "<body>\n"
      "<center style=\"font-size:500%;\">\n"
      "<span style=\"position:relative;bottom:-0.33em;color:orange;\">3</span>"
      "<span style=\"color:red;\">3</span>"
      "<span style=\"color:gold;\">3</span>"
      "<span style=\"color:blue;\">g</span>"
      "<span style=\"color:green;\">l</span>"
      "<span style=\"color:red;\">e</span>\n"
      "</center>\n"
      "<p>\n"
      "<div style=\"height:20px;\"></div>\n"
      "<center>\n"
      "<form action=\"/query\" method=\"get\">\n"
      "<input type=\"text\" size=30 name=\"terms\" />\n"
      "<input type=\"submit\" value=\"Search\" />\n"
      "</form>\n"
      "</center><p>\n";

  // static
  const int HttpServer::kNumThreads = 100;

  // This is the function that threads are dispatched into
  // in order to process new client connections.
  static void HttpServer_ThrFn(ThreadPool::Task *t);

  // Given a request, produce a response.
  static HttpResponse ProcessRequest(const HttpRequest &req,
                                     const string &base_dir,
                                     const list<string> &indices);

  // Process a file request.
  static HttpResponse ProcessFileRequest(const string &uri,
                                         const string &base_dir);

  // Process a query request.
  static HttpResponse ProcessQueryRequest(const string &uri,
                                          const list<string> &indices);

  ///////////////////////////////////////////////////////////////////////////////
  // HttpServer
  ///////////////////////////////////////////////////////////////////////////////
  bool HttpServer::Run(void)
  {
    // Create the server listening socket.
    int listen_fd;
    cout << "  creating and binding the listening socket..." << endl;
    if (!socket_.BindAndListen(AF_INET6, &listen_fd))
    {
      cerr << endl
           << "Couldn't bind to the listening socket." << endl;
      return false;
    }

    // Spin, accepting connections and dispatching them.  Use a
    // threadpool to dispatch connections into their own thread.
    cout << "  accepting connections..." << endl
         << endl;
    ThreadPool tp(kNumThreads);
    while (1)
    {
      HttpServerTask *hst = new HttpServerTask(HttpServer_ThrFn);
      hst->base_dir = static_file_dir_path_;
      hst->indices = &indices_;
      if (!socket_.Accept(&hst->client_fd,
                          &hst->c_addr,
                          &hst->c_port,
                          &hst->c_dns,
                          &hst->s_addr,
                          &hst->s_dns))
      {
        // The accept failed for some reason, so quit out of the server.
        // (Will happen when kill command is used to shut down the server.)
        break;
      }
      // The accept succeeded; dispatch it.
      tp.Dispatch(hst);
    }
    return true;
  }

  static void HttpServer_ThrFn(ThreadPool::Task *t)
  {
    // Cast back our HttpServerTask structure with all of our new
    // client's information in it.
    unique_ptr<HttpServerTask> hst(static_cast<HttpServerTask *>(t));
    cout << "  client " << hst->c_dns << ":" << hst->c_port << " "
         << "(IP address " << hst->c_addr << ")" << " connected." << endl;

    // Read in the next request, process it, and write the response.

    // Use the HttpConnection class to read and process the next
    // request from our current client, then write out our response.  If
    // the client sends a "Connection: close\r\n" header, then shut down
    // the connection -- we're done.
    //
    // Hint: the client can make multiple requests on our single connection,
    // so we should keep the connection open between requests rather than
    // creating/destroying the same connection repeatedly.

    // STEP 1:
    HttpConnection connection = HttpConnection(hst->client_fd);
    HttpRequest request;
    bool done = false;
    while (!done)
    {
      if (!connection.GetNextRequest(&request))
      {
        cerr << "Failed to get next request or";
        cerr << "connection closed by the client." << endl;
        break;
      }
      HttpResponse response = ProcessRequest(request,
                                             hst->base_dir, *(hst->indices));

      if (!connection.WriteResponse(response))
      {
        cerr << "Failed to send response." << endl;
        break;
      }

      if (request.GetHeaderValue("connection") == "close")
      {
        cerr << "Client requested to close connection." << endl;
        done = true;
      }
    }
  }

  static HttpResponse ProcessRequest(const HttpRequest &req,
                                     const string &base_dir,
                                     const list<string> &indices)
  {
    // Is the user asking for a static file?
    if (req.uri().substr(0, 8) == "/static/")
    {
      return ProcessFileRequest(req.uri(), base_dir);
    }

    // The user must be asking for a query.
    return ProcessQueryRequest(req.uri(), indices);
  }

  static HttpResponse ProcessFileRequest(const string &uri,
                                         const string &base_dir)
  {
    // The response we'll build up.
    HttpResponse ret;

    // Steps to follow:
    // 1. Use the URLParser class to figure out what file name
    //    the user is asking for. Note that we identify a request
    //    as a file request if the URI starts with '/static/'
    //
    // 2. Use the FileReader class to read the file into memory
    //
    // 3. Copy the file content into the ret.body
    //
    // 4. Depending on the file name suffix, set the response
    //    Content-type header as appropriate, e.g.,:
    //      --> for ".html" or ".htm", set to "text/html"
    //      --> for ".jpeg" or ".jpg", set to "image/jpeg"
    //      --> for ".png", set to "image/png"
    //      etc.
    //    You should support the file types mentioned above,
    //    as well as ".txt", ".js", ".css", ".xml", ".gif",
    //    and any other extensions to get bikeapalooza
    //    to match the solution server.
    //
    // be sure to set the response code, protocol, and message
    // in the HttpResponse as well.

    // STEP 2:
    size_t qm_pos = uri.find("?");
    string new_uri = qm_pos != string::npos ? uri.substr(0, qm_pos) : uri;
    string file_name = new_uri.substr(8);

    ret.set_protocol("HTTP/1.1");

    FileReader reader(base_dir, file_name);

    std::string contents;
    if (reader.ReadFile(&contents))
    {
      ret.set_response_code(200);
      ret.set_message("OK");
      ret.AppendToBody(contents);

      std::string extension = file_name.substr(file_name.find_last_of(".") + 1);
      if (extension == "html" || extension == "htm")
      {
        ret.set_content_type("text/html");
      }
      else if (extension == "jpeg" || extension == "jpg")
      {
        ret.set_content_type("image/jpeg");
      }
      else if (extension == "png")
      {
        ret.set_content_type("image/png");
      }
      else if (extension == "gif")
      {
        ret.set_content_type("image/gif");
      }
      else if (extension == "css")
      {
        ret.set_content_type("text/css");
      }
      else if (extension == "js")
      {
        ret.set_content_type("application/javascript");
      }
      else if (extension == "xml")
      {
        ret.set_content_type("application/xml");
      }
      else if (extension == "txt")
      {
        ret.set_content_type("text/plain");
      }
    }
    else
    {
      // If you couldn't find the file, return an HTTP 404 error.
      // ret.set_protocol("HTTP/1.1");
      ret.set_response_code(404);
      ret.set_message("Not Found");
      ret.AppendToBody("<html><body>Couldn't find file \"" + EscapeHtml(file_name) + "\"</body></html>\n");
    }

    return ret;
  }

  static HttpResponse ProcessQueryRequest(const string &uri,
                                          const list<string> &indices)
  {
    // The response we're building up.
    HttpResponse ret;

    // Your job here is to figure out how to present the user with
    // the same query interface as our solution_binaries/http333d server.
    // A couple of notes:
    //
    // 1. The 333gle logo and search box/button should be present on the site.
    //
    // 2. If the user had previously typed in a search query, you also need
    //    to display the search results.
    //
    // 3. you'll want to use the URLParser to parse the uri and extract
    //    search terms from a typed-in search query.  convert them
    //    to lower case.
    //
    // 4. Initialize and use hw3::QueryProcessor to process queries with the
    //    search indices.
    //
    // 5. With your results, try figuring out how to hyperlink results to file
    //    contents, like in solution_binaries/http333d. (Hint: Look into HTML
    //    tags!)

    // STEP 3:
    ret.set_protocol("HTTP/1.1");

    URLParser parser;
    parser.Parse(uri);
    auto args = parser.args();

    stringstream ss;
    ss << kThreegleStr;

    if (!args["terms"].empty())
    {
      string search_terms = args["terms"];
      boost::algorithm::to_lower(search_terms);

      vector<string> terms;
      std::istringstream iss(search_terms);
      string term;
      while (iss >> term)
      {
        terms.push_back(term);
      }

      hw3::QueryProcessor qp(indices, true);
      auto results = qp.ProcessQuery(terms);

      if (results.empty())
      {
        ss << "<div>" << "No results found for <b>"
           << EscapeHtml(search_terms) << "</b></div><br>";
      }
      else
      {
        ss << "<div>" << results.size() << " results found for <b>"
           << EscapeHtml(search_terms) << "</b></div><br>";
      }

      for (const auto &result : results)
      {
        if (result.document_name.find("http://") == 0 ||
            result.document_name.find("https://") == 0)
        {
          ss << "<div><li><a href=\"" << result.document_name
             << "\" target=\"_blank\">" << result.document_name
             << "</a> [" << result.rank << "]</li></div>";
        }
        else
        {
          ss << "<div><li><a href=\"/static/" << result.document_name
             << "\">" << result.document_name << "</a> [" << result.rank
             << "]</li></div>";
        }
      }
    }

    ret.AppendToBody(ss.str());
    ret.set_response_code(200);
    ret.set_message("OK");
    ret.set_content_type("text/html");

    return ret;
  }

} // namespace hw4
