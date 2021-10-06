#pragma once

#include "network/tarcap/packets_handlers/common/votes_handler.hpp"

namespace taraxa {
class PbftManager;
class NextVotesForPreviousRound;
}  // namespace taraxa

namespace taraxa::network::tarcap {

class GetVotesSyncPacketHandler : public VotesHandler {
 public:
  GetVotesSyncPacketHandler(std::shared_ptr<PeersState> peers_state, std::shared_ptr<PacketsStats> packets_stats,
                            std::shared_ptr<PbftManager> pbft_mgr,
                            std::shared_ptr<NextVotesForPreviousRound> next_votes_mgr, const addr_t& node_addr = {});

  virtual ~GetVotesSyncPacketHandler() = default;

 private:
  void process(const PacketData& packet_data, const std::shared_ptr<TaraxaPeer>& peer) override;

  std::shared_ptr<PbftManager> pbft_mgr_;
  std::shared_ptr<NextVotesForPreviousRound> next_votes_mgr_;
};

}  // namespace taraxa::network::tarcap
