#include <cstdlib>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

#include "https.hpp"
#include "args.hpp"

int main(int argc, char **argv) {
  arguments args;

  https_resolver hr;
  std::vector<endpoint> ips;
  std::string host;
  std::string uri;
  args.add_handler("-w", std::function([&](const std::string_view &url) {
                     std::regex pattern(R"(^(httpss?://)?([^/]+)(/.*)?$)");
                     std::smatch match;
                     std::string urls(url);
                     if (std::regex_match(urls, match, pattern)) {
                       host = match[2].str();
                       uri = match[3].str();
                     }

                     std::cout << "Host: " << host << " URI: " << uri
                               << std::endl;
                     ips = hr.resolve(host, "https");
                     return false;
                   }));

  args.process_args(argc, argv);

  https_socket hs;
  hs.connect(ips);

  if (uri.empty())
    uri += '/';
  std::ostringstream request_stream;
  request_stream << "GET " << uri << " HTTP/1.1\r\n"
                 << "Host: " << host << "\r\n"
                 << "Accept: */*\r\n"
                 << "Connection: close\r\n\r\n";

  std::string request = request_stream.str();

  auto response = hs.request(request);
  hs.close();
  std::cout << response << std::endl;
  return EXIT_SUCCESS;
}
