#pragma once

#include <taraxa-evm/taraxa-evm.h>

#include <functional>

#include "common/range_view.hpp"
#include "final_chain/state_api_data.hpp"
#include "rewards/block_stats.hpp"
#include "storage/storage.hpp"

namespace taraxa::state_api {

/** @addtogroup FinalChain
 * @{
 */

class StateAPI {
  std::function<h256(EthBlockNumber)> get_blk_hash_;
  taraxa_evm_GetBlockHash get_blk_hash_c_;
  taraxa_evm_state_API_ptr this_c_;
  dev::RLPStream rlp_enc_transition_state_;
  StateTransitionResult result_buf_transition_state_;
  string db_path_;

 public:
  StateAPI(std::function<h256(EthBlockNumber)> get_blk_hash, const Config& state_config, const Opts& opts,
           const OptsDB& opts_db);
  ~StateAPI();
  StateAPI(const StateAPI&) = default;
  StateAPI(StateAPI&&) = default;
  StateAPI& operator=(const StateAPI&) = default;
  StateAPI& operator=(StateAPI&&) = default;

  void update_state_config(const Config& new_config);

  std::optional<Account> get_account(EthBlockNumber blk_num, const addr_t& addr) const;
  u256 get_account_storage(EthBlockNumber blk_num, const addr_t& addr, const u256& key) const;
  bytes get_code_by_address(EthBlockNumber blk_num, const addr_t& addr) const;
  ExecutionResult dry_run_transaction(EthBlockNumber blk_num, const EVMBlock& blk, const EVMTransaction& trx) const;
  bytes trace(EthBlockNumber blk_num, const EVMBlock& blk, const std::vector<EVMTransaction> trx,
              std::optional<Tracing> params = {}) const;
  StateDescriptor get_last_committed_state_descriptor() const;
  const StateTransitionResult& transition_state(const EVMBlock& block,
                                                const util::RangeView<EVMTransaction>& transactions,
                                                const std::vector<rewards::BlockStats>& rewards_stats = {});
  void transition_state_commit();
  void create_snapshot(PbftPeriod period);
  void prune(const std::vector<dev::h256>& state_root_to_keep, EthBlockNumber blk_num);

  // DPOS
  uint64_t dpos_eligible_total_vote_count(EthBlockNumber blk_num) const;
  uint64_t dpos_eligible_vote_count(EthBlockNumber blk_num, const addr_t& addr) const;
  bool dpos_is_eligible(EthBlockNumber blk_num, const addr_t& addr) const;
  u256 get_staking_balance(EthBlockNumber blk_num, const addr_t& addr) const;
  vrf_wrapper::vrf_pk_t dpos_get_vrf_key(EthBlockNumber blk_num, const addr_t& addr) const;
};
/** @} */

}  // namespace taraxa::state_api

namespace taraxa {
using state_api::StateAPI;
}
