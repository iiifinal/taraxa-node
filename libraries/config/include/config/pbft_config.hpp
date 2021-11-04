#pragma once

#include <json/json.h>
#include <libdevcore/RLP.h>

#include "common/types.hpp"

namespace taraxa {

struct PbftConfig {
  uint32_t lambda_ms_min = 0;
  uint32_t committee_size = 0;
  uint32_t dag_blocks_size = 0;
  uint32_t ghost_path_move_back = 0;
  bool run_count_votes = false;

  dev::bytes rlp() const {
    dev::RLPStream s;
    s.appendList(5);

    s << lambda_ms_min;
    s << committee_size;
    s << dag_blocks_size;
    s << ghost_path_move_back;
    s << run_count_votes;

    return s.out();
  }
};
Json::Value enc_json(PbftConfig const& obj);
void dec_json(Json::Value const& json, PbftConfig& obj);

}  // namespace taraxa
