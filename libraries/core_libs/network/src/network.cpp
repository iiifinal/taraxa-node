#include "network/network.hpp"

#include <libdevcrypto/Common.h>
#include <libp2p/Host.h>
#include <libp2p/Network.h>

#include <boost/tokenizer.hpp>

#include "config/version.hpp"
#include "network/tarcap/packets_handlers/pbft_sync_packet_handler.hpp"
#include "network/v1_tarcap/taraxa_capability.hpp"

namespace taraxa {

Network::Network(const FullNodeConfig &config, const h256 &genesis_hash,
                 dev::p2p::Host::CapabilitiesFactory construct_capabilities,
                 std::filesystem::path const &network_file_path, dev::KeyPair const &key, std::shared_ptr<DbStorage> db,
                 std::shared_ptr<PbftManager> pbft_mgr, std::shared_ptr<PbftChain> pbft_chain,
                 std::shared_ptr<VoteManager> vote_mgr, std::shared_ptr<DagManager> dag_mgr,
                 std::shared_ptr<TransactionManager> trx_mgr)
    : tp_(config.network.num_threads, false) {
  auto const &node_addr = key.address();
  LOG_OBJECTS_CREATE("NETWORK");
  LOG(log_nf_) << "Read Network Config: " << std::endl << config.network << std::endl;

  // TODO make all these properties configurable
  dev::p2p::NetworkConfig net_conf;
  net_conf.listenIPAddress = config.network.listen_ip;
  net_conf.listenPort = config.network.listen_port;
  net_conf.discovery = true;
  net_conf.allowLocalDiscovery = true;
  net_conf.traverseNAT = false;
  net_conf.publicIPAddress = config.network.public_ip;
  net_conf.pin = false;

  dev::p2p::TaraxaNetworkConfig taraxa_net_conf;
  taraxa_net_conf.ideal_peer_count = config.network.ideal_peer_count;

  // TODO config.network.max_peer_count -> config.network.peer_count_stretch
  taraxa_net_conf.peer_stretch = config.network.max_peer_count / config.network.ideal_peer_count;
  taraxa_net_conf.chain_id = config.genesis.chain_id;
  taraxa_net_conf.expected_parallelism = tp_.capacity();

  string net_version = "TaraxaNode";  // TODO maybe give a proper name?
  if (!construct_capabilities) {
    construct_capabilities = [&](std::weak_ptr<dev::p2p::Host> host) {
      assert(!host.expired());

      const size_t kOldNetworkVersion = 1;
      assert(kOldNetworkVersion < TARAXA_NET_VERSION);

      dev::p2p::Host::CapabilityList capabilities;

      // Register old version of taraxa capability
      auto v1_tarcap =
          std::make_shared<network::v1_tarcap::TaraxaCapability>(host, key, config, kOldNetworkVersion, "V1_TARCAP");
      v1_tarcap->init(genesis_hash, db, pbft_mgr, pbft_chain, vote_mgr, dag_mgr, trx_mgr, key.address());
      capabilities.emplace_back(v1_tarcap);

      // Register new version of taraxa capability
      auto v2_tarcap =
          std::make_shared<network::tarcap::TaraxaCapability>(host, key, config, TARAXA_NET_VERSION, "TARCAP");
      v2_tarcap->init(genesis_hash, db, pbft_mgr, pbft_chain, vote_mgr, dag_mgr, trx_mgr, key.address());
      capabilities.emplace_back(v2_tarcap);

      return capabilities;
    };
  }

  host_ = dev::p2p::Host::make(net_version, construct_capabilities, key, net_conf, taraxa_net_conf, network_file_path);
  for (const auto &tarcap : host_->getSupportedCapabilities()) {
    tarcaps_.emplace(tarcap.second.ref->version(),
                     std::static_pointer_cast<network::tarcap::TaraxaCapability>(tarcap.second.ref));
  }

  for (uint i = 0; i < tp_.capacity(); ++i) {
    tp_.post_loop({100 + i * 20}, [this] {
      while (0 < host_->do_work())
        ;
    });
  }

  LOG(log_nf_) << "Configured host. Listening on address: " << config.network.listen_ip << ":"
               << config.network.listen_port;
}

Network::~Network() {
  tp_.stop();
  for (auto &tarcap : host_->getSupportedCapabilities()) {
    std::static_pointer_cast<network::tarcap::TaraxaCapability>(tarcap.second.ref)->stop();
  }
}

void Network::start() {
  tp_.start();
  for (auto &tarcap : host_->getSupportedCapabilities()) {
    std::static_pointer_cast<network::tarcap::TaraxaCapability>(tarcap.second.ref)->start();
  }

  LOG(log_nf_) << "Started Node id: " << host_->id() << ", listening on port " << host_->listenPort();
}

bool Network::isStarted() { return tp_.is_running(); }

std::list<dev::p2p::NodeEntry> Network::getAllNodes() const { return host_->getNodes(); }

size_t Network::getPeerCount() { return host_->peer_count(); }

unsigned Network::getNodeCount() { return host_->getNodeCount(); }

Json::Value Network::getStatus() {
  // TODO: refactor this: combine node stats from all tarcaps...
  return tarcaps_.end()->second->getNodeStats()->getStatus();
}

bool Network::pbft_syncing() {
  return std::ranges::any_of(tarcaps_, [](const auto &tarcap) { return tarcap.second->pbft_syncing(); });
}


void Network::setSyncStatePeriod(PbftPeriod period) {
  for (auto &tarcap : tarcaps_) {
    if (tarcap.second->pbft_syncing()) {
      tarcap.second->setSyncStatePeriod(period);
    }
  }
}

// METHODS USED IN TESTS ONLY
// Note: for functions use in tests all data are fetched only from the tarcap with the highest version,
//       other functions must use all tarcaps

void Network::setPendingPeersToReady() {
  const auto &peers_state = tarcaps_.end()->second->getPeersState();

  auto peerIds = peers_state->getAllPendingPeersIDs();
  for (const auto &peerId : peerIds) {
    auto peer = peers_state->getPendingPeer(peerId);
    if (peer) {
      peers_state->setPeerAsReadyToSendMessages(peerId, peer);
    }
  }
}

dev::p2p::NodeID Network::getNodeId() const { return host_->id(); }

int Network::getReceivedBlocksCount() const { return tarcaps_.end()->second->getReceivedBlocksCount(); }

int Network::getReceivedTransactionsCount() const { return tarcaps_.end()->second->getReceivedTransactionsCount(); }

std::shared_ptr<network::tarcap::TaraxaPeer> Network::getPeer(dev::p2p::NodeID const &id) const {
  return tarcaps_.end()->second->getPeersState()->getPeer(id);
}
// METHODS USED IN TESTS ONLY

}  // namespace taraxa
