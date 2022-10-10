/*
 * @Copyright: Taraxa.io
 * @Author: Qi Gao
 * @Date: 2019-04-10
 * @Last Modified by: Qi Gao
 * @Last Modified time: 2019-08-15
 */

#include "pbft/pbft_manager.hpp"

#include <libdevcore/SHA3.h>

#include <chrono>
#include <cstdint>
#include <string>

#include "dag/dag.hpp"
#include "final_chain/final_chain.hpp"
#include "network/tarcap/packets_handlers/pbft_sync_packet_handler.hpp"
#include "network/tarcap/packets_handlers/vote_packet_handler.hpp"
#include "network/tarcap/packets_handlers/votes_sync_packet_handler.hpp"
#include "pbft/period_data.hpp"
#include "vote_manager/vote_manager.hpp"

namespace taraxa {
using vrf_output_t = vrf_wrapper::vrf_output_t;

PbftManager::PbftManager(const PbftConfig &conf, const blk_hash_t &dag_genesis_block_hash, addr_t node_addr,
                         std::shared_ptr<DbStorage> db, std::shared_ptr<PbftChain> pbft_chain,
                         std::shared_ptr<VoteManager> vote_mgr, std::shared_ptr<NextVotesManager> next_votes_mgr,
                         std::shared_ptr<DagManager> dag_mgr, std::shared_ptr<TransactionManager> trx_mgr,
                         std::shared_ptr<FinalChain> final_chain, std::shared_ptr<KeyManager> key_manager,
                         secret_t node_sk, vrf_sk_t vrf_sk, uint32_t max_levels_per_period)
    : db_(std::move(db)),
      next_votes_manager_(std::move(next_votes_mgr)),
      pbft_chain_(std::move(pbft_chain)),
      vote_mgr_(std::move(vote_mgr)),
      dag_mgr_(std::move(dag_mgr)),
      trx_mgr_(std::move(trx_mgr)),
      final_chain_(std::move(final_chain)),
      key_manager_(std::move(key_manager)),
      node_addr_(std::move(node_addr)),
      node_sk_(std::move(node_sk)),
      node_pub_(dev::toPublic(node_sk_)),
      vrf_sk_(std::move(vrf_sk)),
      LAMBDA_ms_MIN(conf.lambda_ms_min),
      COMMITTEE_SIZE(conf.committee_size),
      NUMBER_OF_PROPOSERS(conf.number_of_proposers),
      DAG_BLOCKS_SIZE(conf.dag_blocks_size),
      GHOST_PATH_MOVE_BACK(conf.ghost_path_move_back),
      dag_genesis_block_hash_(dag_genesis_block_hash),
      config_(conf),
      max_levels_per_period_(max_levels_per_period) {
  LOG_OBJECTS_CREATE("PBFT_MGR");
}

PbftManager::~PbftManager() { stop(); }

void PbftManager::setNetwork(std::weak_ptr<Network> network) { network_ = move(network); }

void PbftManager::start() {
  if (bool b = true; !stopped_.compare_exchange_strong(b, !b)) {
    return;
  }

  daemon_ = std::make_unique<std::thread>([this]() { run(); });
  LOG(log_dg_) << "PBFT daemon initiated ...";
}

void PbftManager::stop() {
  if (bool b = false; !stopped_.compare_exchange_strong(b, !b)) {
    return;
  }

  {
    std::unique_lock<std::mutex> lock(stop_mtx_);
    stop_cv_.notify_all();
  }

  daemon_->join();
  final_chain_->stop();

  LOG(log_dg_) << "PBFT daemon terminated ...";
}

/* When a node starts up it has to sync to the current phase (type of block
 * being generated) and step (within the block generation round)
 * Five step loop for block generation over three phases of blocks
 * User's credential, sigma_i_p for a round p is sig_i(R, p)
 * Leader l_i_p = min ( H(sig_j(R,p) ) over set of j in S_i where S_i is set of
 * users from which have received valid round p credentials
 */
void PbftManager::run() {
  LOG(log_nf_) << "PBFT running ...";

  for (auto period = final_chain_->last_block_number() + 1, curr_period = pbft_chain_->getPbftChainSize();
       period <= curr_period; ++period) {
    auto period_raw = db_->getPeriodDataRaw(period);
    if (period_raw.size() == 0) {
      LOG(log_er_) << "DB corrupted - Cannot find PBFT block in period " << period << " in PBFT chain DB pbft_blocks.";
      assert(false);
    }
    PeriodData period_data(period_raw);
    if (period_data.pbft_blk->getPeriod() != period) {
      LOG(log_er_) << "DB corrupted - PBFT block hash " << period_data.pbft_blk->getBlockHash()
                   << " has different period " << period_data.pbft_blk->getPeriod()
                   << " in block data than in block order db: " << period;
      assert(false);
    }
    // We need this section because votes need to be verified for reward distribution
    for (const auto &v : period_data.previous_block_cert_votes) {
      validateVote(v);
    }

    finalize_(std::move(period_data), db_->getFinalizedDagBlockHashesByPeriod(period), period == curr_period);
  }
  // Verify that last block cert votes point to the last block hash
  auto last_block_cert_votes = db_->getLastBlockCertVotes();
  for (auto it = last_block_cert_votes.begin(); it != last_block_cert_votes.end();) {
    if ((*it)->getBlockHash() != pbft_chain_->getLastPbftBlockHash()) {
      LOG(log_er_) << "Found invalid last block cert vote: " << **it;
      db_->removeLastBlockCertVotes((*it)->getHash());
      it = last_block_cert_votes.erase(it);
    } else {
      ++it;
    }
  }
  vote_mgr_->replaceRewardVotes(last_block_cert_votes);
  // Initialize PBFT status
  initialState();

  continuousOperation_();
}

// Only to be used for tests...
void PbftManager::resume() {
  // Will only appear in testing...
  LOG(log_si_) << "Resuming PBFT daemon...";

  if (step_ == 1) {
    state_ = value_proposal_state;
  } else if (step_ == 2) {
    state_ = filter_state;
  } else if (step_ == 3) {
    state_ = certify_state;
  } else if (step_ % 2 == 0) {
    state_ = finish_state;
  } else {
    state_ = finish_polling_state;
  }

  daemon_ = std::make_unique<std::thread>([this]() { continuousOperation_(); });
}

// Only to be used for tests...
void PbftManager::resumeSingleState() {
  if (!stopped_.load()) daemon_->join();
  stopped_ = false;

  if (step_ == 1) {
    state_ = value_proposal_state;
  } else if (step_ == 2) {
    state_ = filter_state;
  } else if (step_ == 3) {
    state_ = certify_state;
  } else if (step_ % 2 == 0) {
    state_ = finish_state;
  } else {
    state_ = finish_polling_state;
  }

  doNextState_();
}

// Only to be used for tests...
void PbftManager::doNextState_() {
  auto initial_state = state_;

  while (!stopped_ && state_ == initial_state) {
    if (stateOperations_()) {
      continue;
    }

    // PBFT states
    switch (state_) {
      case value_proposal_state:
        proposeBlock_();
        break;
      case filter_state:
        identifyBlock_();
        break;
      case certify_state:
        certifyBlock_();
        break;
      case finish_state:
        firstFinish_();
        break;
      case finish_polling_state:
        secondFinish_();
        break;
      default:
        LOG(log_er_) << "Unknown PBFT state " << state_;
        assert(false);
    }

    setNextState_();
    if (state_ != initial_state) {
      return;
    }
    sleep_();
  }
}

void PbftManager::continuousOperation_() {
  while (!stopped_) {
    if (stateOperations_()) {
      continue;
    }

    // PBFT states
    switch (state_) {
      case value_proposal_state:
        proposeBlock_();
        break;
      case filter_state:
        identifyBlock_();
        break;
      case certify_state:
        certifyBlock_();
        break;
      case finish_state:
        firstFinish_();
        break;
      case finish_polling_state:
        secondFinish_();
        break;
      default:
        LOG(log_er_) << "Unknown PBFT state " << state_;
        assert(false);
    }

    setNextState_();
    sleep_();
  }
}

std::pair<bool, uint64_t> PbftManager::getDagBlockPeriod(blk_hash_t const &hash) {
  std::pair<bool, uint64_t> res;
  auto value = db_->getDagBlockPeriod(hash);
  if (value == nullptr) {
    res.first = false;
  } else {
    res.first = true;
    res.second = value->first;
  }
  return res;
}

uint64_t PbftManager::getPbftPeriod() const { return pbft_chain_->getPbftChainSize() + 1; }

uint64_t PbftManager::getPbftRound() const { return round_; }

std::pair<uint64_t, uint64_t> PbftManager::getPbftRoundAndPeriod() const { return {getPbftRound(), getPbftPeriod()}; }

uint64_t PbftManager::getPbftStep() const { return step_; }

void PbftManager::setPbftRound(uint64_t const round) {
  db_->savePbftMgrField(PbftMgrRoundStep::PbftRound, round);
  round_ = round;
}

void PbftManager::waitForPeriodFinalization() {
  do {
    // we need to be sure we finalized at least block block with num lower by delegation_delay
    if (pbft_chain_->getPbftChainSize() <= final_chain_->last_block_number() + final_chain_->delegation_delay()) {
      break;
    }
    thisThreadSleepForMilliSeconds(POLLING_INTERVAL_ms);
  } while (!stopped_);
}

std::optional<uint64_t> PbftManager::getCurrentDposTotalVotesCount() const {
  try {
    return final_chain_->dpos_eligible_total_vote_count(pbft_chain_->getPbftChainSize());
  } catch (state_api::ErrFutureBlock &e) {
    LOG(log_er_) << "Unable to get CurrentDposTotalVotesCount for period: " << pbft_chain_->getPbftChainSize()
                 << ". Period is too far ahead of actual finalized pbft chain size ("
                 << final_chain_->last_block_number() << "). Err msg: " << e.what();
  }

  return {};
}

std::optional<uint64_t> PbftManager::getCurrentNodeVotesCount() const {
  try {
    return final_chain_->dpos_eligible_vote_count(pbft_chain_->getPbftChainSize(), node_addr_);
  } catch (state_api::ErrFutureBlock &e) {
    LOG(log_er_) << "Unable to get CurrentNodeVotesCount for period: " << pbft_chain_->getPbftChainSize()
                 << ". Period is too far ahead of actual finalized pbft chain size ("
                 << final_chain_->last_block_number() << "). Err msg: " << e.what();
  }

  return {};
}

void PbftManager::setPbftStep(size_t const pbft_step) {
  last_step_ = step_;
  db_->savePbftMgrField(PbftMgrRoundStep::PbftStep, pbft_step);
  step_ = pbft_step;

  if (step_ > MAX_STEPS && LAMBDA_backoff_multiple < 8) {
    // Note: We calculate the lambda for a step independently of prior steps
    //       in case missed earlier steps.
    std::uniform_int_distribution<u_long> distribution(0, step_ - MAX_STEPS);
    auto lambda_random_count = distribution(random_engine_);
    LAMBDA_backoff_multiple = 2 * LAMBDA_backoff_multiple;
    LAMBDA_ms = std::min(kMaxLambda, LAMBDA_ms_MIN * (LAMBDA_backoff_multiple + lambda_random_count));
    LOG(log_dg_) << "Surpassed max steps, exponentially backing off lambda to " << LAMBDA_ms << " ms in round "
                 << getPbftRound() << ", step " << step_;
  } else {
    LAMBDA_ms = LAMBDA_ms_MIN;
    LAMBDA_backoff_multiple = 1;
  }
}

void PbftManager::resetStep() {
  last_step_ = step_;
  step_ = 1;
  startingStepInRound_ = 1;

  LAMBDA_ms = LAMBDA_ms_MIN;
  LAMBDA_backoff_multiple = 1;
}

bool PbftManager::tryPushCertVotesBlock() {
  const auto [current_pbft_round, current_pbft_period] = getPbftRoundAndPeriod();

  const auto two_t_plus_one = getPbftTwoTPlusOne(current_pbft_period - 1);
  if (!two_t_plus_one.has_value()) {
    return false;
  }

  auto certified_block =
      vote_mgr_->getTwoTPlusOneVotesBundle(current_pbft_round, current_pbft_period, certify_state, *two_t_plus_one);
  // Not enough cert votes found yet
  if (!certified_block.has_value()) {
    return false;
  }

  LOG(log_dg_) << "Found enough cert votes for PBFT block " << certified_block->voted_block_hash << ", round "
               << current_pbft_round << ", period " << current_pbft_period;

  auto pbft_block =
      proposed_blocks_.getPbftProposedBlock(current_pbft_period, current_pbft_round, certified_block->voted_block_hash);
  if (!pbft_block) {
    LOG(log_er_) << "Cert voted block " << certified_block->voted_block_hash << " not present proposed blocks";
    return false;
  }

  // Push pbft block into chain
  if (!pushCertVotedPbftBlockIntoChain_(pbft_block, std::move(certified_block->votes))) {
    return false;
  }

  duration_ = std::chrono::system_clock::now() - now_;
  auto execute_trxs_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration_).count();
  LOG(log_dg_) << "PBFT block " << certified_block->voted_block_hash << " certified and pushed into chain in round "
               << current_pbft_round << ". Execution time " << execute_trxs_in_ms << " [ms]";
  return true;
}

bool PbftManager::advancePeriod() {
  resetPbftConsensus(1 /* round */);

  // Cleanup previous period votes in vote manager
  const auto new_period = getPbftPeriod();
  vote_mgr_->cleanupVotesByPeriod(new_period);

  // Cleanup proposed blocks
  proposed_blocks_.cleanupProposedPbftBlocksByPeriod(new_period);

  // Cleanup previous round next votes in next votes manager
  next_votes_manager_->clearVotes();

  LOG(log_nf_) << "Period advanced to: " << new_period << ", round reset to 1";

  // Restart while loop...
  return true;
}

bool PbftManager::advanceRound() {
  const auto [current_pbft_round, current_pbft_period] = getPbftRoundAndPeriod();

  const auto two_t_plus_one = getPbftTwoTPlusOne(current_pbft_period - 1);
  if (!two_t_plus_one.has_value()) {
    return false;
  }

  const auto determined_round_with_votes =
      vote_mgr_->determineRoundFromPeriodAndVotes(current_pbft_period, *two_t_plus_one);
  if (!determined_round_with_votes.has_value()) {
    return false;
  }
  assert(determined_round_with_votes->first > current_pbft_round);
  assert(!determined_round_with_votes->second.empty());

  // Reset consensus
  resetPbftConsensus(determined_round_with_votes->first);

  // Move to a new round, cleanup previous round votes in vote manager
  vote_mgr_->cleanupVotesByRound(current_pbft_period, determined_round_with_votes->first);

  // Cleanup proposed blocks for deremined round - 1. We must keep previous round proposed blocks for voting purposes
  if (determined_round_with_votes->first >= 3) {
    proposed_blocks_.cleanupProposedPbftBlocksByRound(current_pbft_period, determined_round_with_votes->first);
  }

  // Cleanup previous round next votes & set new previous round 2t+1 next votes in next vote manager
  next_votes_manager_->updateNextVotes(determined_round_with_votes->second, *two_t_plus_one);

  LOG(log_nf_) << "Round advanced to: " << determined_round_with_votes->first << ", period " << current_pbft_period;

  // Restart while loop...
  return true;
}

void PbftManager::resetPbftConsensus(uint64_t round) {
  LOG(log_dg_) << "Reset PBFT consensus to: period " << getPbftPeriod() << ", round " << round
               << ", step 1, and resetting clock.";
  round_clock_initial_datetime_ = now_;

  // Reset broadcast counters
  broadcast_votes_counter_ = 1;
  rebroadcast_votes_counter_ = 1;

  // Update current round and reset step to 1
  round_ = round;
  resetStep();
  state_ = value_proposal_state;

  // Update in DB first
  auto batch = db_->createWriteBatch();
  db_->addPbftMgrFieldToBatch(PbftMgrRoundStep::PbftRound, round, batch);
  db_->addPbftMgrFieldToBatch(PbftMgrRoundStep::PbftStep, 1, batch);
  db_->addPbftMgrStatusToBatch(PbftMgrStatus::NextVotedNullBlockHash, false, batch);
  db_->addPbftMgrStatusToBatch(PbftMgrStatus::NextVotedSoftValue, false, batch);

  // Reset cert voted block in the new upcoming round
  if (cert_voted_block_for_round_.has_value()) {
    db_->removeCertVotedBlockInRound(batch);
    cert_voted_block_for_round_.reset();
  }

  // Reset soft voted block in the new upcoming round
  if (soft_voted_block_for_round_.has_value()) {
    db_->removeSoftVotedBlockDataInRound(batch);
    soft_voted_block_for_round_.reset();
  }

  db_->commitWriteBatch(batch);

  // reset next voted value since start a new round
  // these are used to prevent voting multiple times while polling through the step
  // under current implementation.
  // TODO: Get rid of this way of doing it!
  next_voted_null_block_hash_ = false;
  next_voted_soft_value_ = false;

  if (executed_pbft_block_) {
    waitForPeriodFinalization();
    db_->savePbftMgrStatus(PbftMgrStatus::ExecutedBlock, false);
    executed_pbft_block_ = false;
  }

  last_step_clock_initial_datetime_ = current_step_clock_initial_datetime_;
  current_step_clock_initial_datetime_ = std::chrono::system_clock::now();
}

void PbftManager::sleep_() {
  now_ = std::chrono::system_clock::now();
  duration_ = now_ - round_clock_initial_datetime_;
  elapsed_time_in_round_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(duration_).count();
  LOG(log_tr_) << "elapsed time in round(ms): " << elapsed_time_in_round_ms_ << ", step " << step_;
  // Add 25ms for practical reality that a thread will not stall for less than 10-25 ms...
  if (next_step_time_ms_ > elapsed_time_in_round_ms_ + 25) {
    auto time_to_sleep_for_ms = next_step_time_ms_ - elapsed_time_in_round_ms_;
    LOG(log_tr_) << "Time to sleep(ms): " << time_to_sleep_for_ms << " in round " << getPbftRound() << ", step "
                 << step_;
    std::unique_lock<std::mutex> lock(stop_mtx_);
    stop_cv_.wait_for(lock, std::chrono::milliseconds(time_to_sleep_for_ms));
  } else {
    LOG(log_tr_) << "Skipping sleep, running late...";
  }
}

void PbftManager::initialState() {
  // Initial PBFT state

  // Time constants...
  LAMBDA_ms = LAMBDA_ms_MIN;

  const auto current_pbft_period = getPbftPeriod();
  const auto current_pbft_round = db_->getPbftMgrField(PbftMgrRoundStep::PbftRound);
  auto current_pbft_step = db_->getPbftMgrField(PbftMgrRoundStep::PbftStep);

  if (current_pbft_round == 1 && current_pbft_step == 1) {
    // Node start from scratch
    state_ = value_proposal_state;
  } else if (current_pbft_step < 4) {
    // Node start from DB, skip step 1 or 2 or 3
    current_pbft_step = 4;
    state_ = finish_state;
  } else if (current_pbft_step % 2 == 0) {
    // Node start from DB in first finishing state
    state_ = finish_state;
  } else if (current_pbft_step % 2 == 1) {
    // Node start from DB in second finishing state
    state_ = finish_polling_state;
  } else {
    LOG(log_er_) << "Unexpected condition at round " << current_pbft_round << " step " << current_pbft_step;
    assert(false);
  }

  // This is used to offset endtime for second finishing step...
  startingStepInRound_ = current_pbft_step;
  setPbftStep(current_pbft_step);
  round_ = current_pbft_round;

  // Process saved soft voted block + votes from db
  if (auto soft_voted_block_data = db_->getSoftVotedBlockDataInRound(); soft_voted_block_data.has_value()) {
    for (const auto &vote : soft_voted_block_data->soft_votes_) {
      vote_mgr_->addVerifiedVote(vote);
    }

    // If there is also actual block, push it into the proposed blocks
    if (soft_voted_block_data->block_) {
      if (proposed_blocks_.pushProposedPbftBlock(soft_voted_block_data->round_, soft_voted_block_data->block_)) {
        LOG(log_nf_) << "Last soft voted block " << soft_voted_block_data->block_->getBlockHash() << " with period "
                     << soft_voted_block_data->block_->getPeriod() << ", round " << soft_voted_block_data->round_
                     << " pushed into proposed blocks";
      }
    }

    // Use period from votes as soft_voted_block_data->block is optional
    const auto votes_period = soft_voted_block_data->soft_votes_[0]->getPeriod();

    // Set soft_voted_block_for_round_ only if round and period match. Note: could differ in edge case when node
    // crashed, new period/round was already saved in db but soft voted block data was not cleared yet
    if (current_pbft_period == votes_period && current_pbft_round == soft_voted_block_data->round_) {
      soft_voted_block_for_round_ = *soft_voted_block_data;
      LOG(log_nf_) << "Init last observed 2t+1 soft voted block to " << soft_voted_block_data->block_hash_
                   << ", period " << current_pbft_period << ", round " << current_pbft_round;
    }
  }

  // Process saved cert voted block from db
  if (auto cert_voted_block_data = db_->getCertVotedBlockInRound(); cert_voted_block_data.has_value()) {
    const auto &[cert_voted_block_round, cert_voted_block] = *cert_voted_block_data;
    if (proposed_blocks_.pushProposedPbftBlock(cert_voted_block_round, cert_voted_block)) {
      LOG(log_nf_) << "Last cert voted block " << cert_voted_block->getBlockHash() << " with period "
                   << cert_voted_block->getPeriod() << ", round " << cert_voted_block_round
                   << " pushed into proposed blocks";
    }

    // Set cert_voted_block_for_round_ only if round and period match. Note: could differ in edge case when node
    // crashed, new period/round was already saved in db but cert voted block was not cleared yet
    if (current_pbft_period == cert_voted_block->getPeriod() && current_pbft_round == cert_voted_block_round) {
      cert_voted_block_for_round_ = cert_voted_block;
      LOG(log_nf_) << "Init last cert voted block in round to " << cert_voted_block->getBlockHash() << ", period "
                   << current_pbft_period << ", round " << current_pbft_round;
    }
  }

  executed_pbft_block_ = db_->getPbftMgrStatus(PbftMgrStatus::ExecutedBlock);
  next_voted_soft_value_ = db_->getPbftMgrStatus(PbftMgrStatus::NextVotedSoftValue);
  next_voted_null_block_hash_ = db_->getPbftMgrStatus(PbftMgrStatus::NextVotedNullBlockHash);

  round_clock_initial_datetime_ = std::chrono::system_clock::now();
  current_step_clock_initial_datetime_ = round_clock_initial_datetime_;
  last_step_clock_initial_datetime_ = current_step_clock_initial_datetime_;
  next_step_time_ms_ = 0;

  waitForPeriodFinalization();

  if (current_pbft_round > 1) {
    // Get next votes for previous round from DB
    auto next_votes_in_previous_round = db_->getPreviousRoundNextVotes();
    if (next_votes_in_previous_round.empty()) {
      LOG(log_er_) << "Cannot get any next votes in previous round " << current_pbft_round - 1 << ". For period "
                   << getPbftPeriod() << " step " << current_pbft_step;
      assert(false);
    }

    const auto two_t_plus_one = getPbftTwoTPlusOne(current_pbft_period - 1);
    assert(two_t_plus_one.has_value());
    next_votes_manager_->updateNextVotes(next_votes_in_previous_round, *two_t_plus_one);
  }

  previous_round_next_voted_value_ = next_votes_manager_->getVotedValue();
  previous_round_next_voted_null_block_hash_ = next_votes_manager_->haveEnoughVotesForNullBlockHash();

  LOG(log_nf_) << "Node initialize at round " << current_pbft_round << ", period " << getPbftPeriod() << ", step "
               << current_pbft_step << ". Previous round has enough next votes for NULL_BLOCK_HASH: " << std::boolalpha
               << previous_round_next_voted_null_block_hash_ << ", voted value "
               << (previous_round_next_voted_value_.has_value() ? previous_round_next_voted_value_->abridged()
                                                                : "no value")
               << ", next votes size in previous round is " << next_votes_manager_->getNextVotesWeight();
}

void PbftManager::setNextState_() {
  switch (state_) {
    case value_proposal_state:
      setFilterState_();
      break;
    case filter_state:
      setCertifyState_();
      break;
    case certify_state:
      if (go_finish_state_) {
        setFinishState_();
      } else {
        next_step_time_ms_ += POLLING_INTERVAL_ms;
      }
      break;
    case finish_state:
      setFinishPollingState_();
      break;
    case finish_polling_state:
      if (loop_back_finish_state_) {
        loopBackFinishState_();
      } else {
        next_step_time_ms_ += POLLING_INTERVAL_ms;
      }
      break;
    default:
      LOG(log_er_) << "Unknown PBFT state " << state_;
      assert(false);
  }
  LOG(log_tr_) << "next step time(ms): " << next_step_time_ms_ << ", step " << step_;
}

void PbftManager::setFilterState_() {
  state_ = filter_state;
  setPbftStep(step_ + 1);
  next_step_time_ms_ = 2 * LAMBDA_ms;
  last_step_clock_initial_datetime_ = current_step_clock_initial_datetime_;
  current_step_clock_initial_datetime_ = std::chrono::system_clock::now();
}

void PbftManager::setCertifyState_() {
  state_ = certify_state;
  setPbftStep(step_ + 1);
  next_step_time_ms_ = 2 * LAMBDA_ms;
  last_step_clock_initial_datetime_ = current_step_clock_initial_datetime_;
  current_step_clock_initial_datetime_ = std::chrono::system_clock::now();
}

void PbftManager::setFinishState_() {
  LOG(log_dg_) << "Will go to first finish State";
  state_ = finish_state;
  setPbftStep(step_ + 1);
  next_step_time_ms_ = 4 * LAMBDA_ms;
  last_step_clock_initial_datetime_ = current_step_clock_initial_datetime_;
  current_step_clock_initial_datetime_ = std::chrono::system_clock::now();
}

void PbftManager::setFinishPollingState_() {
  state_ = finish_polling_state;
  setPbftStep(step_ + 1);
  auto batch = db_->createWriteBatch();
  db_->addPbftMgrStatusToBatch(PbftMgrStatus::NextVotedSoftValue, false, batch);
  db_->addPbftMgrStatusToBatch(PbftMgrStatus::NextVotedNullBlockHash, false, batch);
  db_->commitWriteBatch(batch);
  next_voted_soft_value_ = false;
  next_voted_null_block_hash_ = false;
  last_step_clock_initial_datetime_ = current_step_clock_initial_datetime_;
  current_step_clock_initial_datetime_ = std::chrono::system_clock::now();
}

void PbftManager::loopBackFinishState_() {
  auto round = getPbftRound();
  LOG(log_dg_) << "CONSENSUS debug round " << round << " , step " << step_
               << " | next_voted_soft_value_ = " << next_voted_soft_value_ << " soft_voted_value_for_round = "
               << (soft_voted_block_for_round_.has_value() ? soft_voted_block_for_round_->block_hash_.abridged()
                                                           : "no value")
               << " next_voted_null_block_hash_ = " << next_voted_null_block_hash_ << " cert_voted_value_for_round = "
               << (cert_voted_block_for_round_.has_value() ? (*cert_voted_block_for_round_)->getBlockHash().abridged()
                                                           : "no value")
               << " previous round next voted NULL_BLOCK_HASH = " << std::boolalpha
               << previous_round_next_voted_null_block_hash_ << " previous_round_next_voted_value_ = "
               << (previous_round_next_voted_value_.has_value() ? previous_round_next_voted_value_->abridged()
                                                                : "no value");
  state_ = finish_state;
  setPbftStep(step_ + 1);
  auto batch = db_->createWriteBatch();
  db_->addPbftMgrStatusToBatch(PbftMgrStatus::NextVotedSoftValue, false, batch);
  db_->addPbftMgrStatusToBatch(PbftMgrStatus::NextVotedNullBlockHash, false, batch);
  db_->commitWriteBatch(batch);
  next_voted_soft_value_ = false;
  next_voted_null_block_hash_ = false;
  assert(step_ >= startingStepInRound_);
  next_step_time_ms_ += POLLING_INTERVAL_ms;
  last_step_clock_initial_datetime_ = current_step_clock_initial_datetime_;
  current_step_clock_initial_datetime_ = std::chrono::system_clock::now();
}

void PbftManager::broadcastVotes(bool rebroadcast) {
  auto [round, period] = getPbftRoundAndPeriod();
  auto net = network_.lock();
  if (!net) {
    return;
  }

  if (auto soft_voted_block_data = getTwoTPlusOneSoftVotedBlockData(period, round); soft_voted_block_data.has_value()) {
    net->getSpecificHandler<network::tarcap::VotePacketHandler>()->onNewPbftVotes(
        std::move(soft_voted_block_data->soft_votes_), rebroadcast);
    vote_mgr_->sendRewardVotes(getLastPbftBlockHash(), rebroadcast);
  }

  LOG(log_dg_) << "Broadcast next votes for previous round. In period " << period << ", round " << round << " step "
               << step_;
  net->getSpecificHandler<network::tarcap::VotesSyncPacketHandler>()->broadcastPreviousRoundNextVotesBundle(
      rebroadcast);
}

bool PbftManager::stateOperations_() {
  pushSyncedPbftBlocksIntoChain();

  checkPreviousRoundNextVotedValueChange_();

  now_ = std::chrono::system_clock::now();
  duration_ = now_ - round_clock_initial_datetime_;
  elapsed_time_in_round_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(duration_).count();

  if (elapsed_time_in_round_ms_ / LAMBDA_ms_MIN > kRebroadcastVotesLambdaTime * rebroadcast_votes_counter_) {
    broadcastVotes(true);
    rebroadcast_votes_counter_++;
    // If there was a rebroadcast no need to do next broadcast either
    broadcast_votes_counter_++;
  } else if (elapsed_time_in_round_ms_ / LAMBDA_ms_MIN > kBroadcastVotesLambdaTime * broadcast_votes_counter_) {
    broadcastVotes(false);
    broadcast_votes_counter_++;
  }

  auto [round, period] = getPbftRoundAndPeriod();
  LOG(log_tr_) << "PBFT current round: " << round << ", period: " << period << ", step " << step_;

  // Check if these is already 2t+1 cert votes for some valid block, if so - push it into the chain
  if (tryPushCertVotesBlock()) {
    return true;
  }

  // 2t+1 next votes were seen
  if (advanceRound()) {
    return true;
  }

  // If node is not eligible to vote and create block, always return true so pbft state machine never enters specific
  // consensus steps (propose, soft-vote, cert-vote, next-vote). Nodes that have no delegation should just
  // observe 2t+1 cert votes to move to the next period or 2t+1 next votes to move to the next round
  if (!canParticipateInConsensus(period - 1)) {
    // Check 2t+1 cert/next votes every POLLING_INTERVAL_ms
    std::this_thread::sleep_for(std::chrono::milliseconds(POLLING_INTERVAL_ms));
    return true;
  }

  return false;
}

const std::optional<TwoTPlusOneSoftVotedBlockData> &PbftManager::getTwoTPlusOneSoftVotedBlockData(uint64_t period,
                                                                                                  uint64_t round) {
  // Have 2t+1 soft votes for some block for current round already
  if (soft_voted_block_for_round_.has_value()) {
    // soft_voted_block_for_round_ should be reset every time period or round is incremented and we should never request
    // it with different period or round than what is already saved in cache.
    // If this happens, it is code bug that should be fixed
    assert(period == soft_voted_block_for_round_->soft_votes_[0]->getPeriod());
    assert(round == soft_voted_block_for_round_->round_);

    // In case we dont have full block object yet, try to get it and save into db
    if (!soft_voted_block_for_round_->block_) {
      auto block = proposed_blocks_.getPbftProposedBlock(period, round, soft_voted_block_for_round_->block_hash_);
      if (block) {
        soft_voted_block_for_round_->block_ = std::move(block);
        db_->saveSoftVotedBlockDataInRound(*soft_voted_block_for_round_);
      }
    }
  } else if (const auto two_t_plus_one = getPbftTwoTPlusOne(period - 1); two_t_plus_one.has_value()) {
    // Try to get 2t+1 soft votes for some block
    const auto soft_votes_bundle = vote_mgr_->getTwoTPlusOneVotesBundle(round, period, filter_state, *two_t_plus_one);
    if (soft_votes_bundle.has_value()) {
      TwoTPlusOneSoftVotedBlockData soft_voted_block_data;
      soft_voted_block_data.round_ = round;
      soft_voted_block_data.block_hash_ = soft_votes_bundle->voted_block_hash;
      soft_voted_block_data.soft_votes_ = std::move(soft_votes_bundle->votes);
      soft_voted_block_data.block_ =
          proposed_blocks_.getPbftProposedBlock(period, round, soft_votes_bundle->voted_block_hash);

      db_->saveSoftVotedBlockDataInRound(soft_voted_block_data);
      soft_voted_block_for_round_ = std::move(soft_voted_block_data);
    }
  }

  return soft_voted_block_for_round_;
}

void PbftManager::checkPreviousRoundNextVotedValueChange_() {
  previous_round_next_voted_value_ = next_votes_manager_->getVotedValue();
  previous_round_next_voted_null_block_hash_ = next_votes_manager_->haveEnoughVotesForNullBlockHash();
}

bool PbftManager::placeVote(const std::shared_ptr<Vote> &vote, std::string_view log_vote_id,
                            const std::shared_ptr<PbftBlock> &voted_block) {
  if (!vote_mgr_->addVerifiedVote(vote)) {
    LOG(log_er_) << "Unable to place vote " << vote->getHash().abridged();
    return false;
  }
  db_->saveVerifiedVote(vote);

  gossipNewVote(vote, voted_block);

  LOG(log_nf_) << "Placed " << log_vote_id << " " << vote->getHash() << " for " << vote->getBlockHash()
               << ", vote weight " << *vote->getWeight() << ", round " << vote->getRound() << ", period "
               << vote->getPeriod() << ", step " << vote->getStep();

  return true;
}

bool PbftManager::genAndPlaceProposeVote(const std::shared_ptr<PbftBlock> &proposed_block) {
  const auto [current_pbft_round, current_pbft_period] = getPbftRoundAndPeriod();
  const auto current_pbft_step = getPbftStep();

  if (proposed_block->getPeriod() != current_pbft_period) {
    LOG(log_er_) << "Propose block " << proposed_block->getBlockHash()
                 << " has different period than current pbft period " << current_pbft_period;
    return false;
  }

  auto propose_vote = generateVoteWithWeight(proposed_block->getBlockHash(), propose_vote_type, current_pbft_period,
                                             current_pbft_round, current_pbft_step);
  if (!propose_vote) {
    LOG(log_er_) << "Unable to generate propose vote";
    return false;
  }

  if (!placeVote(propose_vote, "propose vote", proposed_block)) {
    LOG(log_er_) << "Unable place propose vote";
    return false;
  }

  proposed_blocks_.pushProposedPbftBlock(proposed_block, propose_vote);
  vote_mgr_->sendRewardVotes(proposed_block->getPrevBlockHash());

  LOG(log_nf_) << "Placed propose vote " << propose_vote->getHash() << " for proposed block "
               << proposed_block->getBlockHash() << ", vote weight " << *propose_vote->getWeight() << ", period "
               << current_pbft_period << ", round " << current_pbft_round << ", step " << current_pbft_step;

  return true;
}

void PbftManager::gossipNewVote(const std::shared_ptr<Vote> &vote, const std::shared_ptr<PbftBlock> &voted_block) {
  assert(!voted_block || vote->getBlockHash() == voted_block->getBlockHash());

  auto net = network_.lock();
  if (!net) {
    LOG(log_er_) << "Could not obtain net - cannot gossip new vote";
    assert(false);
    return;
  }

  net->getSpecificHandler<network::tarcap::VotePacketHandler>()->onNewPbftVote(vote, voted_block);
}

void PbftManager::proposeBlock_() {
  // Value Proposal
  auto [round, period] = getPbftRoundAndPeriod();
  LOG(log_dg_) << "PBFT value proposal state in round: " << round << ", period: " << period;

  if (round == 1 || previous_round_next_voted_null_block_hash_) {
    if (round > 1) {
      LOG(log_nf_) << "Previous round " << round - 1 << " had next voted NULL_BLOCK_HASH";
    }

    proposed_block_ = proposePbftBlock_();
    if (proposed_block_) {
      genAndPlaceProposeVote(proposed_block_);
    }
    return;
  } else if (previous_round_next_voted_value_.has_value()) {
    // previous_round_next_voted_value_ should never have value for round == 1
    assert(round > 1);

    // Round greater than 1 and next voted some value that is not null block hash
    const auto &next_voted_block_hash = *previous_round_next_voted_value_;

    LOG(log_nf_) << "Previous round " << round - 1 << " next voted block is " << next_voted_block_hash;

    // auto next_voted_block = proposed_blocks_->get ->getUnverifiedPbftBlock(next_voted_block_hash);
    auto next_voted_block = proposed_blocks_.getPbftProposedBlock(period, round - 1, next_voted_block_hash);
    if (!next_voted_block) {
      LOG(log_wr_) << "Unable to find previous round next voted block " << next_voted_block_hash;
      return;
    }

    genAndPlaceProposeVote(next_voted_block);
    return;
  } else {
    LOG(log_er_) << "Previous round " << round - 1 << " doesn't have enough next votes";
    assert(false);
  }
}

void PbftManager::identifyBlock_() {
  // The Filtering Step
  auto [round, period] = getPbftRoundAndPeriod();
  LOG(log_dg_) << "PBFT filtering state in round: " << round << ", period: " << period;

  if (round == 1 || previous_round_next_voted_null_block_hash_) {
    // Identity leader
    if (auto leader_block = identifyLeaderBlock_(round, period); leader_block) {
      assert(leader_block->getPeriod() == period);
      LOG(log_dg_) << "Leader block identified " << leader_block->getBlockHash() << ", round " << round << ", period "
                   << period;

      if (auto vote = generateVoteWithWeight(leader_block->getBlockHash(), soft_vote_type, leader_block->getPeriod(),
                                             round, step_);
          vote) {
        placeVote(vote, "soft vote", leader_block);
      }
    }
  } else if (previous_round_next_voted_value_.has_value()) {
    const auto &next_voted_block_hash = *previous_round_next_voted_value_;

    if (auto vote = generateVoteWithWeight(next_voted_block_hash, soft_vote_type, period, round, step_); vote) {
      auto previous_round_next_voted_block =
          proposed_blocks_.getPbftProposedBlock(period, round - 1, next_voted_block_hash);
      placeVote(vote, "previous round next voted block soft vote", previous_round_next_voted_block);
    }
  }
}

void PbftManager::certifyBlock_() {
  // The Certifying Step
  auto [round, period] = getPbftRoundAndPeriod();
  LOG(log_dg_) << "PBFT certifying state in round: " << round << ", period: " << period;

  go_finish_state_ = elapsed_time_in_round_ms_ > 4 * LAMBDA_ms - POLLING_INTERVAL_ms;

  if (elapsed_time_in_round_ms_ < 2 * LAMBDA_ms) {
    // Should not happen, add log here for safety checking
    LOG(log_er_) << "PBFT Reached step 3 too quickly after only " << elapsed_time_in_round_ms_ << " (ms) in round "
                 << round;
    return;
  }

  if (go_finish_state_) {
    LOG(log_dg_) << "Step 3 expired, will go to step 4 in round " << round;
    return;
  }

  // Already sent cert voted in this round
  if (cert_voted_block_for_round_.has_value()) {
    LOG(log_dg_) << "Already did cert vote in this round " << round;
    return;
  }

  // Get soft voted bock with 2t+1 soft votes
  const auto soft_voted_block_data = getTwoTPlusOneSoftVotedBlockData(period, round);
  if (soft_voted_block_data.has_value() == false) {
    LOG(log_dg_) << "Certify: Not enough soft votes for current round yet. Period " << period << ",  round" << round;
    return;
  }

  if (!soft_voted_block_data->block_) {
    LOG(log_er_) << "Unable to get proposed block " << soft_voted_block_data->block_hash_ << ", period " << period
                 << ", round " << round;
    return;
  }

  if (!pbft_chain_->checkPbftBlockValidation(soft_voted_block_data->block_)) {
    return;
  }

  if (!compareBlocksAndRewardVotes_(soft_voted_block_data->block_)) {
    LOG(log_dg_) << "Incomplete or invalid soft voted block " << soft_voted_block_data->block_->getBlockHash()
                 << ", period " << period << ", round " << round;
    return;
  }

  // generate cert vote
  auto vote = generateVoteWithWeight(soft_voted_block_data->block_->getBlockHash(), cert_vote_type,
                                     soft_voted_block_data->block_->getPeriod(), round, step_);
  if (!vote) {
    LOG(log_dg_) << "Failed to generate cert vote for " << soft_voted_block_data->block_->getBlockHash();
    return;
  }

  if (!placeVote(vote, "cert vote", soft_voted_block_data->block_)) {
    LOG(log_er_) << "Failed to place cert vote for " << soft_voted_block_data->block_->getBlockHash();
    return;
  }

  cert_voted_block_for_round_ = soft_voted_block_data->block_;
  db_->saveCertVotedBlockInRound(round, soft_voted_block_data->block_);
}

void PbftManager::firstFinish_() {
  // Even number steps from 4 are in first finish
  auto [round, period] = getPbftRoundAndPeriod();
  LOG(log_dg_) << "PBFT first finishing state in period " << period << ", round " << round << ", step " << step_;

  if (cert_voted_block_for_round_.has_value()) {
    const auto &cert_voted_block = *cert_voted_block_for_round_;

    // It should never happen that node moved to the next period without cert_voted_block_for_round_ reset
    assert(cert_voted_block->getPeriod() == period);

    if (auto vote = generateVoteWithWeight(cert_voted_block->getBlockHash(), next_vote_type,
                                           cert_voted_block->getPeriod(), round, step_);
        vote) {
      placeVote(vote, "first finish next vote", cert_voted_block);
    }
  } else if (round >= 2 && previous_round_next_voted_null_block_hash_) {
    // Starting value in round 1 is always null block hash... So combined with other condition for next
    // voting null block hash...
    if (auto vote = generateVoteWithWeight(NULL_BLOCK_HASH, next_vote_type, period, round, step_); vote) {
      placeVote(vote, "first finish next vote", nullptr);
    }
  } else {
    // TODO: We should vote for any value that we first saw 2t+1 next votes for in previous round -> in current design
    // we dont know for which value we saw 2t+1 next votes as first so we prefer specific block if possible
    std::pair<blk_hash_t, std::shared_ptr<PbftBlock>> starting_value;
    if (previous_round_next_voted_value_.has_value()) {
      auto block = proposed_blocks_.getPbftProposedBlock(period, round - 1, *previous_round_next_voted_value_);
      starting_value = {*previous_round_next_voted_value_, std::move(block)};
    } else {  // for round == 1, starting value is always NULL_BLOCK_HASH and previous_round_next_voted_null_block_hash_
              // should be == false
      // This should never happen as round >= 2 && previous_round_next_voted_block == NULL_BLOCK_HASH is covered in
      // previous "else if" condition
      assert(!previous_round_next_voted_null_block_hash_);
      starting_value = {NULL_BLOCK_HASH, nullptr};
    }

    if (auto vote = generateVoteWithWeight(starting_value.first, next_vote_type, period, round, step_); vote) {
      placeVote(vote, "starting value first finish next vote", starting_value.second);
    }
  }
}

void PbftManager::secondFinish_() {
  // Odd number steps from 5 are in second finish
  auto [round, period] = getPbftRoundAndPeriod();
  LOG(log_dg_) << "PBFT second finishing state in period " << period << ", round " << round << ", step " << step_;

  assert(step_ >= startingStepInRound_);

  auto elapsed_time_in_step = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now() - current_step_clock_initial_datetime_)
                                  .count();

  // Have 2t+1 soft votes for some block
  if (auto soft_voted_block_data = getTwoTPlusOneSoftVotedBlockData(period, round); soft_voted_block_data.has_value()) {
    LOG(log_dg_) << "Regossip 2t+1 soft votes for " << soft_voted_block_data->block_hash_;

    if (!next_voted_soft_value_) {
      if (auto vote = generateVoteWithWeight(soft_voted_block_data->block_hash_, next_vote_type, period, round, step_);
          vote) {
        // It is ok even if soft_voted_block_data->block == nullptr
        if (placeVote(vote, "second finish vote", soft_voted_block_data->block_)) {
          db_->savePbftMgrStatus(PbftMgrStatus::NextVotedSoftValue, true);
          next_voted_soft_value_ = true;
        }
      }
    }
  }

  if (!next_voted_null_block_hash_ && round >= 2 && previous_round_next_voted_null_block_hash_ &&
      !cert_voted_block_for_round_.has_value()) {
    if (auto vote = generateVoteWithWeight(NULL_BLOCK_HASH, next_vote_type, period, round, step_); vote) {
      if (placeVote(vote, "second finish next vote", nullptr)) {
        db_->savePbftMgrStatus(PbftMgrStatus::NextVotedNullBlockHash, true);
        next_voted_null_block_hash_ = true;
      }
    }
  }

  loop_back_finish_state_ = elapsed_time_in_step > (int64_t)(2 * (LAMBDA_ms - POLLING_INTERVAL_ms));
}

std::shared_ptr<PbftBlock> PbftManager::generatePbftBlock(uint64_t propose_period, const blk_hash_t &prev_blk_hash,
                                                          const blk_hash_t &anchor_hash, const blk_hash_t &order_hash) {
  // Reward votes should only include those reward votes with the same round as the round last pbft block was pushed
  // into chain
  const auto reward_votes = vote_mgr_->getProposeRewardVotes();
  std::vector<vote_hash_t> reward_votes_hashes;
  std::transform(reward_votes.begin(), reward_votes.end(), std::back_inserter(reward_votes_hashes),
                 [](const auto &v) { return v->getHash(); });
  return std::make_shared<PbftBlock>(prev_blk_hash, anchor_hash, order_hash, propose_period, node_addr_, node_sk_,
                                     std::move(reward_votes_hashes));
}

std::shared_ptr<Vote> PbftManager::generateVote(blk_hash_t const &blockhash, PbftVoteTypes type, uint64_t period,
                                                uint64_t round, size_t step) {
  // sortition proof
  VrfPbftSortition vrf_sortition(vrf_sk_, {type, period, round, step});
  return std::make_shared<Vote>(node_sk_, std::move(vrf_sortition), blockhash);
}

std::pair<bool, std::string> PbftManager::validateVote(const std::shared_ptr<Vote> &vote) const {
  const uint64_t vote_period = vote->getPeriod();

  // Validate vote against dpos contract
  try {
    const auto pk = key_manager_->get(vote_period - 1, vote->getVoterAddr());
    if (!pk) {
      std::stringstream err;
      err << "No vrf key mapped for vote author " << vote->getVoterAddr();
      return {false, err.str()};
    }

    const uint64_t voter_dpos_votes_count =
        final_chain_->dpos_eligible_vote_count(vote_period - 1, vote->getVoterAddr());
    const uint64_t total_dpos_votes_count = final_chain_->dpos_eligible_total_vote_count(vote_period - 1);
    const uint64_t pbft_sortition_threshold = getPbftSortitionThreshold(total_dpos_votes_count, vote->getType());

    vote->validate(voter_dpos_votes_count, total_dpos_votes_count, pbft_sortition_threshold, *pk);
  } catch (state_api::ErrFutureBlock &e) {
    std::stringstream err;
    err << "Unable to validate vote " << vote->getHash() << " against dpos contract. It's period (" << vote_period
        << ") is too far ahead of actual finalized pbft chain size (" << final_chain_->last_block_number()
        << "). Err msg: " << e.what();

    return {false, err.str()};
  } catch (const std::logic_error &e) {
    std::stringstream err;
    err << "Vote " << vote->getHash() << " validation failed. Err: " << e.what();

    return {false, err.str()};
  }

  return {true, ""};
}

void PbftManager::processProposedBlock(const std::shared_ptr<PbftBlock> &proposed_block,
                                       const std::shared_ptr<Vote> &propose_vote) {
  if (proposed_blocks_.isInProposedBlocks(propose_vote->getPeriod(), propose_vote->getRound(),
                                          propose_vote->getBlockHash())) {
    return;
  }

  proposed_blocks_.pushProposedPbftBlock(proposed_block, propose_vote);
}

uint64_t PbftManager::getPbftSortitionThreshold(uint64_t total_dpos_votes_count, PbftVoteTypes vote_type) const {
  switch (vote_type) {
    case PbftVoteTypes::propose_vote_type:
      return std::min<uint64_t>(NUMBER_OF_PROPOSERS, total_dpos_votes_count);
    case PbftVoteTypes::soft_vote_type:
    case PbftVoteTypes::cert_vote_type:
    case PbftVoteTypes::next_vote_type:
    default:
      return std::min<uint64_t>(COMMITTEE_SIZE, total_dpos_votes_count);
  }
}

std::optional<uint64_t> PbftManager::getPbftTwoTPlusOne(uint64_t pbft_period) const {
  // Check cache first
  {
    std::shared_lock lock(current_two_t_plus_one_mutex_);
    if (pbft_period == current_two_t_plus_one_.first && current_two_t_plus_one_.second) {
      return current_two_t_plus_one_.second;
    }
  }

  uint64_t total_dpos_votes_count = 0;
  try {
    total_dpos_votes_count = final_chain_->dpos_eligible_total_vote_count(pbft_period);
  } catch (state_api::ErrFutureBlock &e) {
    LOG(log_er_) << "Unable to calculate 2t + 1 for period: " << pbft_period
                 << ". Period is too far ahead of actual finalized pbft chain size ("
                 << final_chain_->last_block_number() << "). Err msg: " << e.what();
    return {};
  }

  const auto two_t_plus_one =
      getPbftSortitionThreshold(total_dpos_votes_count, PbftVoteTypes::cert_vote_type) * 2 / 3 + 1;

  // Cache is only for current pbft period
  if (pbft_period == pbft_chain_->getPbftChainSize()) {
    std::unique_lock lock(current_two_t_plus_one_mutex_);
    current_two_t_plus_one_ = std::make_pair(pbft_period, two_t_plus_one);
  }

  return two_t_plus_one;
}

std::shared_ptr<Vote> PbftManager::generateVoteWithWeight(taraxa::blk_hash_t const &blockhash, PbftVoteTypes vote_type,
                                                          uint64_t period, uint64_t round, size_t step) {
  const auto current_pbft_period = getPbftPeriod();
  if (period != current_pbft_period) {
    LOG(log_er_) << "Unable to generate vote for block " << blockhash << ", period " << period << ", round " << round
                 << ", step " << step << ", type " << vote_type << " as current pbft period is different "
                 << current_pbft_period;
    return nullptr;
  }

  uint64_t voter_dpos_votes_count = 0;
  uint64_t total_dpos_votes_count = 0;
  uint64_t pbft_sortition_threshold = 0;

  try {
    voter_dpos_votes_count = final_chain_->dpos_eligible_vote_count(period - 1, node_addr_);
    total_dpos_votes_count = final_chain_->dpos_eligible_total_vote_count(period - 1);
    pbft_sortition_threshold = getPbftSortitionThreshold(total_dpos_votes_count, vote_type);

  } catch (state_api::ErrFutureBlock &e) {
    LOG(log_er_) << "Unable to place vote for period: " << period << ", round: " << round << ", step: " << step
                 << ", voted block hash: " << blockhash.abridged() << ". "
                 << "Period is too far ahead of actual finalized pbft chain size (" << final_chain_->last_block_number()
                 << "). Err msg: " << e.what();

    return nullptr;
  }

  if (!voter_dpos_votes_count) {
    // No delegation
    return nullptr;
  }

  auto vote = generateVote(blockhash, vote_type, period, round, step);
  vote->calculateWeight(voter_dpos_votes_count, total_dpos_votes_count, pbft_sortition_threshold);

  if (*vote->getWeight() == 0) {
    // zero weight vote
    return nullptr;
  }

  return vote;
}

blk_hash_t PbftManager::calculateOrderHash(const std::vector<blk_hash_t> &dag_block_hashes) {
  if (dag_block_hashes.empty()) {
    return NULL_BLOCK_HASH;
  }
  dev::RLPStream order_stream(1);
  order_stream.appendList(dag_block_hashes.size());
  for (auto const &blk_hash : dag_block_hashes) {
    order_stream << blk_hash;
  }
  return dev::sha3(order_stream.out());
}

blk_hash_t PbftManager::calculateOrderHash(const std::vector<DagBlock> &dag_blocks) {
  if (dag_blocks.empty()) {
    return NULL_BLOCK_HASH;
  }
  dev::RLPStream order_stream(1);
  order_stream.appendList(dag_blocks.size());
  for (auto const &blk : dag_blocks) {
    order_stream << blk.getHash();
  }
  return dev::sha3(order_stream.out());
}

std::optional<blk_hash_t> findClosestAnchor(const std::vector<blk_hash_t> &ghost,
                                            const std::vector<blk_hash_t> &dag_order, uint32_t included) {
  for (uint32_t i = included; i > 0; i--) {
    if (std::count(ghost.begin(), ghost.end(), dag_order[i - 1])) {
      return dag_order[i - 1];
    }
  }
  return ghost[1];
}

std::shared_ptr<PbftBlock> PbftManager::proposePbftBlock_() {
  const auto [current_pbft_round, current_pbft_period] = getPbftRoundAndPeriod();
  VrfPbftSortition vrf_sortition(vrf_sk_, {propose_vote_type, current_pbft_period, current_pbft_round, 1});

  try {
    const uint64_t voter_dpos_votes_count = final_chain_->dpos_eligible_vote_count(current_pbft_period - 1, node_addr_);
    const uint64_t total_dpos_votes_count = final_chain_->dpos_eligible_total_vote_count(current_pbft_period - 1);
    const uint64_t pbft_sortition_threshold =
        getPbftSortitionThreshold(total_dpos_votes_count, PbftVoteTypes::propose_vote_type);

    if (!voter_dpos_votes_count) {
      LOG(log_er_) << "Unable to propose block for period " << current_pbft_period << ", round " << current_pbft_round
                   << ". Voter dpos vote count is zero";
      return nullptr;
    }

    if (!vrf_sortition.calculateWeight(voter_dpos_votes_count, total_dpos_votes_count, pbft_sortition_threshold,
                                       node_pub_)) {
      LOG(log_dg_) << "Unable to propose block for period " << current_pbft_period << ", round " << current_pbft_round
                   << ". vrf sortition is zero";
      return nullptr;
    }
  } catch (state_api::ErrFutureBlock &e) {
    LOG(log_er_) << "Unable to propose block for period " << current_pbft_period << ", round " << current_pbft_round
                 << ". Period is too far ahead of actual finalized pbft chain size ("
                 << final_chain_->last_block_number() << "). Err msg: " << e.what();
    return nullptr;
  }

  auto last_pbft_block_hash = pbft_chain_->getLastPbftBlockHash();
  auto last_period_dag_anchor_block_hash = pbft_chain_->getLastNonNullPbftBlockAnchor();
  if (last_period_dag_anchor_block_hash == NULL_BLOCK_HASH) {
    last_period_dag_anchor_block_hash = dag_genesis_block_hash_;
  }

  auto ghost = dag_mgr_->getGhostPath(last_period_dag_anchor_block_hash);
  LOG(log_dg_) << "GHOST size " << ghost.size();
  // Looks like ghost never empty, at least include the last period dag anchor block
  if (ghost.empty()) {
    LOG(log_dg_) << "GHOST is empty. No new DAG blocks generated, PBFT propose NULL BLOCK HASH anchor";
    return generatePbftBlock(current_pbft_period, last_pbft_block_hash, NULL_BLOCK_HASH, NULL_BLOCK_HASH);
  }

  blk_hash_t dag_block_hash;
  if (ghost.size() <= DAG_BLOCKS_SIZE) {
    // Move back GHOST_PATH_MOVE_BACK DAG blocks for DAG sycning
    auto ghost_index = (ghost.size() < GHOST_PATH_MOVE_BACK + 1) ? 0 : (ghost.size() - 1 - GHOST_PATH_MOVE_BACK);
    while (ghost_index < ghost.size() - 1) {
      if (ghost[ghost_index] != last_period_dag_anchor_block_hash) {
        break;
      }
      ghost_index += 1;
    }
    dag_block_hash = ghost[ghost_index];
  } else {
    dag_block_hash = ghost[DAG_BLOCKS_SIZE - 1];
  }

  if (dag_block_hash == dag_genesis_block_hash_) {
    LOG(log_dg_) << "No new DAG blocks generated. DAG only has genesis " << dag_block_hash
                 << " PBFT propose NULL BLOCK HASH anchor";
    return generatePbftBlock(current_pbft_period, last_pbft_block_hash, NULL_BLOCK_HASH, NULL_BLOCK_HASH);
  }

  // Compare with last dag block hash. If they are same, which means no new dag blocks generated since last round. In
  // that case PBFT proposer should propose NULL BLOCK HASH anchor as their value
  if (dag_block_hash == last_period_dag_anchor_block_hash) {
    LOG(log_dg_) << "Last period DAG anchor block hash " << dag_block_hash
                 << " No new DAG blocks generated, PBFT propose NULL BLOCK HASH anchor";
    LOG(log_dg_) << "Ghost: " << ghost;
    return generatePbftBlock(current_pbft_period, last_pbft_block_hash, NULL_BLOCK_HASH, NULL_BLOCK_HASH);
  }

  // get DAG block and transaction order
  auto dag_block_order = dag_mgr_->getDagBlockOrder(dag_block_hash, current_pbft_period);

  if (dag_block_order.empty()) {
    LOG(log_er_) << "DAG anchor block hash " << dag_block_hash << " getDagBlockOrder failed in propose";
    assert(false);
  }

  u256 total_weight = 0;
  uint32_t dag_blocks_included = 0;
  for (auto const &blk_hash : dag_block_order) {
    auto dag_blk = dag_mgr_->getDagBlock(blk_hash);
    if (!dag_blk) {
      LOG(log_er_) << "DAG anchor block hash " << dag_block_hash << " getDagBlock failed in propose for block "
                   << blk_hash;
      assert(false);
    }
    const auto &dag_block_weight = dag_blk->getGasEstimation();

    if (total_weight + dag_block_weight > config_.gas_limit) {
      break;
    }
    total_weight += dag_block_weight;
    dag_blocks_included++;
  }

  if (dag_blocks_included != dag_block_order.size()) {
    auto closest_anchor = findClosestAnchor(ghost, dag_block_order, dag_blocks_included);
    if (!closest_anchor) {
      LOG(log_er_) << "Can't find closest anchor after block clipping. Ghost: " << ghost << ". Clipped block_order: "
                   << vec_blk_t(dag_block_order.begin(), dag_block_order.begin() + dag_blocks_included);
      assert(false);
    }

    dag_block_hash = *closest_anchor;
    dag_block_order = dag_mgr_->getDagBlockOrder(dag_block_hash, current_pbft_period);
  }

  auto order_hash = calculateOrderHash(dag_block_order);
  auto pbft_block_hash = generatePbftBlock(current_pbft_period, last_pbft_block_hash, dag_block_hash, order_hash);
  LOG(log_nf_) << "Proposed Pbft block: " << pbft_block_hash << ". Order hash:" << order_hash
               << ". DAG order for proposed block" << dag_block_order;

  return pbft_block_hash;
}

h256 PbftManager::getProposal(const std::shared_ptr<Vote> &vote) const {
  auto lowest_hash = getVoterIndexHash(vote->getCredential(), vote->getVoter(), 1);
  for (uint64_t i = 2; i <= vote->getWeight(); ++i) {
    auto tmp_hash = getVoterIndexHash(vote->getCredential(), vote->getVoter(), i);
    if (lowest_hash > tmp_hash) {
      lowest_hash = tmp_hash;
    }
  }
  return lowest_hash;
}

std::shared_ptr<PbftBlock> PbftManager::identifyLeaderBlock_(uint64_t round, uint64_t period) {
  LOG(log_tr_) << "Identify leader block, in period " << period << ", round " << round;

  // Get all proposal votes in the round
  auto votes = vote_mgr_->getProposalVotes(period, round);

  // Each leader candidate with <vote_signature_hash, vote>
  std::map<h256, std::shared_ptr<Vote>> leader_candidates;

  for (auto const &v : votes) {
    if (v->getRound() != round) {
      LOG(log_er_) << "Vote round is different than current round " << round << ". Vote " << v;
      continue;
    }

    if (v->getPeriod() != period) {
      LOG(log_er_) << "Vote period is different than wanted new period " << period << ". Vote " << v;
      continue;
    }

    if (v->getStep() != 1) {
      LOG(log_er_) << "Vote step is not 1. Vote " << v;
      continue;
    }

    if (v->getType() != propose_vote_type) {
      LOG(log_er_) << "Vote type is not propose vote type. Vote " << v;
      continue;
    }

    const auto proposed_block_hash = v->getBlockHash();
    if (proposed_block_hash == NULL_BLOCK_HASH) {
      LOG(log_er_) << "Propose block hash should not be NULL. Vote " << v;
      continue;
    }

    leader_candidates[getProposal(v)] = v;
  }

  for (auto const &leader_vote : leader_candidates) {
    const auto proposed_block_hash = leader_vote.second->getBlockHash();

    if (pbft_chain_->findPbftBlockInChain(proposed_block_hash)) {
      continue;
    }

    auto leader_block = proposed_blocks_.getPbftProposedBlock(leader_vote.second->getPeriod(),
                                                              leader_vote.second->getRound(), proposed_block_hash);
    if (!leader_block) {
      LOG(log_er_) << "Unable to find proposed block " << proposed_block_hash;
      continue;
    }

    if (!compareBlocksAndRewardVotes_(leader_block)) {
      LOG(log_er_) << "Incomplete or invalid proposed block " << leader_block->getBlockHash() << ", period " << period
                   << ", round " << round;
      continue;
    }

    if (!pbft_chain_->checkPbftBlockValidation(leader_block)) {
      LOG(log_er_) << "Proposed block " << leader_block->getBlockHash() << " failed validation, period " << period
                   << ", round " << round;
      continue;
    }

    return leader_block;
  }

  // no eligible leader
  return nullptr;
}

bool PbftManager::compareBlocksAndRewardVotes_(const std::shared_ptr<PbftBlock> &pbft_block) {
  if (!pbft_block) {
    return false;
  }
  auto const &pbft_block_hash = pbft_block->getBlockHash();
  // Check reward votes
  if (!vote_mgr_->checkRewardVotes(pbft_block)) {
    LOG(log_er_) << "Failed verifying reward votes for proposed PBFT block " << pbft_block_hash;
    return false;
  }

  auto const &anchor_hash = pbft_block->getPivotDagBlockHash();
  if (anchor_hash == NULL_BLOCK_HASH) {
    return true;
  }

  auto dag_order_it = anchor_dag_block_order_cache_.find(anchor_hash);
  if (dag_order_it != anchor_dag_block_order_cache_.end()) {
    return true;
  }

  auto dag_blocks_order = dag_mgr_->getDagBlockOrder(anchor_hash, pbft_block->getPeriod());
  if (dag_blocks_order.empty()) {
    LOG(log_er_) << "Missing dag blocks for proposed PBFT block " << pbft_block_hash;
    return false;
  }

  auto calculated_order_hash = calculateOrderHash(dag_blocks_order);
  if (calculated_order_hash != pbft_block->getOrderHash()) {
    LOG(log_er_) << "Order hash incorrect. Pbft block: " << pbft_block_hash
                 << ". Order hash: " << pbft_block->getOrderHash() << " . Calculated hash:" << calculated_order_hash
                 << ". Dag order: " << dag_blocks_order;
    return false;
  }

  anchor_dag_block_order_cache_[anchor_hash].reserve(dag_blocks_order.size());
  for (auto const &dag_blk_hash : dag_blocks_order) {
    auto dag_block = dag_mgr_->getDagBlock(dag_blk_hash);
    assert(dag_block);
    anchor_dag_block_order_cache_[anchor_hash].emplace_back(std::move(*dag_block));
  }

  auto last_pbft_block_hash = pbft_chain_->getLastPbftBlockHash();
  if (last_pbft_block_hash) {
    auto prev_pbft_block = pbft_chain_->getPbftBlockInChain(last_pbft_block_hash);
    auto ghost = dag_mgr_->getGhostPath(prev_pbft_block.getPivotDagBlockHash());
    if (ghost.size() > 1 && anchor_hash != ghost[1]) {
      if (!checkBlockWeight(anchor_dag_block_order_cache_[anchor_hash])) {
        LOG(log_er_) << "PBFT block " << pbft_block_hash << " weight exceeded max limit";
        anchor_dag_block_order_cache_.erase(anchor_hash);
        return false;
      }
    }
  }

  return true;
}

bool PbftManager::pushCertVotedPbftBlockIntoChain_(const std::shared_ptr<PbftBlock> &pbft_block,
                                                   std::vector<std::shared_ptr<Vote>> &&current_round_cert_votes) {
  if (!pbft_chain_->checkPbftBlockValidation(pbft_block)) {
    LOG(log_er_) << "Failed pbft chain validation for cert voted block " << pbft_block->getBlockHash()
                 << ", will call sync pbft chain from peers";
    return false;
  }

  if (!compareBlocksAndRewardVotes_(pbft_block)) {
    LOG(log_er_) << "Failed compare DAG blocks or reward votes with cert voted block " << pbft_block->getBlockHash();
    return false;
  }

  PeriodData period_data;
  period_data.pbft_blk = pbft_block;
  if (pbft_block->getPivotDagBlockHash() != NULL_BLOCK_HASH) {
    auto dag_order_it = anchor_dag_block_order_cache_.find(pbft_block->getPivotDagBlockHash());
    assert(dag_order_it != anchor_dag_block_order_cache_.end());
    std::unordered_set<trx_hash_t> trx_set;
    std::vector<trx_hash_t> transactions_to_query;
    period_data.dag_blocks.reserve(dag_order_it->second.size());
    for (auto const &dag_blk : dag_order_it->second) {
      for (auto const &trx_hash : dag_blk.getTrxs()) {
        if (trx_set.insert(trx_hash).second) {
          transactions_to_query.emplace_back(trx_hash);
        }
      }
      period_data.dag_blocks.emplace_back(std::move(dag_blk));
    }
    period_data.transactions = trx_mgr_->getNonfinalizedTrx(transactions_to_query);
  }

  period_data.previous_block_cert_votes = vote_mgr_->getRewardVotesByHashes(period_data.pbft_blk->getRewardVotes());
  if (period_data.previous_block_cert_votes.size() < period_data.pbft_blk->getRewardVotes().size()) {
    LOG(log_er_) << "Missing reward votes in cert voted block " << pbft_block->getBlockHash();
    return false;
  }

  if (!pushPbftBlock_(std::move(period_data), std::move(current_round_cert_votes))) {
    LOG(log_er_) << "Failed push cert voted block " << pbft_block->getBlockHash() << " into PBFT chain";
    return false;
  }

  return true;
}

void PbftManager::pushSyncedPbftBlocksIntoChain() {
  if (auto net = network_.lock()) {
    auto round = getPbftRound();
    sync_queue_.cleanOldData(getPbftPeriod());
    while (periodDataQueueSize() > 0) {
      auto period_data_opt = processPeriodData();
      if (!period_data_opt) continue;
      auto period_data = std::move(*period_data_opt);
      const auto period = period_data.first.pbft_blk->getPeriod();
      auto pbft_block_hash = period_data.first.pbft_blk->getBlockHash();
      LOG(log_nf_) << "Pick pbft block " << pbft_block_hash << " from synced queue in round " << round;

      if (pushPbftBlock_(std::move(period_data.first), std::move(period_data.second))) {
        LOG(log_nf_) << node_addr_ << " push synced PBFT block " << pbft_block_hash << " in period " << period
                     << ", round " << round;
      } else {
        LOG(log_si_) << "Failed push PBFT block " << pbft_block_hash << " into chain";
        break;
      }

      net->setSyncStatePeriod(period);
    }
  }
}

void PbftManager::reorderTransactions(SharedTransactions &transactions) {
  // DAG reordering can cause transactions from same sender to be reordered by nonce. If this is the case only
  // transactions from these accounts are sorted and reordered, all other transactions keep the order
  SharedTransactions ordered_transactions;

  // Account with reverse order nonce, the value in a map is a position of last instance
  // of transaction with this account
  std::unordered_map<addr_t, uint32_t> account_reverse_order;

  // While iterating over transactions, account_nonce will keep the last nonce for the account
  std::unordered_map<addr_t, val_t> account_nonce;
  std::unordered_map<addr_t, std::multimap<val_t, std::shared_ptr<Transaction>>> account_nonce_transactions;

  // Find accounts that need reordering and place in account_reverse_order set
  for (uint32_t i = 0; i < transactions.size(); i++) {
    const auto &t = transactions[i];
    auto ro_it = account_reverse_order.find(t->getSender());
    if (ro_it == account_reverse_order.end()) {
      auto it = account_nonce.find(t->getSender());
      if (it == account_nonce.end() || it->second < t->getNonce()) {
        account_nonce[t->getSender()] = t->getNonce();
      } else if (it->second > t->getNonce()) {
        // Nonce of the transaction is smaller than previous nonce, this account transactions will need reordering
        account_reverse_order.insert({t->getSender(), i});
      }
    } else {
      ro_it->second = i;
    }
  }

  // If account_reverse_order size is 0, there is no need to reorder transactions
  if (account_reverse_order.size() > 0) {
    // Keep the order for all transactions that do not need reordering
    for (uint32_t i = 0; i < transactions.size(); i++) {
      const auto &t = transactions[i];
      auto ro_it = account_reverse_order.find(t->getSender());
      if (ro_it != account_reverse_order.end()) {
        account_nonce_transactions[t->getSender()].insert({t->getNonce(), t});
        if (ro_it->second == i) {
          // This is the last instance of transaction for this account, place all the reordered transactions for this
          // account at this position
          for (const auto &nonce : account_nonce_transactions[t->getSender()]) {
            ordered_transactions.push_back(nonce.second);
          }
        }
      } else {
        ordered_transactions.push_back(t);
      }
    }
    transactions = ordered_transactions;
  }
}

void PbftManager::finalize_(PeriodData &&period_data, std::vector<h256> &&finalized_dag_blk_hashes,
                            bool synchronous_processing) {
  const auto anchor = period_data.pbft_blk->getPivotDagBlockHash();
  reorderTransactions(period_data.transactions);

  auto result = final_chain_->finalize(
      std::move(period_data), std::move(finalized_dag_blk_hashes),
      [this, weak_ptr = weak_from_this(), anchor_hash = anchor, period = period_data.pbft_blk->getPeriod()](
          auto const &, auto &batch) {
        // Update proposal period DAG levels map
        auto ptr = weak_ptr.lock();
        if (!ptr) return;  // it was destroyed

        if (!anchor_hash) {
          // Null anchor don't update proposal period DAG levels map
          return;
        }

        auto anchor = dag_mgr_->getDagBlock(anchor_hash);
        if (!anchor) {
          LOG(log_er_) << "DB corrupted - Cannot find anchor block: " << anchor_hash << " in DB.";
          assert(false);
        }

        db_->addProposalPeriodDagLevelsMapToBatch(anchor->getLevel() + max_levels_per_period_, period, batch);
      });

  if (synchronous_processing) {
    result.wait();
  }
}

bool PbftManager::pushPbftBlock_(PeriodData &&period_data, std::vector<std::shared_ptr<Vote>> &&cert_votes) {
  auto const &pbft_block_hash = period_data.pbft_blk->getBlockHash();
  if (db_->pbftBlockInDb(pbft_block_hash)) {
    LOG(log_nf_) << "PBFT block: " << pbft_block_hash << " in DB already.";
    if (cert_voted_block_for_round_.has_value() && (*cert_voted_block_for_round_)->getBlockHash() == pbft_block_hash) {
      LOG(log_er_) << "Last cert voted value should be NULL_BLOCK_HASH. Block hash "
                   << (*cert_voted_block_for_round_)->getBlockHash() << " has been pushed into chain already";
      assert(false);
    }
    return false;
  }

  assert(cert_votes.empty() == false);
  assert(pbft_block_hash == cert_votes[0]->getBlockHash());

  auto pbft_period = period_data.pbft_blk->getPeriod();
  auto null_anchor = period_data.pbft_blk->getPivotDagBlockHash() == NULL_BLOCK_HASH;

  auto batch = db_->createWriteBatch();

  LOG(log_nf_) << "Storing cert votes of pbft blk " << pbft_block_hash;
  LOG(log_dg_) << "Stored following cert votes:\n" << cert_votes;
  // Update PBFT chain head block
  db_->addPbftHeadToBatch(pbft_chain_->getHeadHash(), pbft_chain_->getJsonStrForBlock(pbft_block_hash, null_anchor),
                          batch);

  vec_blk_t dag_blocks_order;
  dag_blocks_order.reserve(period_data.dag_blocks.size());
  std::transform(period_data.dag_blocks.begin(), period_data.dag_blocks.end(), std::back_inserter(dag_blocks_order),
                 [](const DagBlock &dag_block) { return dag_block.getHash(); });

  db_->savePeriodData(period_data, batch);
  auto reward_votes = vote_mgr_->replaceRewardVotes(cert_votes);
  db_->addLastBlockCertVotesToBatch(cert_votes, reward_votes, batch);

  // pass pbft with dag blocks and transactions to adjust difficulty
  if (period_data.pbft_blk->getPivotDagBlockHash() != NULL_BLOCK_HASH) {
    dag_mgr_->sortitionParamsManager().pbftBlockPushed(period_data, batch,
                                                       pbft_chain_->getPbftChainSizeExcludingEmptyPbftBlocks() + 1);
  }
  {
    // This makes sure that no DAG block or transaction can be added or change state in transaction and dag manager
    // when finalizing pbft block with dag blocks and transactions
    std::unique_lock dag_lock(dag_mgr_->getDagMutex());
    std::unique_lock trx_lock(trx_mgr_->getTransactionsMutex());

    // Commit DB
    db_->commitWriteBatch(batch);

    // Set DAG blocks period
    auto const &anchor_hash = period_data.pbft_blk->getPivotDagBlockHash();
    dag_mgr_->setDagBlockOrder(anchor_hash, pbft_period, dag_blocks_order);

    trx_mgr_->updateFinalizedTransactionsStatus(period_data);

    // update PBFT chain size
    pbft_chain_->updatePbftChain(pbft_block_hash, anchor_hash);
  }

  // anchor_dag_block_order_cache_ is valid in one period, clear when period changes
  anchor_dag_block_order_cache_.clear();

  LOG(log_nf_) << "Pushed new PBFT block " << pbft_block_hash << " into chain. Period: " << pbft_period
               << ", round: " << getPbftRound();

  finalize_(std::move(period_data), std::move(dag_blocks_order));

  db_->savePbftMgrStatus(PbftMgrStatus::ExecutedBlock, true);
  executed_pbft_block_ = true;

  // Advance pbft consensus period
  advancePeriod();

  return true;
}

uint64_t PbftManager::pbftSyncingPeriod() const {
  return std::max(sync_queue_.getPeriod(), pbft_chain_->getPbftChainSize());
}

std::optional<std::pair<PeriodData, std::vector<std::shared_ptr<Vote>>>> PbftManager::processPeriodData() {
  auto [period_data, cert_votes, node_id] = sync_queue_.pop();
  auto pbft_block_hash = period_data.pbft_blk->getBlockHash();
  LOG(log_nf_) << "Pop pbft block " << pbft_block_hash << " from synced queue";

  if (pbft_chain_->findPbftBlockInChain(pbft_block_hash)) {
    LOG(log_dg_) << "PBFT block " << pbft_block_hash << " already present in chain.";
    return std::nullopt;
  }

  auto net = network_.lock();
  assert(net);  // Should never happen

  auto last_pbft_block_hash = pbft_chain_->getLastPbftBlockHash();

  // Check previous hash matches
  if (period_data.pbft_blk->getPrevBlockHash() != last_pbft_block_hash) {
    auto last_pbft_block = pbft_chain_->getPbftBlockInChain(last_pbft_block_hash);
    if (period_data.pbft_blk->getPeriod() <= last_pbft_block.getPeriod()) {
      // Old block in the sync queue
      return std::nullopt;
    }

    LOG(log_er_) << "Invalid PBFT block " << pbft_block_hash
                 << "; prevHash: " << period_data.pbft_blk->getPrevBlockHash() << " from peer " << node_id.abridged()
                 << " received, stop syncing.";
    sync_queue_.clear();
    // Handle malicious peer on network level
    net->getSpecificHandler<network::tarcap::PbftSyncPacketHandler>()->handleMaliciousSyncPeer(node_id);
    return std::nullopt;
  }

  // Check reward votes
  if (!vote_mgr_->checkRewardVotes(period_data.pbft_blk)) {
    LOG(log_er_) << "Failed verifying reward votes. Disconnect malicious peer " << node_id.abridged();
    sync_queue_.clear();
    net->getSpecificHandler<network::tarcap::PbftSyncPacketHandler>()->handleMaliciousSyncPeer(node_id);
    return std::nullopt;
  }

  // Special case when previous block was already in chain so we hit condition
  // pbft_chain_->findPbftBlockInChain(pbft_block_hash) and it's cert votes were not verified here, they are part of
  // vote_manager so we need to replace them as they are not verified period_data structure
  if (period_data.previous_block_cert_votes.size() && !period_data.previous_block_cert_votes.front()->getWeight()) {
    if (auto votes = vote_mgr_->getRewardVotesByHashes(period_data.pbft_blk->getRewardVotes()); votes.size()) {
      if (votes.size() < period_data.pbft_blk->getRewardVotes().size()) {
        LOG(log_er_) << "Failed verifying reward votes. PBFT block " << pbft_block_hash << ".Disconnect malicious peer "
                     << node_id.abridged();
        sync_queue_.clear();
        net->getSpecificHandler<network::tarcap::PbftSyncPacketHandler>()->handleMaliciousSyncPeer(node_id);
        return std::nullopt;
      }
      period_data.previous_block_cert_votes = std::move(votes);
    }
  }

  // Validate cert votes
  if (!validatePbftBlockCertVotes(period_data.pbft_blk, cert_votes)) {
    LOG(log_er_) << "Synced PBFT block " << pbft_block_hash
                 << " doesn't have enough valid cert votes. Clear synced PBFT blocks!";
    sync_queue_.clear();
    net->getSpecificHandler<network::tarcap::PbftSyncPacketHandler>()->handleMaliciousSyncPeer(node_id);
    return std::nullopt;
  }

  // Get all the ordered unique non-finalized transactions which should match period_data.transactions
  std::unordered_set<trx_hash_t> trx_set;
  std::vector<trx_hash_t> transactions_to_query;
  for (auto const &dag_block : period_data.dag_blocks) {
    for (auto const &trx_hash : dag_block.getTrxs()) {
      if (trx_set.insert(trx_hash).second) {
        transactions_to_query.emplace_back(trx_hash);
      }
    }
  }
  auto non_finalized_transactions = trx_mgr_->excludeFinalizedTransactions(transactions_to_query);

  if (non_finalized_transactions.size() != period_data.transactions.size()) {
    LOG(log_er_) << "Synced PBFT block " << pbft_block_hash << " transactions count " << period_data.transactions.size()
                 << " incorrect, expected: " << non_finalized_transactions.size();
    sync_queue_.clear();
    net->getSpecificHandler<network::tarcap::PbftSyncPacketHandler>()->handleMaliciousSyncPeer(node_id);
    return std::nullopt;
  }
  for (uint32_t i = 0; i < non_finalized_transactions.size(); i++) {
    if (non_finalized_transactions[i] != period_data.transactions[i]->getHash()) {
      LOG(log_er_) << "Synced PBFT block " << pbft_block_hash << " transaction mismatch "
                   << non_finalized_transactions[i]
                   << " incorrect, expected: " << period_data.transactions[i]->getHash();
      sync_queue_.clear();
      net->getSpecificHandler<network::tarcap::PbftSyncPacketHandler>()->handleMaliciousSyncPeer(node_id);
      return std::nullopt;
    }
  }

  return std::optional<std::pair<PeriodData, std::vector<std::shared_ptr<Vote>>>>(
      {std::move(period_data), std::move(cert_votes)});
}

bool PbftManager::validatePbftBlockCertVotes(const std::shared_ptr<PbftBlock> pbft_block,
                                             const std::vector<std::shared_ptr<Vote>> &cert_votes) const {
  if (cert_votes.empty()) {
    LOG(log_er_) << "No cert votes provided! The synced PBFT block comes from a malicious player";
    return false;
  }

  size_t votes_weight = 0;
  auto first_vote_round = cert_votes[0]->getRound();
  auto first_vote_period = cert_votes[0]->getPeriod();

  if (pbft_block->getPeriod() != first_vote_period) {
    LOG(log_er_) << "pbft block period " << pbft_block->getPeriod() << " != first_vote_period " << first_vote_period;
    return false;
  }

  for (const auto &v : cert_votes) {
    // Any info is wrong that can determine the synced PBFT block comes from a malicious player
    if (v->getPeriod() != first_vote_period) {
      LOG(log_er_) << "Invalid cert vote " << v->getHash() << " period " << v->getPeriod() << ", PBFT block "
                   << pbft_block->getBlockHash() << ", first_vote_period " << first_vote_period;
      return false;
    }

    if (v->getRound() != first_vote_round) {
      LOG(log_er_) << "Invalid cert vote " << v->getHash() << " round " << v->getRound() << ", PBFT block "
                   << pbft_block->getBlockHash() << ", first_vote_round " << first_vote_round;
      return false;
    }

    if (v->getType() != cert_vote_type) {
      LOG(log_er_) << "Invalid cert vote " << v->getHash() << " type " << v->getType() << ", PBFT block "
                   << pbft_block->getBlockHash();
      return false;
    }

    if (v->getStep() != PbftStates::certify_state) {
      LOG(log_er_) << "Invalid cert vote " << v->getHash() << " step " << v->getStep() << ", PBFT block "
                   << pbft_block->getBlockHash();
      return false;
    }

    if (v->getBlockHash() != pbft_block->getBlockHash()) {
      LOG(log_er_) << "Invalid cert vote " << v->getHash() << " block hash " << v->getBlockHash() << ", PBFT block "
                   << pbft_block->getBlockHash();
      return false;
    }

    if (const auto ret = validateVote(v); !ret.first) {
      LOG(log_er_) << "Cert vote " << v->getHash() << " validation failed. Err: " << ret.second << ", pbft block "
                   << pbft_block->getBlockHash();
      return false;
    }

    assert(v->getWeight());
    votes_weight += *v->getWeight();
  }

  const auto two_t_plus_one = getPbftTwoTPlusOne(first_vote_period - 1);
  if (!two_t_plus_one.has_value()) {
    return false;
  }

  if (votes_weight < *two_t_plus_one) {
    LOG(log_er_) << "Invalid votes weight " << votes_weight << " < two_t_plus_one " << *two_t_plus_one
                 << ", pbft block " << pbft_block->getBlockHash();
    return false;
  }

  return true;
}

bool PbftManager::canParticipateInConsensus(uint64_t period) const {
  try {
    return final_chain_->dpos_is_eligible(period, node_addr_);
  } catch (state_api::ErrFutureBlock &e) {
    LOG(log_er_) << "Unable to decide if node is consensus node or not for period: " << period
                 << ". Period is too far ahead of actual finalized pbft chain size ("
                 << final_chain_->last_block_number() << "). Err msg: " << e.what()
                 << ". Node is considered as not eligible to participate in consensus for period " << period;
  }

  return false;
}

blk_hash_t PbftManager::lastPbftBlockHashFromQueueOrChain() {
  auto pbft_block = sync_queue_.lastPbftBlock();
  if (pbft_block && pbft_block->getPeriod() >= getPbftPeriod()) {
    return pbft_block->getBlockHash();
  }
  return pbft_chain_->getLastPbftBlockHash();
}

bool PbftManager::periodDataQueueEmpty() const { return sync_queue_.empty(); }

void PbftManager::periodDataQueuePush(PeriodData &&period_data, dev::p2p::NodeID const &node_id,
                                      std::vector<std::shared_ptr<Vote>> &&current_block_cert_votes) {
  const auto period = period_data.pbft_blk->getPeriod();
  if (!sync_queue_.push(std::move(period_data), node_id, pbft_chain_->getPbftChainSize(),
                        std::move(current_block_cert_votes))) {
    LOG(log_er_) << "Trying to push period data with " << period << " period, but current period is "
                 << sync_queue_.getPeriod();
  }
}

size_t PbftManager::periodDataQueueSize() const { return sync_queue_.size(); }

bool PbftManager::checkBlockWeight(const std::vector<DagBlock> &dag_blocks) const {
  u256 total_weight = 0;
  for (const auto &dag_block : dag_blocks) {
    total_weight += dag_block.getGasEstimation();
  }
  if (total_weight > config_.gas_limit) {
    return false;
  }
  return true;
}

blk_hash_t PbftManager::getLastPbftBlockHash() { return pbft_chain_->getLastPbftBlockHash(); }

const ProposedBlocks &PbftManager::getProposedBlocksSt() const { return proposed_blocks_; }

}  // namespace taraxa
