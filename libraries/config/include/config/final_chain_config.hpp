#pragma once

#include <json/json.h>

#include "common/types.hpp"
#include "config/state_api_config.hpp"

namespace taraxa::final_chain {

struct Config {
  state_api::Config state;
  struct GenesisBlockFields {
    addr_t author;
    uint64_t timestamp = 0;
  } genesis_block_fields;

  bytes rlp() const {
    dev::RLPStream s;
    s.appendList(3);

    state.rlp(s);
    s << genesis_block_fields.author;
    s << genesis_block_fields.timestamp;

    return s.out();
  }
};
Json::Value enc_json(Config const& obj);
void dec_json(Json::Value const& json, Config& obj);
Json::Value enc_json(Config::GenesisBlockFields const& obj);
void dec_json(Json::Value const& json, Config::GenesisBlockFields& obj);

}  // namespace taraxa::final_chain