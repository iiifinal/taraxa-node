#include "network/tarcap/packets_handlers/votes_sync_packet_handler.hpp"

#include "pbft/pbft_manager.hpp"
#include "vote_manager/vote_manager.hpp"

namespace taraxa::network::tarcap {

VotesSyncPacketHandler::VotesSyncPacketHandler(
    std::shared_ptr<PeersState> peers_state, std::shared_ptr<PacketsStats> packets_stats,
    std::shared_ptr<PbftManager> pbft_mgr, std::shared_ptr<PbftChain> pbft_chain, std::shared_ptr<VoteManager> vote_mgr,
    std::shared_ptr<NextVotesManager> next_votes_mgr, std::shared_ptr<DbStorage> db, const addr_t &node_addr)
    : ExtVotesPacketHandler(std::move(peers_state), std::move(packets_stats), std::move(pbft_mgr),
                            std::move(pbft_chain), node_addr, "VOTES_SYNC_PH"),
      vote_mgr_(std::move(vote_mgr)),
      next_votes_mgr_(std::move(next_votes_mgr)),
      db_(std::move(db)) {}

void VotesSyncPacketHandler::validatePacketRlpFormat([[maybe_unused]] const PacketData &packet_data) const {
  auto items = packet_data.rlp_.itemCount();
  if (items == 0 || items > kMaxVotesInPacket) {
    throw InvalidRlpItemsCountException(packet_data.type_str_, items, kMaxVotesInPacket);
  }
}

void VotesSyncPacketHandler::process(const PacketData &packet_data, const std::shared_ptr<TaraxaPeer> &peer) {
  auto vote = std::make_shared<Vote>(packet_data.rlp_[0].data().toBytes());

  const auto pbft_current_round = pbft_mgr_->getPbftRound();
  const uint64_t pbft_current_period = pbft_chain_->getPbftChainSize();
  const auto peer_pbft_round = vote->getRound() + 1;

  // Next votes are from too far ahead in the future
  if (pbft_current_round > peer_pbft_round) {
    LOG(log_dg_) << "Dropping votes sync packet due to round. Votes round: " << peer_pbft_round
                 << ", current pbft round: " << pbft_current_round;
    return;
  }

  std::vector<std::shared_ptr<Vote>> next_votes;
  const auto next_votes_count = packet_data.rlp_.itemCount();
  for (size_t i = 0; i < next_votes_count; i++) {
    auto next_vote = std::make_shared<Vote>(packet_data.rlp_[i].data().toBytes());
    const auto next_vote_hash = next_vote->getHash();
    if (next_vote->getRound() != peer_pbft_round - 1) {
      LOG(log_er_) << "Received next votes bundle with unmatched rounds from " << packet_data.from_node_id_
                   << ". The peer may be a malicious player, will be disconnected";
      disconnect(packet_data.from_node_id_, dev::p2p::UserReason);
      return;
    }

    // Old vote or vote from too far in the future, can be dropped
    // TODO: current_pbft_period + 5 -> put 5 into some variable
    if (next_vote->getPeriod() < pbft_current_period || next_vote->getPeriod() > pbft_current_period + 5) {
      LOG(log_dg_) << "Dropping vote " << next_vote_hash << " due to period. Vote period: " << vote->getPeriod()
                   << ", current pbft period: " << pbft_current_period;
      continue;
    }

    if (auto vote_is_valid = validateStandardVote(vote); vote_is_valid.first == false) {
      LOG(log_er_) << "Vote " << next_vote_hash << " validation failed. Err: " << vote_is_valid.second;
      continue;
    }

    if (pbft_current_round < peer_pbft_round) {
      // Check if vote is already in verified queue, if not - try to add it
      if (vote_mgr_->voteInVerifiedMap(next_vote) || !vote_mgr_->addVerifiedVote(next_vote)) {
        LOG(log_dg_) << "Received PBFT next vote " << next_vote_hash << " (from "
                     << packet_data.from_node_id_.abridged() << ") already saved in verified queue.";
        continue;
      }
    }

    LOG(log_dg_) << "Received PBFT next vote " << next_vote_hash;
    peer->markVoteAsKnown(next_vote_hash);
    next_votes.push_back(std::move(next_vote));
  }

  LOG(log_nf_) << "Received " << next_votes_count << " next votes from peer " << packet_data.from_node_id_
               << " node current round " << pbft_current_round << ", peer pbft round " << peer_pbft_round;

  if (pbft_current_round < peer_pbft_round) {
    onNewPbftVotes(std::move(next_votes));

  } else if (pbft_current_round == peer_pbft_round) {
    // Update previous round next votes
    const auto pbft_2t_plus_1 = db_->getPbft2TPlus1(pbft_current_round - 1);
    if (!pbft_2t_plus_1) {
      LOG(log_er_) << "Cannot get PBFT 2t+1 in PBFT round " << pbft_current_round - 1;
      return;
    }

    // Update our previous round next vote bundles...
    next_votes_mgr_->updateWithSyncedVotes(next_votes, pbft_2t_plus_1);
    // Pass them on to our peers...
    const auto updated_next_votes_size = next_votes_mgr_->getNextVotesWeight();
    for (auto const &peer_to_share_to : peers_state_->getAllPeers()) {
      // Do not send votes right back to same peer...
      if (peer_to_share_to.first == packet_data.from_node_id_) {
        continue;
      }

      // Do not send votes to nodes that already have as many bundles as we do...
      if (peer_to_share_to.second->pbft_previous_round_next_votes_size_ >= updated_next_votes_size) {
        continue;
      }

      // Nodes may vote at different values at previous round, so need less or equal
      if (!peer_to_share_to.second->syncing_ && peer_to_share_to.second->pbft_round_ > pbft_current_round) {
        continue;
      }

      std::vector<std::shared_ptr<Vote>> send_next_votes_bundle;
      for (const auto &v : next_votes) {
        if (!peer_to_share_to.second->isVoteKnown(v->getHash())) {
          send_next_votes_bundle.push_back(v);
        }
      }
      sendPbftVotes(peer_to_share_to.first, std::move(send_next_votes_bundle), true);
    }
  }
}

void VotesSyncPacketHandler::broadcastPreviousRoundNextVotesBundle() {
  auto next_votes_bundle = next_votes_mgr_->getNextVotes();
  if (next_votes_bundle.empty()) {
    LOG(log_er_) << "There are empty next votes for previous PBFT round";
    return;
  }

  const auto pbft_current_round = pbft_mgr_->getPbftRound();

  for (auto const &peer : peers_state_->getAllPeers()) {
    // Nodes may vote at different values at previous round, so need less or equal
    if (!peer.second->syncing_ && peer.second->pbft_round_ <= pbft_current_round) {
      std::vector<std::shared_ptr<Vote>> send_next_votes_bundle;
      for (const auto &v : next_votes_bundle) {
        if (!peer.second->isVoteKnown(v->getHash())) {
          send_next_votes_bundle.push_back(v);
        }
      }
      sendPbftVotes(peer.first, std::move(send_next_votes_bundle), true);
    }
  }
}

}  // namespace taraxa::network::tarcap
