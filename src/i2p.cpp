
#include "i2p.hpp"
#include "api.h"
#include "util.h"

#include <condition_variable>
#include <map>
#include <mutex>

i2p_session::i2p_session() {
  if (!is_set)
    generate_keys();
}

i2p_session::~i2p_session() { stop(); }

/*static*/
std::string i2p_session::generate_b32_address_from_destination(
    const std::string &destination) {
  i2p::data::IdentityEx identity;
  identity.FromBase64(destination);
  return identity.GetIdentHash().ToBase32() + ".b32.i2p";
}

const i2p::data::PrivateKeys &i2p_session::generate_keys() {
  is_set = true;
  keys = i2p::data::CreateRandomKeys();
  return keys;
}

std::string i2p_session::get_b32_address() const {
  return keys.GetPublic()->GetIdentHash().ToBase32() + ".b32.i2p";
}

void i2p_session::start() {
  std::map<std::string, std::string> params;
  params[i2p::client::I2CP_PARAM_INBOUND_TUNNELS_QUANTITY] =
      std::to_string(i2p::client::DEFAULT_INBOUND_TUNNELS_QUANTITY);
  params[i2p::client::I2CP_PARAM_INBOUND_TUNNEL_LENGTH] =
      std::to_string(i2p::client::DEFAULT_INBOUND_TUNNEL_LENGTH);
  params[i2p::client::I2CP_PARAM_OUTBOUND_TUNNELS_QUANTITY] =
      std::to_string(i2p::client::DEFAULT_OUTBOUND_TUNNELS_QUANTITY);
  params[i2p::client::I2CP_PARAM_OUTBOUND_TUNNEL_LENGTH] =
      std::to_string(i2p::client::DEFAULT_OUTBOUND_TUNNEL_LENGTH);
  local_destination = i2p::api::CreateLocalDestination(keys, true, &params);

  local_destination->AcceptStreams(
      std::bind(&i2p_session::handle_accept, this, std::placeholders::_1));
}

void i2p_session::stop() {
  if (local_destination) {
    i2p::api::DestroyLocalDestination(local_destination);
    local_destination = nullptr;
  }
}

void add_incoming_i2p_stream(std::shared_ptr<i2p::stream::Stream> stream) {
  if (!stream)
    return;
  static std::mutex v_lock;
  v_lock.lock();
  i2p_session::instance().connection_streams.push_back(stream);
  v_lock.unlock();
}

void i2p_session::handle_accept(std::shared_ptr<i2p::stream::Stream> stream) {
  if (stream) {
    std::cerr << "Incoming Connection From: "
              << stream->GetRemoteIdentity()->GetIdentHash().ToBase32()
              << std::endl;
    add_incoming_i2p_stream(stream);
  }
}

std::shared_ptr<const i2p::data::LeaseSet>
i2p_session::request_lease_set(const i2p::data::IdentHash &ident) {
  if (!local_destination)
    return nullptr;
  std::condition_variable responded;
  std::mutex respondedMutex;
  bool notified = false;
  auto leaseSet = local_destination->FindLeaseSet(ident);
  if (!leaseSet) {
    std::unique_lock<std::mutex> l(respondedMutex);
    local_destination->RequestDestination(
        ident, [&responded, &leaseSet,
                &notified](std::shared_ptr<i2p::data::LeaseSet> ls) {
          leaseSet = ls;
          notified = true;
          responded.notify_all();
        });
    auto ret =
        responded.wait_for(l, std::chrono::seconds(I2P_STREAM_CONNECT_TIMEOUT));
    if (!notified) // unsolicited wakeup
      local_destination->CancelDestinationRequest(ident);
    if (ret == std::cv_status::timeout) {
      // most likely it shouldn't happen
      return nullptr;
    }
  }
  return leaseSet;
}

std::shared_ptr<i2p::stream::Stream>
i2p_session::connect(const i2p::data::IdentHash &ident, const uint16_t port) {
  auto leaseSet = request_lease_set(ident);
  if (!leaseSet)
    return nullptr;
  return local_destination->CreateStream(leaseSet, port);
}

std::string i2p_session::resolve(const std::string &b32) {
  i2p::data::IdentHash ident;
  ident.FromBase32(b32);
  auto leaseSet = request_lease_set(ident);
  return leaseSet ? leaseSet->GetIdentity()->ToBase64() : "";
}
