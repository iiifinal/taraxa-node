#pragma once

#include "network/ws_server.hpp"

namespace taraxa::net {

class GraphQlWSSession final : public WSSession {
 public:
  using WSSession::WSSession;
  std::string processRequest(const std::string_view& request) override;

  void triggerTestSubscribtion(unsigned int number);
};

class GraphQlWsServer final : public WSServer {
 public:
  using WSServer::WSServer;
  std::shared_ptr<WSSession> createSession(tcp::socket&& socket) override;
};

}  // namespace taraxa::net