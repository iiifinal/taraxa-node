#include "blocks_packet_handler.hpp"

#include "dag/dag_block_manager.hpp"
#include "network/tarcap/packets_handler/syncing_state.hpp"

namespace taraxa::network::tarcap {

BlocksPacketHandler::BlocksPacketHandler(std::shared_ptr<PeersState> peers_state,
                                         std::shared_ptr<SyncingState> syncing_state,
                                         std::shared_ptr<DagBlockManager> dag_blk_mgr, const addr_t &node_addr)
    : PacketHandler(std::move(peers_state), node_addr, "BLOCKS_PH"),
      syncing_state_(std::move(syncing_state)),
      dag_blk_mgr_(std::move(dag_blk_mgr)) {}

void BlocksPacketHandler::process(const PacketData &packet_data, const dev::RLP &packet_rlp) {
  std::string received_dag_blocks_str;
  auto it = packet_rlp.begin();
  const bool is_final_sync_packet = (*it++).toInt<unsigned>();

  for (; it != packet_rlp.end();) {
    DagBlock block(*it++);
    peer_->markBlockAsKnown(block.getHash());

    std::vector<Transaction> new_transactions;
    for (size_t i = 0; i < block.getTrxs().size(); i++) {
      Transaction transaction(*it++);
      peer_->markTransactionAsKnown(transaction.getHash());
      new_transactions.push_back(std::move(transaction));
    }

    received_dag_blocks_str += block.getHash().toString() + " ";

    auto status = syncing_state_->checkDagBlockValidation(block);
    if (!status.first) {
      LOG(log_wr_) << "DagBlockValidation failed " << status.second;
      status.second.push_back(block.getHash());
      syncing_state_->requestBlocks(packet_data.from_node_id_, status.second,
                                    GetBlocksPacketRequestType::MissingHashes);
      continue;
    }

    LOG(log_dg_) << "Storing block " << block.getHash().toString() << " with " << new_transactions.size()
                 << " transactions";
    if (block.getLevel() > peer_->dag_level_) peer_->dag_level_ = block.getLevel();
    dag_blk_mgr_->insertBroadcastedBlockWithTransactions(block, new_transactions);
  }

  if (is_final_sync_packet) {
    syncing_state_->set_dag_syncing(false);
    LOG(log_nf_) << "Received final DagBlocksSyncPacket with blocks: " << received_dag_blocks_str;
  } else {
    syncing_state_->set_last_sync_packet_time();
    LOG(log_nf_) << "Received partial DagBlocksSyncPacket with blocks: " << received_dag_blocks_str;
  }
}

}  // namespace taraxa::network::tarcap