#pragma once

#include <json/json.h>

#include <functional>
#include <unordered_map>

#include "common/lazy.hpp"
#include "config/final_chain_config.hpp"
#include "config/pbft_config.hpp"
#include "dag/dag_block.hpp"

namespace taraxa {
using std::string;
using std::unordered_map;
using ::taraxa::util::lazy::LazyVal;

struct Genesis {
  uint64_t chain_id = 0;
  DagBlock dag_block;
  SortitionConfig sortition;
  PbftConfig pbft;
  final_chain::Config final_chain;

 private:
  static LazyVal<std::unordered_map<string, Genesis>> const predefined_;
  bytes rlp() const;

 public:
  Genesis() = default;
  Genesis(Genesis&&) = default;
  Genesis(const Genesis&) = default;
  Genesis& operator=(Genesis&&) = default;
  Genesis& operator=(const Genesis&) = default;

  static auto const& predefined(std::string const& name = "default") {
    if (auto i = predefined_->find(name); i != predefined_->end()) {
      return i->second;
    }
    throw std::runtime_error("unknown chain config: " + name);
  }

  h256 getHash() const;
};

Json::Value enc_json(Genesis const& obj);
void dec_json(Json::Value const& json, Genesis& obj);

}  // namespace taraxa
