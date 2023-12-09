#include <cstdlib>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

#include "i2p.hpp"
#include "common/string/args.hpp"

int main(int argc, char **argv) {
  init_i2p(argc, argv, "i2p-test");
  i2p_session::instance().start();
  std::cerr
      << "I2P Addr: "
      << i2p_session::instance().local_destination->GetIdentHash().ToBase32()
      << std::endl;
  arguments args;

  i2p_resolver ir;
  std::string b64;
  std::string host;
  std::string uri;
  args.add_handler("-w", std::function([&](const std::string_view &url) {
                     std::regex pattern(R"(^(https?://)?([^/]+)(/.*)?$)");
                     std::smatch match;
                     std::string urls(url);
                     if (std::regex_match(urls, match, pattern)) {

                       std::string target = ".b32.i2p";

                       host = match[2].str();
                       std::size_t found = host.find(target);
                       if (found != std::string::npos) {
                         host = host.substr(0, found);
                       }

                       uri = match[3].str();
                     }
                     while (b64.length() == 0)
                       b64 = ir.resolve(host);

                     return false;
                   }));

  sleep(60 * 10); // testing sleep method again

  args.process_args(argc, argv);

  std::cerr << "Host: " << host << " URI: " << uri << " B64: " << b64
            << std::endl;

  i2p_socket is;

  if (!is.connect(b64, 80)) { // assume 80 == http
    std::cerr << "Could not connect to host" << std::endl;
    return EXIT_FAILURE;
  }
  std::cerr << std::endl;

  if (uri.empty())
    uri += '/';
  std::ostringstream request_stream;
  request_stream << "GET " << uri << " HTTP/1.1\r\n"
                 << "Host: " << host + ".b32.i2p"
                 << "\r\n"
                 << "Accept: */*\r\n"
                 << "Connection: keep-alive\r\n\r\n";

  std::string request = request_stream.str();

  std::cerr << "Sending Request" << std::endl;
  is.send(request);
  std::string response;
  std::array<char, 4096> receive_buffer;

  std::cerr << "Connection Opened" << std::endl;
  std::size_t bytes = 0;
  do {
    bytes = is.receive(receive_buffer);
    response.append(receive_buffer.data(), bytes);
    if (bytes > 0)
      std::cerr << "Bytes Received " << bytes << std::endl;
  } while (is.stream->IsOpen() || bytes > 0);

  std::cerr << std::endl;

  is.close();
  //  std::cerr << "Bytes Received: " << response.size() << std::endl;
  std::cout << response << std::endl;
  i2p_session::instance().stop();
  deinit_i2p();
  return EXIT_SUCCESS;
}
