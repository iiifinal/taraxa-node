/*
 * @Copyright: Taraxa.io
 * @Author: Chia-Chun Lin
 * @Date: 2019-01-14 12:41:38
 * @Last Modified by: Chia-Chun Lin
 * @Last Modified time: 2019-03-15 21:24:27
 */

#include "dag_block.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include "libdevcore/Log.h"
#include "types.hpp"

namespace taraxa {

TEST(uint256_hash_t, clear) {
  std::string str(
      "8f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
  ASSERT_EQ(str.size(), 64);
  uint256_hash_t test(str);
  ASSERT_EQ(test.toString(),
            "8f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
  test.clear();
  ASSERT_EQ(test.toString(),
            "0000000000000000000000000000000000000000000000000000000000000000");
}

TEST(uint256_hash_t, send_receive_one) {
  uint256_hash_t send(
      "8f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");

  std::vector<uint8_t> data;
  {
    vectorstream strm(data);
    write(strm, send);
  }

  ASSERT_EQ(data.size(), 32);
  bufferstream strm2(data.data(), data.size());
  uint256_hash_t recv;
  read(strm2, recv);

  ASSERT_EQ(send, recv);
}

TEST(uint256_hash_t, send_receive_two) {
  using std::string;
  using std::vector;
  vector<uint256_hash_t> outgoings{
      blk_hash_t(
          "8f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
      blk_hash_t(
          "7f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f")};

  vector<uint8_t> data;
  {
    vectorstream strm(data);
    for (auto const& i : outgoings) {
      write(strm, i);
    }
  }
  ASSERT_EQ(data.size(), 32 * 2);

  vector<uint256_hash_t> receivings(2);
  bufferstream strm2(data.data(), data.size());
  for (auto i = 0; i < 2; ++i) {
    uint256_hash_t& recv = receivings[i];
    read(strm2, recv);
  }
  ASSERT_EQ(outgoings, receivings);
}

TEST(uint256_hash_t, send_receive_three) {
  using std::string;
  using std::vector;
  vector<uint256_hash_t> outgoings{
      blk_hash_t(
          "8f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
      blk_hash_t(
          "7f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"),
      blk_hash_t(
          "6f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f")};

  vector<uint8_t> data;
  {
    vectorstream strm(data);
    for (auto const& i : outgoings) {
      write(strm, i);
    }
  }
  ASSERT_EQ(data.size(), 32 * 3);

  vector<uint256_hash_t> receivings(3);
  bufferstream strm2(data.data(), data.size());
  for (auto i = 0; i < 3; ++i) {
    uint256_hash_t& recv = receivings[i];
    read(strm2, recv);
  }
  ASSERT_EQ(outgoings, receivings);
}

TEST(DagBlock, string_format) {
  using std::string;
  DagBlock blk(
      blk_hash_t(
          "1111111111111111111111111111111111111111111111111111111111111111"),
      {blk_hash_t(
           "2222222222222222222222222222222222222222222222222222222222222222"),
       blk_hash_t(
           "3333333333333333333333333333333333333333333333333333333333333333"),
       blk_hash_t(
           "4444444444444444444444444444444444444444444444444444444444444444")},
      {trx_hash_t(
           "5555555555555555555555555555555555555555555555555555555555555555"),
       trx_hash_t(
           "6666666666666666666666666666666666666666666666666666666666666666")},
      sig_t("777777777777777777777777777777777777777777777777777777777777777777"
            "777777"
            "777777777777777777777777777777777777777777777777777777777"),
      blk_hash_t(
          "8888888888888888888888888888888888888888888888888888888888888888"),
      name_t(
          "000000000000000000000000000000000000000000000000000000000000000F"));
  std::string json = blk.getJsonStr();
  std::stringstream ss1, ss2;
  ss1 << blk;
  std::vector<uint8_t> bytes;
  // Need to put a scope of vectorstream, other bytes won't get result.
  {
    vectorstream strm1(bytes);
    blk.serialize(strm1);
  }
  // check stream size
  ASSERT_EQ(bytes.size(), 323);
  bufferstream strm2(bytes.data(), bytes.size());
  DagBlock blk2;
  blk2.deserialize(strm2);
  ss2 << blk2;
  // Compare block content
  ASSERT_EQ(ss1.str(), ss2.str());
  // Compare json output
  ASSERT_EQ(blk.getJsonStr(), blk2.getJsonStr());
}

TEST(BlockQueue, push_and_pop) {
  BlockQueue blk_qu(1024, 2);
  blk_qu.start();
  DagBlock blk1(blk_hash_t(1111),
                {blk_hash_t(222), blk_hash_t(333), blk_hash_t(444)},
                {trx_hash_t(555), trx_hash_t(666)}, sig_t(7777),
                blk_hash_t(888), name_t(999));

  DagBlock blk2(blk_hash_t(21111),
                {blk_hash_t(2222), blk_hash_t(2333), blk_hash_t(2444)},
                {trx_hash_t(2555), trx_hash_t(2666)}, sig_t(27777),
                blk_hash_t(2888), name_t(2999));

  blk_qu.pushUnverifiedBlock(blk1);
  blk_qu.pushUnverifiedBlock(blk2);

  DagBlock blk3 = blk_qu.getVerifiedBlock();
  DagBlock blk4 = blk_qu.getVerifiedBlock();
  EXPECT_EQ(blk1, blk3);
  EXPECT_EQ(blk2, blk4);
}
}  // namespace taraxa
int main(int argc, char** argv) {
  dev::LoggingOptions logOptions;
  logOptions.verbosity = dev::VerbositySilent;
  dev::setupLogging(logOptions);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}