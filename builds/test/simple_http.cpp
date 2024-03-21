#include <cstdlib>
#include <regex>

#include "args.hpp"
#include "http.hpp"

int main(int argc, char **argv) {
  arguments args;

  http_resolver hr;
  std::vector<endpoint> ips;
  std::string host;
  std::string uri;
  args.add_handler("-w", std::function([&](const std::string_view &url) {
                     std::regex pattern(R"(^(https?://)?([^/]+)(/.*)?$)");
                     std::smatch match;
                     std::string urls(url);
                     if (std::regex_match(urls, match, pattern)) {
                       host = match[2].str();
                       uri = match[3].str();
                     }

                     ips = hr.resolve(host, "http");
                     return false;
                   }));

  args.process_args(argc, argv);

  std::cout << "Host: " << host << " URI: " << uri << std::endl;
  http_socket hs;
  hs.connect(ips[0]);

  if (uri.empty())
    uri += '/';
  auto response = hs.get(uri);

  std::cout << response << std::endl;
  return EXIT_SUCCESS;
}
