/**
 * This file is generated by jsonrpcstub, DO NOT CHANGE IT MANUALLY!
 */

#ifndef JSONRPC_CPP_STUB_TARAXA_NET_TESTCLIENT_H_
#define JSONRPC_CPP_STUB_TARAXA_NET_TESTCLIENT_H_

#include <jsonrpccpp/client.h>

namespace taraxa {
namespace net {
class TestClient : public jsonrpc::Client {
 public:
  TestClient(jsonrpc::IClientConnector& conn, jsonrpc::clientVersion_t type = jsonrpc::JSONRPC_CLIENT_V2)
      : jsonrpc::Client(conn, type) {}

  Json::Value insert_dag_block(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("insert_dag_block", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_dag_block(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_dag_block", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value send_coin_transaction(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("send_coin_transaction", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value create_test_coin_transactions(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("create_test_coin_transactions", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_num_proposed_blocks() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_num_proposed_blocks", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_account_address() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_account_address", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_account_balance(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_account_balance", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_node_count() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_node_count", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_peer_count() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_peer_count", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_node_status() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_node_status", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_node_version() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_node_version", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_all_peers() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_all_peers", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_all_nodes() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_all_nodes", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value should_speak(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("should_speak", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value place_vote(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("place_vote", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_votes(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_votes", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value draw_graph(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("draw_graph", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_transaction_count(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_transaction_count", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_executed_trx_count(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_executed_trx_count", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_executed_blk_count(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_executed_blk_count", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_dag_size(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_dag_size", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_dag_blk_count(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_dag_blk_count", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_pbft_chain_size() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_pbft_chain_size", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_pbft_chain_blocks(const Json::Value& param1) throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p.append(param1);
    Json::Value result = this->CallMethod("get_pbft_chain_blocks", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
  Json::Value get_db_stats() throw(jsonrpc::JsonRpcException) {
    Json::Value p;
    p = Json::nullValue;
    Json::Value result = this->CallMethod("get_db_stats", p);
    if (result.isObject())
      return result;
    else
      throw jsonrpc::JsonRpcException(jsonrpc::Errors::ERROR_CLIENT_INVALID_RESPONSE, result.toStyledString());
  }
};

}  // namespace net
}  // namespace taraxa
#endif  // JSONRPC_CPP_STUB_TARAXA_NET_TESTCLIENT_H_
