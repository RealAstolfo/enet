#include <memory>

#include "socks4.hpp"
#include "tcp.hpp"

int main(int argc, char **argv) {
  tcp_resolver r;
  auto results = r.resolve("localhost", "8888");

  tcp_socket serv_sock;
  serv_sock.bind(results[0]);

  serv_sock.listen(1);

  tcp_socket cli_sock = serv_sock.accept();

  std::string response;
  std::array<char, 4096> receive_buffer;
  ssize_t bytes = cli_sock.receive(receive_buffer);
  response.append(receive_buffer.data(), bytes);

  socks4_request req;
  std::memcpy(&req, response.data(), sizeof(socks4_request));

  std::cout << (int)req.socks4_version << " " << (int)req.cmd << " "
            << req.dstport << " " << req.destip << " " << std::endl;

  socks4_response resp;
  resp.vn = 0x00;
  resp.rep = req_granted;
  std::string data;
  data.resize(sizeof(resp));
  memcpy(data.data(), &resp, sizeof(resp));
  cli_sock.send(data);

  // Now implement proxy logic here.

  cli_sock.close();
  serv_sock.close();
  return 0;
}
