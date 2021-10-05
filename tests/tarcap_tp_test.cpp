#include <gtest/gtest.h>

#include <tuple>

#include "dag/dag_block.hpp"
#include "logger/logger.hpp"
#include "network/tarcap/packets_handler.hpp"
#include "network/tarcap/threadpool/tarcap_thread_pool.hpp"

namespace taraxa::core_tests {

// Do not use BaseTest from "util_test/gtest.hpp" as its functionality is not needed in this test
struct BaseTest : virtual testing::Test {
  testing::UnitTest* current_test = ::testing::UnitTest::GetInstance();
  testing::TestInfo const* current_test_info = current_test->current_test_info();

  virtual ~BaseTest() {}
};

struct TarcapTpTest : BaseTest {};

using namespace taraxa::network;

class PacketsProcessingInfo {
 public:
  struct PacketProcessingTimes {
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point finish_time_;
  };

 public:
  void addPacketProcessingTimes(tarcap::PacketData::PacketId packet_id,
                                PacketProcessingTimes&& packet_processing_times) {
    std::scoped_lock<std::shared_mutex> lock(mutex_);
    bool res = packets_processing_times_.emplace(packet_id, std::move(packet_processing_times)).second;
    assert(res);
  }

  PacketProcessingTimes getPacketProcessingTimes(tarcap::PacketData::PacketId packet_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto found_packet_info = packets_processing_times_.find(packet_id);
    assert(found_packet_info != packets_processing_times_.end());

    return found_packet_info->second;
  }

 private:
  std::unordered_map<tarcap::PacketData::PacketId, PacketProcessingTimes> packets_processing_times_;
  mutable std::shared_mutex mutex_;
};

class DummyPacketHandler : public tarcap::PacketHandler {
 public:
  DummyPacketHandler(std::shared_ptr<tarcap::PeersState> peers_state,
                     std::shared_ptr<tarcap::PacketsStats> packets_stats, const addr_t& node_addr,
                     const std::string& log_channel_name, uint32_t processing_delay_ms,
                     std::shared_ptr<PacketsProcessingInfo> packets_proc_info)
      : PacketHandler(std::move(peers_state), std::move(packets_stats), node_addr, log_channel_name),
        processing_delay_ms_(processing_delay_ms),
        packets_proc_info_(std::move(packets_proc_info)) {}

  virtual ~DummyPacketHandler() = default;

 private:
  void process(const tarcap::PacketData& packet_data,
               [[maybe_unused]] const std::shared_ptr<tarcap::TaraxaPeer>& peer) override {
    // Note do not use LOG() before saving start & finish time as it is internally synchronized and can
    // cause delays, which result in tests fails
    auto start_time = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(processing_delay_ms_));
    auto finish_time = std::chrono::steady_clock::now();

    LOG(log_dg_) << "Processing packet: " << packet_data.type_str_ << ", id(" << packet_data.id_ << ") finished. "
                 << "Start time: " << start_time.time_since_epoch().count()
                 << ", finish time: " << finish_time.time_since_epoch().count();

    packets_proc_info_->addPacketProcessingTimes(packet_data.id_, {start_time, finish_time});
  }

  uint32_t processing_delay_ms_{0};
  std::shared_ptr<PacketsProcessingInfo> packets_proc_info_;
};

// Help functions for tests
struct HandlersInitData {
  dev::p2p::NodeID sender_node_id;

  dev::p2p::NodeID own_node_id;
  addr_t own_node_addr;

  std::shared_ptr<tarcap::PeersState> peers_state;
  std::shared_ptr<tarcap::PacketsStats> packets_stats;
  std::shared_ptr<PacketsProcessingInfo> packets_processing_info;

  dev::p2p::NodeID copySender() { return sender_node_id; }
};

HandlersInitData createHandlersInitData() {
  HandlersInitData ret_init_data;

  ret_init_data.sender_node_id = dev::p2p::NodeID(1);
  ret_init_data.own_node_id = dev::p2p::NodeID(2);
  ret_init_data.own_node_addr = addr_t(2);
  ret_init_data.peers_state =
      std::make_shared<tarcap::PeersState>(std::weak_ptr<dev::p2p::Host>(), ret_init_data.own_node_id);
  ret_init_data.packets_stats = std::make_shared<tarcap::PacketsStats>(ret_init_data.own_node_addr);
  ret_init_data.packets_processing_info = std::make_shared<PacketsProcessingInfo>();

  // Enable packets from sending peer to be processed
  auto peer = ret_init_data.peers_state->addPendingPeer(ret_init_data.sender_node_id);
  ret_init_data.peers_state->setPeerAsReadyToSendMessages(ret_init_data.sender_node_id, peer);

  return ret_init_data;
}

std::shared_ptr<DummyPacketHandler> createDummyPacketHandler(const HandlersInitData& init_data,
                                                             const std::string& logger_name,
                                                             uint32_t processing_delay_ms) {
  return std::make_shared<DummyPacketHandler>(init_data.peers_state, init_data.packets_stats, init_data.own_node_addr,
                                              logger_name, processing_delay_ms, init_data.packets_processing_info);
}

tarcap::PacketData createPacket(dev::p2p::NodeID&& sender_node_id, tarcap::SubprotocolPacketType packet_type,
                                std::optional<std::vector<unsigned char>> packet_rlp_bytes = {}) {
  if (packet_rlp_bytes.has_value()) {
    return {packet_type, std::move(sender_node_id), std::move(packet_rlp_bytes.value())};
  }

  return {packet_type, std::move(sender_node_id), {}};
}

bytes createDagBlockRlp(level_t level) {
  // Creates dag block rlp as it is required for blocking mask to extract dag block level
  DagBlock blk(blk_hash_t(10), level, {}, {}, sig_t(777), blk_hash_t(1), addr_t(15));
  dev::RLPStream s;
  s.appendList(2);
  s.appendRaw(blk.rlp(false));
  s << static_cast<uint8_t>(0);

  return s.out();
}

/**
 * @brief Check all combinations(without repetition) of provided packets that they were processed concurrently:
 *          - packet1.start_time < packet2.finish_time
 *          - packet2.start_time < packet1.finish_time
 *
 * @param packets
 */
void checkConcurrentProcessing(
    const std::vector<std::pair<PacketsProcessingInfo::PacketProcessingTimes, std::string>>& packets) {
  assert(packets.size() >= 2);

  for (size_t i = 0; i < packets.size(); i++) {
    const auto& packet_l = packets[0];
    for (size_t j = i + 1; j < packets.size(); j++) {
      const auto& packet_r = packets[j];
      EXPECT_LT(packet_l.first.start_time_, packet_r.first.finish_time_)
          << packet_l.second << ".start_time < " << packet_r.second << ".finish_time";
      EXPECT_LT(packet_r.first.start_time_, packet_l.first.finish_time_)
          << packet_r.second << ".start_time < " << packet_l.second << ".finish_time";
    }
  }
}

// Test if all "block-free" packets are processed concurrently
// Note: in case someone creates new blocking dependency and does not adjust tests, this test should fail
TEST_F(TarcapTpTest, block_free_packets) {
  HandlersInitData init_data = createHandlersInitData();

  // Creates sender 2 to bypass peer order block on Transaction -> DagBlock packet. In case those packets sent
  // 2 different senders those packets are "block-free"
  dev::p2p::NodeID sender2(3);
  auto peer = init_data.peers_state->addPendingPeer(sender2);
  init_data.peers_state->setPeerAsReadyToSendMessages(sender2, peer);

  auto packets_handler = std::make_shared<tarcap::PacketsHandler>();
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::PbftBlockPacket,
                                   createDummyPacketHandler(init_data, "PBFT_BLOCK_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::TransactionPacket,
                                   createDummyPacketHandler(init_data, "TX_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::DagBlockPacket,
                                   createDummyPacketHandler(init_data, "DAG_BLOCK_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::TestPacket,
                                   createDummyPacketHandler(init_data, "TEST_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::StatusPacket,
                                   createDummyPacketHandler(init_data, "STATUS_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::SyncedPacket,
                                   createDummyPacketHandler(init_data, "SYNCED_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::PbftVotePacket,
                                   createDummyPacketHandler(init_data, "PBFT_VOTE_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::GetPbftNextVotes,
                                   createDummyPacketHandler(init_data, "GET_PBFT_NEXT_VOTES_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::PbftNextVotesPacket,
                                   createDummyPacketHandler(init_data, "PBFT_NEXT_VOTES_PH", 20));

  // Creates threadpool
  // Note: make num of threads >= num of packets to check if they are processed concurrently without blocks, otherwise
  //       some blocks would be blocked for processing due to max threads limit
  tarcap::TarcapThreadPool tp(18);
  tp.setPacketsHandlers(packets_handler);

  // Pushes packets to the tp
  const auto packet0_pbft_block_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftBlockPacket, {})).value();
  const auto packet1_pbft_block_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftBlockPacket, {})).value();

  const auto packet2_tx_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TransactionPacket, {})).value();
  const auto packet3_tx_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TransactionPacket, {})).value();

  const auto packet4_dag_block_id =
      tp.push(createPacket(dev::p2p::NodeID(sender2), tarcap::SubprotocolPacketType::DagBlockPacket,
                           {createDagBlockRlp(0)}))
          .value();
  const auto packet5_dag_block_id =
      tp.push(createPacket(dev::p2p::NodeID(sender2), tarcap::SubprotocolPacketType::DagBlockPacket,
                           {createDagBlockRlp(0)}))
          .value();

  const auto packet6_test_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TestPacket, {})).value();
  const auto packet7_test_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TestPacket, {})).value();

  const auto packet8_status_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::StatusPacket, {})).value();
  const auto packet9_status_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::StatusPacket, {})).value();

  const auto packet10_synced_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::SyncedPacket, {})).value();
  const auto packet11_synced_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::SyncedPacket, {})).value();

  const auto packet12_pbft_vote_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftVotePacket, {})).value();
  const auto packet13_pbft_vote_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftVotePacket, {})).value();

  const auto packet14_get_pbft_next_votes_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::GetPbftNextVotes, {})).value();
  const auto packet15_get_pbft_next_votes_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::GetPbftNextVotes, {})).value();

  const auto packet16_pbft_next_votes_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftNextVotesPacket, {})).value();
  const auto packet17_pbft_next_votes_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftNextVotesPacket, {})).value();

  tp.startProcessing();

  // Wait for all packets to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Check order of packets how they were processed
  const auto packets_proc_info = init_data.packets_processing_info;

  const auto packet0_pbft_block_proc_info = packets_proc_info->getPacketProcessingTimes(packet0_pbft_block_id);
  const auto packet1_pbft_block_proc_info = packets_proc_info->getPacketProcessingTimes(packet1_pbft_block_id);

  const auto packet2_tx_proc_info = packets_proc_info->getPacketProcessingTimes(packet2_tx_id);
  const auto packet3_tx_proc_info = packets_proc_info->getPacketProcessingTimes(packet3_tx_id);

  const auto packet4_dag_block_proc_info = packets_proc_info->getPacketProcessingTimes(packet4_dag_block_id);
  const auto packet5_dag_block_proc_info = packets_proc_info->getPacketProcessingTimes(packet5_dag_block_id);

  const auto packet6_test_proc_info = packets_proc_info->getPacketProcessingTimes(packet6_test_id);
  const auto packet7_test_proc_info = packets_proc_info->getPacketProcessingTimes(packet7_test_id);

  const auto packet8_status_proc_info = packets_proc_info->getPacketProcessingTimes(packet8_status_id);
  const auto packet9_status_proc_info = packets_proc_info->getPacketProcessingTimes(packet9_status_id);

  const auto packet10_synced_proc_info = packets_proc_info->getPacketProcessingTimes(packet10_synced_id);
  const auto packet11_synced_proc_info = packets_proc_info->getPacketProcessingTimes(packet11_synced_id);

  const auto packet12_pbft_vote_proc_info = packets_proc_info->getPacketProcessingTimes(packet12_pbft_vote_id);
  const auto packet13_pbft_vote_proc_info = packets_proc_info->getPacketProcessingTimes(packet13_pbft_vote_id);

  const auto packet14_get_pbft_next_votes_proc_info =
      packets_proc_info->getPacketProcessingTimes(packet14_get_pbft_next_votes_id);
  const auto packet15_get_pbft_next_votes_proc_info =
      packets_proc_info->getPacketProcessingTimes(packet15_get_pbft_next_votes_id);

  const auto packet16_pbft_next_votes_proc_info =
      packets_proc_info->getPacketProcessingTimes(packet16_pbft_next_votes_id);
  const auto packet17_pbft_next_votes_proc_info =
      packets_proc_info->getPacketProcessingTimes(packet17_pbft_next_votes_id);

  checkConcurrentProcessing({
      {packet0_pbft_block_proc_info, "packet0_pbft_block"},
      {packet1_pbft_block_proc_info, "packet1_pbft_block"},
      {packet2_tx_proc_info, "packet2_tx"},
      {packet3_tx_proc_info, "packet3_tx"},
      {packet4_dag_block_proc_info, "packet4_dag_block"},
      {packet5_dag_block_proc_info, "packet5_dag_block"},
      {packet6_test_proc_info, "packet6_test"},
      {packet7_test_proc_info, "packet7_test"},
      {packet8_status_proc_info, "packet8_status"},
      {packet9_status_proc_info, "packet9_status"},
      {packet10_synced_proc_info, "packet10_synced"},
      {packet11_synced_proc_info, "packet10_synced"},
      {packet12_pbft_vote_proc_info, "packet12_pbft_vote"},
      {packet13_pbft_vote_proc_info, "packet13_pbft_vote"},
      {packet14_get_pbft_next_votes_proc_info, "packet14_get_pbft_next_votes"},
      {packet15_get_pbft_next_votes_proc_info, "packet15_get_pbft_next_votes"},
      {packet16_pbft_next_votes_proc_info, "packet16_pbft_next_votes"},
      {packet17_pbft_next_votes_proc_info, "packet17_pbft_next_votes"},
  });
}

// Test "hard blocking dependencies" related synchronous processing of certain packets:
//
// Packets types that are currently hard blocked for processing in another threads due to dependencies,
// e.g. syncing packets must be processed synchronously one by one, etc...
// Each packet type might be simultaneously blocked by multiple different packets that are being processed.
TEST_F(TarcapTpTest, hard_blocking_deps) {
  HandlersInitData init_data = createHandlersInitData();

  auto packets_handler = std::make_shared<tarcap::PacketsHandler>();
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::GetDagSyncPacket,
                                   createDummyPacketHandler(init_data, "GET_DAG_SYNC_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::GetPbftSyncPacket,
                                   createDummyPacketHandler(init_data, "GET_PBFT_SYNC_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::DagSyncPacket,
                                   createDummyPacketHandler(init_data, "DAG_SYNC_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::PbftSyncPacket,
                                   createDummyPacketHandler(init_data, "PBFT_SYNC_PH", 20));

  // Creates threadpool
  tarcap::TarcapThreadPool tp(10);
  tp.setPacketsHandlers(packets_handler);

  // Pushes packets to the tp
  const auto packet0_dag_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagSyncPacket, {})).value();
  const auto packet1_dag_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagSyncPacket, {})).value();
  const auto packet2_get_dag_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::GetDagSyncPacket, {})).value();
  const auto packet3_get_dag_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::GetDagSyncPacket, {})).value();
  const auto packet4_get_pbft_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::GetPbftSyncPacket, {})).value();
  const auto packet5_get_pbft_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::GetPbftSyncPacket, {})).value();
  const auto packet6_pbft_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftSyncPacket, {})).value();
  const auto packet7_pbft_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftSyncPacket, {})).value();
  const auto packet8_get_dag_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::GetDagSyncPacket, {})).value();

  tp.startProcessing();

  // How should dag blocks packets be processed:
  /*
    ------------------------
    --- packet0_dag_sync ---
    ------------------------
                              ------------------------
                              --- packet1_dag_sync ---
                              ------------------------
    -------------------------
    -- packet2_get_dag_sync -
    -------------------------
                              -------------------------
                              -- packet3_get_dag_sync -
                              -------------------------
    -------------------------
    - packet4_get_pbft_sync -
    -------------------------
                              -------------------------
                              - packet5_get_pbft_sync -
                              -------------------------
    ------------------------
    --- packet6_pbft_sync --
    ------------------------
                              ------------------------
                              --- packet7_pbft_sync --
                              ------------------------
                                                       ------------------------
                                                       - packet8_get_dag_sync -
                                                       ------------------------
    ..................................................................................... time
  */

  // Wait for all packets to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Check order of packets how they were processed
  const auto packets_proc_info = init_data.packets_processing_info;

  const auto packet0_dag_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet0_dag_sync_id);
  const auto packet1_dag_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet1_dag_sync_id);
  const auto packet2_get_dag_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet2_get_dag_sync_id);
  const auto packet3_get_dag_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet3_get_dag_sync_id);
  const auto packet4_get_pbft_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet4_get_pbft_sync_id);
  const auto packet5_get_pbft_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet5_get_pbft_sync_id);
  const auto packet6_pbft_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet6_pbft_sync_id);
  const auto packet7_pbft_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet7_pbft_sync_id);
  const auto packet8_get_dag_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet8_get_dag_sync_id);

  checkConcurrentProcessing({
      {packet0_dag_sync_proc_info, "packet0_dag_sync"},
      {packet2_get_dag_sync_proc_info, "packet2_get_dag_sync"},
      {packet4_get_pbft_sync_proc_info, "packet4_get_pbft_sync"},
      {packet6_pbft_sync_proc_info, "packet6_pbft_sync"},
  });

  checkConcurrentProcessing({
      {packet1_dag_sync_proc_info, "packet1_dag_sync"},
      {packet3_get_dag_sync_proc_info, "packet3_get_dag_sync"},
      {packet5_get_pbft_sync_proc_info, "packet5_get_pbft_sync"},
      {packet7_pbft_sync_proc_info, "packet7_pbft_sync"},
  });

  EXPECT_GT(packet1_dag_sync_proc_info.start_time_, packet0_dag_sync_proc_info.finish_time_);
  EXPECT_GT(packet3_get_dag_sync_proc_info.start_time_, packet2_get_dag_sync_proc_info.finish_time_);
  EXPECT_GT(packet5_get_pbft_sync_proc_info.start_time_, packet4_get_pbft_sync_proc_info.finish_time_);
  EXPECT_GT(packet7_pbft_sync_proc_info.start_time_, packet6_pbft_sync_proc_info.finish_time_);

  EXPECT_GT(packet8_get_dag_sync_proc_info.start_time_, packet3_get_dag_sync_proc_info.finish_time_);
}

// Test "peer-order blocking dependencies" related to specific (peer & order) combination:
//
// Packets types that are blocked only for processing when received from specific peer & after specific
// time (order), e.g.: new dag block packet processing is blocked until all transactions packets that were received
// before it are processed. This blocking dependency is applied only for the same peer so transaction packet from one
// peer does not block new dag block packet from another peer
TEST_F(TarcapTpTest, peer_order_blocking_deps) {
  HandlersInitData init_data = createHandlersInitData();

  auto packets_handler = std::make_shared<tarcap::PacketsHandler>();
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::TransactionPacket,
                                   createDummyPacketHandler(init_data, "TX_PH", 20));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::DagBlockPacket,
                                   createDummyPacketHandler(init_data, "DAG_BLOCK_PH", 0));
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::DagSyncPacket,
                                   createDummyPacketHandler(init_data, "SYNC_TEST_PH", 40));

  // Creates threadpool
  tarcap::TarcapThreadPool tp(10);
  tp.setPacketsHandlers(packets_handler);

  // Pushes packets to the tp
  const auto packet0_tx_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TransactionPacket)).value();
  const auto packet1_tx_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TransactionPacket)).value();
  const auto packet2_dag_sync_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagSyncPacket)).value();
  const auto packet3_tx_id =
      tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TransactionPacket)).value();
  const auto packet4_dag_block_id =
      tp.push(
            createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagBlockPacket, {createDagBlockRlp(1)}))
          .value();

  // How should dag blocks packets be processed:
  /*
    --------------
    - packet0_tx -
    --------------
    --------------
    - packet1_tx -
    --------------
    ----------------------------
    ----- packet2_dag_sync -----
    ----------------------------
    --------------
    - packet3_tx -
    --------------
                                 ---------------------
                                 - packet4_dag_block -
                                 ---------------------
    ..................................................................................... time
  */

  tp.startProcessing();

  // Wait for all packets to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Check order of packets how they were processed
  const auto packets_proc_info = init_data.packets_processing_info;

  const auto packet0_tx_proc_info = packets_proc_info->getPacketProcessingTimes(packet0_tx_id);
  const auto packet1_tx_proc_info = packets_proc_info->getPacketProcessingTimes(packet1_tx_id);
  const auto packet3_tx_proc_info = packets_proc_info->getPacketProcessingTimes(packet3_tx_id);
  const auto packet4_dag_block_proc_info = packets_proc_info->getPacketProcessingTimes(packet4_dag_block_id);
  const auto packet2_dag_sync_proc_info = packets_proc_info->getPacketProcessingTimes(packet2_dag_sync_id);

  checkConcurrentProcessing({
      {packet0_tx_proc_info, "packet0_tx"},
      {packet1_tx_proc_info, "packet1_tx"},
      {packet2_dag_sync_proc_info, "packet2_dag_sync"},
      {packet3_tx_proc_info, "packet3_tx"},
  });

  EXPECT_GT(packet2_dag_sync_proc_info.finish_time_, packet0_tx_proc_info.finish_time_);
  EXPECT_GT(packet2_dag_sync_proc_info.finish_time_, packet1_tx_proc_info.finish_time_);
  EXPECT_GT(packet2_dag_sync_proc_info.finish_time_, packet3_tx_proc_info.finish_time_);

  EXPECT_GT(packet4_dag_block_proc_info.start_time_, packet2_dag_sync_proc_info.finish_time_);
}

// Test "dag-level blocking dependencies" related to dag blocks levels:
//
// Ideally only dag blocks with the same level should be processed. In reality there are situation when node receives
// dag block with smaller level than the level of blocks that are already being processed. In such case these blocks
// with smaller levels can be processed concurrently with blocks that have higher level. All new dag blocks with higher
// level than the lowest level from all the blocks that currently being processed are blocked for processing
TEST_F(TarcapTpTest, dag_blks_lvls_ordering) {
  HandlersInitData init_data = createHandlersInitData();

  auto packets_handler = std::make_shared<tarcap::PacketsHandler>();
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::DagBlockPacket,
                                   createDummyPacketHandler(init_data, "DAG_BLOCK_PH", 20));

  // Creates threadpool
  tarcap::TarcapThreadPool tp(10);
  tp.setPacketsHandlers(packets_handler);

  // Pushes packets to the tp
  const auto blk0_lvl1_id = tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagBlockPacket,
                                                 {createDagBlockRlp(1)}))
                                .value();
  const auto blk1_lvl1_id = tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagBlockPacket,
                                                 {createDagBlockRlp(1)}))
                                .value();
  const auto blk2_lvl0_id = tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagBlockPacket,
                                                 {createDagBlockRlp(0)}))
                                .value();
  const auto blk3_lvl1_id = tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagBlockPacket,
                                                 {createDagBlockRlp(1)}))
                                .value();
  const auto blk4_lvl2_id = tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagBlockPacket,
                                                 {createDagBlockRlp(2)}))
                                .value();
  const auto blk5_lvl3_id = tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::DagBlockPacket,
                                                 {createDagBlockRlp(3)}))
                                .value();

  tp.startProcessing();

  // How should dag blocks packets be processed:
  /*
    -------------
    - blk0_lvl1 -
    -------------
    -------------
    - blk1_lvl1 -
    -------------
    -------------
    - blk2_lvl0 -
    -------------
                  -------------
                  - blk3_lvl1 -
                  -------------
                                -------------
                                - blk4_lvl2 -
                                -------------
                                              -------------
                                              - blk5_lvl3 -
                                              -------------
    ..................................................................................... time
  */

  // Wait for all packets to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Check order of packets how they were processed
  const auto packets_proc_info = init_data.packets_processing_info;

  const auto blk0_lvl1_proc_info = packets_proc_info->getPacketProcessingTimes(blk0_lvl1_id);
  const auto blk1_lvl1_proc_info = packets_proc_info->getPacketProcessingTimes(blk1_lvl1_id);
  const auto blk2_lvl0_proc_info = packets_proc_info->getPacketProcessingTimes(blk2_lvl0_id);
  const auto blk3_lvl1_proc_info = packets_proc_info->getPacketProcessingTimes(blk3_lvl1_id);
  const auto blk4_lvl2_proc_info = packets_proc_info->getPacketProcessingTimes(blk4_lvl2_id);
  const auto blk5_lvl3_proc_info = packets_proc_info->getPacketProcessingTimes(blk5_lvl3_id);

  checkConcurrentProcessing({
      {blk0_lvl1_proc_info, "blk0_lvl1"},
      {blk1_lvl1_proc_info, "blk1_lvl1"},
      {blk2_lvl0_proc_info, "blk2_lvl0"},
  });

  EXPECT_GT(blk3_lvl1_proc_info.start_time_, blk2_lvl0_proc_info.finish_time_);
  EXPECT_GT(blk4_lvl2_proc_info.start_time_, blk3_lvl1_proc_info.finish_time_);
  EXPECT_GT(blk5_lvl3_proc_info.start_time_, blk4_lvl2_proc_info.finish_time_);
}

// Test threads borrowing
//
// It can happen that no packet for processing was returned during the first iteration over priority queues as there
// are limits for max total workers per each priority queue. These limits can and should be ignored in some
// scenarios... For example:
// High priority queue reached it's max workers limit, other queues have inside many blocked packets that cannot be
// currently processed concurrently and MAX_TOTAL_WORKERS_COUNT is not reached yet. In such case some threads might
// be unused. In such cases priority queues max workers limits can and should be ignored.
TEST_F(TarcapTpTest, threads_borrowing) {
  HandlersInitData init_data = createHandlersInitData();

  auto packets_handler = std::make_shared<tarcap::PacketsHandler>();
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::PbftVotePacket,
                                   createDummyPacketHandler(init_data, "PBFT_VOTE_PH", 20));

  // Creates threadpool
  const size_t threads_num = 10;
  tarcap::TarcapThreadPool tp(threads_num);
  tp.setPacketsHandlers(packets_handler);

  // Pushes packets to the tp
  std::vector<uint64_t> pushed_packets_ids;
  for (size_t i = 0; i < threads_num; i++) {
    uint64_t packet_id =
        tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftVotePacket, {})).value();
    pushed_packets_ids.push_back(packet_id);
  }

  tp.startProcessing();

  // Wait for all packets to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Check order of packets how they were processed
  const auto packets_proc_info = init_data.packets_processing_info;

  std::vector<std::pair<PacketsProcessingInfo::PacketProcessingTimes, std::string>> packets_proc_info_vec;
  for (const auto packet_id : pushed_packets_ids) {
    packets_proc_info_vec.emplace_back(packets_proc_info->getPacketProcessingTimes(packet_id),
                                       "packet" + std::to_string(packet_id) + "_pbft_vote");
  }

  // Check if all pbft vote packets were processed concurrently -> threads from other queues had to be borrowed for that
  checkConcurrentProcessing(packets_proc_info_vec);
}

// Test low priority queue starvation
//
// It should never happen that packets from lower priority queues are waiting to be processed until all packets from
// higher priority queues are processed
TEST_F(TarcapTpTest, low_priotity_queue_starvation) {
  HandlersInitData init_data = createHandlersInitData();

  auto packets_handler = std::make_shared<tarcap::PacketsHandler>();
  // Handler for packet from high priority queue
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::PbftVotePacket,
                                   createDummyPacketHandler(init_data, "PBFT_VOTE_PH", 20));

  // Handler for packet from mid priority queue
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::TransactionPacket,
                                   createDummyPacketHandler(init_data, "TX_PH", 20));

  // Handler for packet from mid priority queue
  packets_handler->registerHandler(tarcap::SubprotocolPacketType::TestPacket,
                                   createDummyPacketHandler(init_data, "TEST_PH", 20));

  // Creates threadpool
  size_t threads_num = 10;
  tarcap::TarcapThreadPool tp(threads_num);
  tp.setPacketsHandlers(packets_handler);

  // Push 10x more packets for each prioRtiy queue than max tp capacity to make sure that tp wont be able to process all
  // packets from each queue concurrently -> many packets will be waiting due to max threads num reached for specific
  // priority queues
  for (size_t i = 0; i < 2 * 10 * threads_num; i++) {
    tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::PbftVotePacket, {})).value();
    tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TransactionPacket, {})).value();
  }

  // Push a few packets low priority packets
  for (size_t i = 0; i < 4; i++) {
    tp.push(createPacket(init_data.copySender(), tarcap::SubprotocolPacketType::TestPacket, {})).value();
  }

  tp.startProcessing();

  // Do not Wwit for all packets to be processed, wait only for packets from low priority queues to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const auto [high_priority_queue_size, mid_priority_queue_size, low_priority_queue_size] = tp.getQueueSize();

  EXPECT_GT(high_priority_queue_size, 0);
  EXPECT_GT(mid_priority_queue_size, 0);
  EXPECT_EQ(low_priority_queue_size, 0);
}

}  // namespace taraxa::core_tests

int main(int argc, char** argv) {
  using namespace taraxa;

  auto logging = logger::createDefaultLoggingConfig();

  // Set this to debug to see log msgs
  logging.verbosity = logger::Verbosity::Debug;

  addr_t node_addr;
  logger::InitLogging(logging, node_addr);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}