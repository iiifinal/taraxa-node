#include "graphql/transaction.hpp"

#include <optional>

#include "graphql/account.hpp"
#include "graphql/block.hpp"
#include "graphql/log.hpp"
#include "libdevcore/CommonJS.h"

using namespace std::literals;

namespace graphql::taraxa {

Transaction::Transaction(std::shared_ptr<::taraxa::final_chain::FinalChain> final_chain,
                         std::shared_ptr<::taraxa::TransactionManager> trx_manager,
                         std::shared_ptr<::taraxa::Transaction> transaction) noexcept
    : final_chain_(std::move(final_chain)),
      trx_manager_(std::move(trx_manager)),
      transaction_(std::move(transaction)) {}

response::Value Transaction::getHash() const noexcept { return response::Value(transaction_->getHash().toString()); }

response::Value Transaction::getNonce() const noexcept { return response::Value(dev::toJS(transaction_->getNonce())); }

std::optional<int> Transaction::getIndex() const noexcept { return std::nullopt; }

std::shared_ptr<object::Account> Transaction::getFrom(std::optional<response::Value>&&) const noexcept {
  return std::make_shared<object::Account>(std::make_shared<Account>(final_chain_, transaction_->getSender()));
}

std::shared_ptr<object::Account> Transaction::getTo(std::optional<response::Value>&&) const noexcept {
  if (!transaction_->getReceiver()) return nullptr;
  return std::make_shared<object::Account>(std::make_shared<Account>(final_chain_, *transaction_->getReceiver()));
}

response::Value Transaction::getValue() const noexcept { return response::Value(dev::toJS(transaction_->getValue())); }

response::Value Transaction::getGasPrice() const noexcept {
  return response::Value(dev::toJS(transaction_->getGasPrice()));
}

response::Value Transaction::getGas() const noexcept { return response::Value(dev::toJS(transaction_->getGas())); }

response::Value Transaction::getInputData() const noexcept {
  return response::Value(dev::toJS(transaction_->getData()));
}

std::shared_ptr<object::Block> Transaction::getBlock() const noexcept {
  const auto location = final_chain_->transaction_location(transaction_->getHash());
  if (!location) return nullptr;
  return std::make_shared<object::Block>(
      std::make_shared<Block>(final_chain_, trx_manager_, final_chain_->block_header(location->blk_n)));
}

std::optional<response::Value> Transaction::getStatus() const noexcept { return std::nullopt; }

std::optional<response::Value> Transaction::getGasUsed() const noexcept {
  const auto recipe = final_chain_->transaction_receipt(transaction_->getHash());
  if (!recipe) return std::nullopt;
  return response::Value(dev::toJS(recipe->gas_used));
}

std::optional<response::Value> Transaction::getCumulativeGasUsed() const noexcept {
  const auto recipe = final_chain_->transaction_receipt(transaction_->getHash());
  if (!recipe) return std::nullopt;
  return response::Value(dev::toJS(recipe->cumulative_gas_used));
}

std::shared_ptr<object::Account> Transaction::getCreatedContract(std::optional<response::Value>&&) const noexcept {
  return nullptr;
}

std::optional<std::vector<std::shared_ptr<object::Log>>> Transaction::getLogs() const noexcept {
  std::vector<std::shared_ptr<object::Log>> logs;
  const auto recipe = final_chain_->transaction_receipt(transaction_->getHash());
  if (!recipe) return std::nullopt;
  for (auto& log : recipe->logs)
    logs.push_back(std::make_shared<object::Log>(std::make_shared<Log>(final_chain_, trx_manager_, std::move(log))));
  return logs;
}

response::Value Transaction::getR() const noexcept { return response::Value(dev::toJS(transaction_->getVRS().r)); }

response::Value Transaction::getS() const noexcept { return response::Value(dev::toJS(transaction_->getVRS().s)); }

response::Value Transaction::getV() const noexcept { return response::Value(dev::toJS(transaction_->getVRS().v)); }

}  // namespace graphql::taraxa