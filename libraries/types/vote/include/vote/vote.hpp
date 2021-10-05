#pragma once

#include <libdevcore/SHA3.h>
#include <libdevcrypto/Common.h>

#include <string>

#include "common/types.hpp"
#include "common/vrf_wrapper.hpp"

namespace taraxa {

enum PbftVoteTypes : uint8_t { propose_vote_type = 0, soft_vote_type, cert_vote_type, next_vote_type };
struct VrfPbftMsg {
  VrfPbftMsg() = default;
  VrfPbftMsg(PbftVoteTypes type, uint64_t round, size_t step, size_t weighted_index)
      : type(type), round(round), step(step), weighted_index(weighted_index) {}

  std::string toString() const {
    return std::to_string(type) + "_" + std::to_string(round) + "_" + std::to_string(step) + "_" +
           std::to_string(weighted_index);
  }

  bool operator==(VrfPbftMsg const& other) const {
    return type == other.type && round == other.round && step == other.step && weighted_index == other.weighted_index;
  }

  friend std::ostream& operator<<(std::ostream& strm, VrfPbftMsg const& pbft_msg) {
    strm << "  [Vrf Pbft Msg] " << std::endl;
    strm << "    type: " << pbft_msg.type << std::endl;
    strm << "    round: " << pbft_msg.round << std::endl;
    strm << "    step: " << pbft_msg.step << std::endl;
    strm << "    weighted_index: " << pbft_msg.weighted_index << std::endl;
    return strm;
  }

  bytes getRlpBytes() const {
    dev::RLPStream s;
    s.appendList(4);
    s << static_cast<uint8_t>(type);
    s << round;
    s << step;
    s << weighted_index;
    return s.out();
  }

  PbftVoteTypes type;
  uint64_t round;
  size_t step;
  size_t weighted_index;
};

struct VrfPbftSortition : public vrf_wrapper::VrfSortitionBase {
  using vrf_sk_t = vrf_wrapper::vrf_sk_t;
  using vrf_pk_t = vrf_wrapper::vrf_pk_t;
  using vrf_proof_t = vrf_wrapper::vrf_proof_t;
  using vrf_output_t = vrf_wrapper::vrf_output_t;
  using bytes = dev::bytes;
  VrfPbftSortition() = default;
  VrfPbftSortition(vrf_sk_t const& sk, VrfPbftMsg const& pbft_msg)
      : VrfSortitionBase(sk, pbft_msg.getRlpBytes()), pbft_msg(pbft_msg) {}
  explicit VrfPbftSortition(bytes const& rlp);
  bytes getRlpBytes() const;
  bool verify() const { return VrfSortitionBase::verify(pbft_msg.getRlpBytes()); }
  bool operator==(VrfPbftSortition const& other) const {
    return pk == other.pk && pbft_msg == other.pbft_msg && proof == other.proof && output == other.output;
  }
  static inline uint512_t max512bits = std::numeric_limits<uint512_t>::max();
  bool canSpeak(size_t threshold, size_t valid_players) const;
  friend std::ostream& operator<<(std::ostream& strm, VrfPbftSortition const& vrf_sortition) {
    strm << "[VRF sortition] " << std::endl;
    strm << "  pk: " << vrf_sortition.pk << std::endl;
    strm << "  proof: " << vrf_sortition.proof << std::endl;
    strm << "  output: " << vrf_sortition.output << std::endl;
    strm << vrf_sortition.pbft_msg << std::endl;
    return strm;
  }
  VrfPbftMsg pbft_msg;
};

class Vote {
 public:
  using vrf_pk_t = vrf_wrapper::vrf_pk_t;
  Vote() = default;
  Vote(secret_t const& node_sk, VrfPbftSortition const& vrf_sortition, blk_hash_t const& blockhash);

  explicit Vote(dev::RLP const& rlp);
  explicit Vote(bytes const& rlp);
  bool operator==(Vote const& other) const { return rlp() == other.rlp(); }

  void validate(size_t const valid_sortition_players, size_t const sortition_threshold) const;
  vote_hash_t getHash() const { return vote_hash_; }
  public_t getVoter() const {
    if (!cached_voter_) cached_voter_ = dev::recover(vote_signature_, sha3(false));
    return cached_voter_;
  }
  addr_t getVoterAddr() const {
    if (!cached_voter_addr_) cached_voter_addr_ = dev::toAddress(getVoter());
    return cached_voter_addr_;
  }

  bool verifyVrfSortition() const { return vrf_sortition_.verify(); }
  auto getVrfSortition() const { return vrf_sortition_; }
  auto getSortitionProof() const { return vrf_sortition_.proof; }
  auto getCredential() const { return vrf_sortition_.output; }
  sig_t getVoteSignature() const { return vote_signature_; }
  blk_hash_t getBlockHash() const { return blockhash_; }
  PbftVoteTypes getType() const { return vrf_sortition_.pbft_msg.type; }
  uint64_t getRound() const { return vrf_sortition_.pbft_msg.round; }
  size_t getStep() const { return vrf_sortition_.pbft_msg.step; }
  size_t getWeightedIndex() const { return vrf_sortition_.pbft_msg.weighted_index; }
  bytes rlp(bool inc_sig = true) const;
  bool verifyVote() const {
    auto pk = getVoter();
    return !pk.isZero();  // recoverd public key means that it was verified
  }
  bool verifyCanSpeak(size_t threshold, size_t dpos_total_votes_count) const {
    return vrf_sortition_.canSpeak(threshold, dpos_total_votes_count);
  }

  friend std::ostream& operator<<(std::ostream& strm, Vote const& vote) {
    strm << "[Vote] " << std::endl;
    strm << "  vote_hash: " << vote.vote_hash_ << std::endl;
    strm << "  voter: " << vote.getVoter() << std::endl;
    strm << "  vote_signatue: " << vote.vote_signature_ << std::endl;
    strm << "  blockhash: " << vote.blockhash_ << std::endl;
    strm << "  vrf_sorition: " << vote.vrf_sortition_ << std::endl;
    return strm;
  }

 private:
  blk_hash_t sha3(bool inc_sig) const { return dev::sha3(rlp(inc_sig)); }

  vote_hash_t vote_hash_;  // hash of this vote
  blk_hash_t blockhash_;   // Voted PBFT block hash
  sig_t vote_signature_;
  VrfPbftSortition vrf_sortition_;
  mutable public_t cached_voter_;
  mutable addr_t cached_voter_addr_;
};

struct VotesBundle {
  bool enough;
  blk_hash_t voted_block_hash;
  std::vector<std::shared_ptr<Vote>> votes;  // exactly 2t+1 votes

  VotesBundle() : enough(false), voted_block_hash(blk_hash_t(0)) {}
  VotesBundle(bool enough, blk_hash_t const& voted_block_hash, std::vector<std::shared_ptr<Vote>> const& votes)
      : enough(enough), voted_block_hash(voted_block_hash), votes(votes) {}
};

}  // namespace taraxa