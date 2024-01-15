#include "socks4.hpp"
#include "tcp.hpp"

int main(int argc, char **argv) {
  tcp_resolver r;
  auto results = r.resolve("localhost", "8888");

  tcp_socket sock;
  sock.connect(results[0]);

  socks4_request req;
  req.cmd = establish_tcp_stream;
  req.dstport = 4444;
  req.destip = 2130706433; // 1.0.0.127

  std::string data;
  data.resize(sizeof(req));
  std::memcpy(data.data(), &req, sizeof(req));
  sock.send(data);

  std::string response;
  std::array<char, 4096> receive_buffer;
  ssize_t bytes = 0;
  do {
    bytes = sock.receive(receive_buffer);
    response.append(receive_buffer.data(), bytes);
  } while (bytes > 0);

  socks4_response resp;
  std::memcpy(&resp, response.data(), sizeof(resp));
  std::cout << (int)resp.vn << " " << (int)resp.rep << std::endl;

  return 0;
}
