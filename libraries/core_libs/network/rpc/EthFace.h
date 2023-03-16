/**
 * This file is generated by jsonrpcstub, DO NOT CHANGE IT MANUALLY!
 */

#ifndef JSONRPC_CPP_STUB_TARAXA_NET_ETHFACE_H_
#define JSONRPC_CPP_STUB_TARAXA_NET_ETHFACE_H_

#include <libweb3jsonrpc/ModularServer.h>

namespace taraxa {
namespace net {
class EthFace : public ServerInterface<EthFace> {
 public:
  EthFace() {
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_protocolVersion", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, NULL),
        &taraxa::net::EthFace::eth_protocolVersionI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_coinbase", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_coinbaseI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_gasPrice", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_gasPriceI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_accounts", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_ARRAY, NULL),
                           &taraxa::net::EthFace::eth_accountsI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_blockNumber", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, NULL),
        &taraxa::net::EthFace::eth_blockNumberI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getBalance", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING,
                                              "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_OBJECT, NULL),
                           &taraxa::net::EthFace::eth_getBalanceI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_getStorageAt", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, "param1",
                           jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_STRING, "param3", jsonrpc::JSON_OBJECT, NULL),
        &taraxa::net::EthFace::eth_getStorageAtI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getStorageRoot", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING,
                                              "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getStorageRootI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_getTransactionCount", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, "param1",
                           jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_OBJECT, NULL),
        &taraxa::net::EthFace::eth_getTransactionCountI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getBlockTransactionCountByHash", jsonrpc::PARAMS_BY_POSITION,
                                              jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getBlockTransactionCountByHashI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getBlockTransactionCountByNumber", jsonrpc::PARAMS_BY_POSITION,
                                              jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getBlockTransactionCountByNumberI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getUncleCountByBlockHash", jsonrpc::PARAMS_BY_POSITION,
                                              jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getUncleCountByBlockHashI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getUncleCountByBlockNumber", jsonrpc::PARAMS_BY_POSITION,
                                              jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getUncleCountByBlockNumberI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getCode", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING,
                                              "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_OBJECT, NULL),
                           &taraxa::net::EthFace::eth_getCodeI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_call", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, "param1",
                                              jsonrpc::JSON_OBJECT, "param2", jsonrpc::JSON_OBJECT, NULL),
                           &taraxa::net::EthFace::eth_callI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getBlockByHash", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT,
                                              "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_BOOLEAN, NULL),
                           &taraxa::net::EthFace::eth_getBlockByHashI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getBlockByNumber", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT,
                                              "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_BOOLEAN, NULL),
                           &taraxa::net::EthFace::eth_getBlockByNumberI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getTransactionByHash", jsonrpc::PARAMS_BY_POSITION,
                                              jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getTransactionByHashI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_getTransactionByBlockHashAndIndex", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT,
                           "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_STRING, NULL),
        &taraxa::net::EthFace::eth_getTransactionByBlockHashAndIndexI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_getTransactionByBlockNumberAndIndex", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT,
                           "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_STRING, NULL),
        &taraxa::net::EthFace::eth_getTransactionByBlockNumberAndIndexI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getTransactionReceipt", jsonrpc::PARAMS_BY_POSITION,
                                              jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getTransactionReceiptI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_getUncleByBlockHashAndIndex", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT,
                           "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_STRING, NULL),
        &taraxa::net::EthFace::eth_getUncleByBlockHashAndIndexI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_getUncleByBlockNumberAndIndex", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT,
                           "param1", jsonrpc::JSON_STRING, "param2", jsonrpc::JSON_STRING, NULL),
        &taraxa::net::EthFace::eth_getUncleByBlockNumberAndIndexI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_newFilter", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING,
                                              "param1", jsonrpc::JSON_OBJECT, NULL),
                           &taraxa::net::EthFace::eth_newFilterI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_newBlockFilter", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, NULL),
        &taraxa::net::EthFace::eth_newBlockFilterI);
    this->bindAndAddMethod(
        jsonrpc::Procedure("eth_newPendingTransactionFilter", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING, NULL),
        &taraxa::net::EthFace::eth_newPendingTransactionFilterI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_uninstallFilter", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_BOOLEAN,
                                              "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_uninstallFilterI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getFilterChanges", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_ARRAY,
                                              "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getFilterChangesI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getFilterLogs", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_ARRAY,
                                              "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_getFilterLogsI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_getLogs", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_ARRAY, "param1",
                                              jsonrpc::JSON_OBJECT, NULL),
                           &taraxa::net::EthFace::eth_getLogsI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_sendRawTransaction", jsonrpc::PARAMS_BY_POSITION,
                                              jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_STRING, NULL),
                           &taraxa::net::EthFace::eth_sendRawTransactionI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_syncing", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, NULL),
                           &taraxa::net::EthFace::eth_syncingI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_estimateGas", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING,
                                              "param1", jsonrpc::JSON_OBJECT, NULL),
                           &taraxa::net::EthFace::eth_estimateGasI);
    this->bindAndAddMethod(jsonrpc::Procedure("eth_chainId", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, NULL),
                           &taraxa::net::EthFace::eth_chainIdI);
  }

  inline virtual void eth_protocolVersionI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_protocolVersion();
  }
  inline virtual void eth_coinbaseI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_coinbase();
  }
  inline virtual void eth_gasPriceI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_gasPrice();
  }
  inline virtual void eth_accountsI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_accounts();
  }
  inline virtual void eth_blockNumberI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_blockNumber();
  }
  inline virtual void eth_getBalanceI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getBalance(request[0u].asString(), request[1u]);
  }
  inline virtual void eth_getStorageAtI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getStorageAt(request[0u].asString(), request[1u].asString(), request[2u]);
  }
  inline virtual void eth_getStorageRootI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getStorageRoot(request[0u].asString(), request[1u].asString());
  }
  inline virtual void eth_getTransactionCountI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getTransactionCount(request[0u].asString(), request[1u]);
  }
  inline virtual void eth_getBlockTransactionCountByHashI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getBlockTransactionCountByHash(request[0u].asString());
  }
  inline virtual void eth_getBlockTransactionCountByNumberI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getBlockTransactionCountByNumber(request[0u].asString());
  }
  inline virtual void eth_getUncleCountByBlockHashI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getUncleCountByBlockHash(request[0u].asString());
  }
  inline virtual void eth_getUncleCountByBlockNumberI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getUncleCountByBlockNumber(request[0u].asString());
  }
  inline virtual void eth_getCodeI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getCode(request[0u].asString(), request[1u]);
  }
  inline virtual void eth_callI(const Json::Value &request, Json::Value &response) {
    response = this->eth_call(request[0u], request[1u]);
  }
  inline virtual void eth_getBlockByHashI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getBlockByHash(request[0u].asString(), request[1u].asBool());
  }
  inline virtual void eth_getBlockByNumberI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getBlockByNumber(request[0u].asString(), request[1u].asBool());
  }
  inline virtual void eth_getTransactionByHashI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getTransactionByHash(request[0u].asString());
  }
  inline virtual void eth_getTransactionByBlockHashAndIndexI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getTransactionByBlockHashAndIndex(request[0u].asString(), request[1u].asString());
  }
  inline virtual void eth_getTransactionByBlockNumberAndIndexI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getTransactionByBlockNumberAndIndex(request[0u].asString(), request[1u].asString());
  }
  inline virtual void eth_getTransactionReceiptI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getTransactionReceipt(request[0u].asString());
  }
  inline virtual void eth_getUncleByBlockHashAndIndexI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getUncleByBlockHashAndIndex(request[0u].asString(), request[1u].asString());
  }
  inline virtual void eth_getUncleByBlockNumberAndIndexI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getUncleByBlockNumberAndIndex(request[0u].asString(), request[1u].asString());
  }
  inline virtual void eth_newFilterI(const Json::Value &request, Json::Value &response) {
    response = this->eth_newFilter(request[0u]);
  }
  inline virtual void eth_newBlockFilterI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_newBlockFilter();
  }
  inline virtual void eth_newPendingTransactionFilterI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_newPendingTransactionFilter();
  }
  inline virtual void eth_uninstallFilterI(const Json::Value &request, Json::Value &response) {
    response = this->eth_uninstallFilter(request[0u].asString());
  }
  inline virtual void eth_getFilterChangesI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getFilterChanges(request[0u].asString());
  }
  inline virtual void eth_getFilterLogsI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getFilterLogs(request[0u].asString());
  }
  inline virtual void eth_getLogsI(const Json::Value &request, Json::Value &response) {
    response = this->eth_getLogs(request[0u]);
  }
  inline virtual void eth_sendRawTransactionI(const Json::Value &request, Json::Value &response) {
    response = this->eth_sendRawTransaction(request[0u].asString());
  }
  inline virtual void eth_syncingI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_syncing();
  }
  inline virtual void eth_estimateGasI(const Json::Value &request, Json::Value &response) {
    response = this->eth_estimateGas(request[0u]);
  }
  inline virtual void eth_chainIdI(const Json::Value &request, Json::Value &response) {
    (void)request;
    response = this->eth_chainId();
  }
  virtual std::string eth_protocolVersion() = 0;
  virtual std::string eth_coinbase() = 0;
  virtual std::string eth_gasPrice() = 0;
  virtual Json::Value eth_accounts() = 0;
  virtual std::string eth_blockNumber() = 0;
  virtual std::string eth_getBalance(const std::string &param1, const Json::Value &param2) = 0;
  virtual std::string eth_getStorageAt(const std::string &param1, const std::string &param2,
                                       const Json::Value &param3) = 0;
  virtual std::string eth_getStorageRoot(const std::string &param1, const std::string &param2) = 0;
  virtual std::string eth_getTransactionCount(const std::string &param1, const Json::Value &param2) = 0;
  virtual Json::Value eth_getBlockTransactionCountByHash(const std::string &param1) = 0;
  virtual Json::Value eth_getBlockTransactionCountByNumber(const std::string &param1) = 0;
  virtual Json::Value eth_getUncleCountByBlockHash(const std::string &param1) = 0;
  virtual Json::Value eth_getUncleCountByBlockNumber(const std::string &param1) = 0;
  virtual std::string eth_getCode(const std::string &param1, const Json::Value &param2) = 0;
  virtual std::string eth_call(const Json::Value &param1, const Json::Value &param2) = 0;
  virtual Json::Value eth_getBlockByHash(const std::string &param1, bool param2) = 0;
  virtual Json::Value eth_getBlockByNumber(const std::string &param1, bool param2) = 0;
  virtual Json::Value eth_getTransactionByHash(const std::string &param1) = 0;
  virtual Json::Value eth_getTransactionByBlockHashAndIndex(const std::string &param1, const std::string &param2) = 0;
  virtual Json::Value eth_getTransactionByBlockNumberAndIndex(const std::string &param1, const std::string &param2) = 0;
  virtual Json::Value eth_getTransactionReceipt(const std::string &param1) = 0;
  virtual Json::Value eth_getUncleByBlockHashAndIndex(const std::string &param1, const std::string &param2) = 0;
  virtual Json::Value eth_getUncleByBlockNumberAndIndex(const std::string &param1, const std::string &param2) = 0;
  virtual std::string eth_newFilter(const Json::Value &param1) = 0;
  virtual std::string eth_newBlockFilter() = 0;
  virtual std::string eth_newPendingTransactionFilter() = 0;
  virtual bool eth_uninstallFilter(const std::string &param1) = 0;
  virtual Json::Value eth_getFilterChanges(const std::string &param1) = 0;
  virtual Json::Value eth_getFilterLogs(const std::string &param1) = 0;
  virtual Json::Value eth_getLogs(const Json::Value &param1) = 0;
  virtual std::string eth_sendRawTransaction(const std::string &param1) = 0;
  virtual Json::Value eth_syncing() = 0;
  virtual std::string eth_estimateGas(const Json::Value &param1) = 0;
  virtual Json::Value eth_chainId() = 0;
};

}  // namespace net
}  // namespace taraxa
#endif  // JSONRPC_CPP_STUB_TARAXA_NET_ETHFACE_H_
