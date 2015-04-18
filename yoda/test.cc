/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>
          (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#include "yoda.h"

#include <string>
#include <atomic>
#include <thread>

#include "../../Bricks/dflags/dflags.h"
#include "../../Bricks/3party/gtest/gtest-main-with-dflags.h"

// TODO(mzhurovich): We'll need it for REST API tests.
// DEFINE_int32(yoda_http_test_port, 8090, "Local port to use for Sherlock unit test.");

using std::string;
using std::atomic_size_t;
using bricks::strings::Printf;

struct IntKey {
  int x;
  IntKey(int x = 0) : x(x) {}
  int operator()() const { return x; }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(x));
  }

  bool operator==(const IntKey& rhs) const { return x == rhs.x; }
  struct HashFunction {
    size_t operator()(const IntKey& k) const { return static_cast<size_t>(k.x); }
  };
};

struct KeyValueEntry {
  IntKey key_;
  double value_;
  // Uncomment the line below to ensure that it doesn't compile.
  // constexpr static bool allow_nonthrowing_get = true;

  KeyValueEntry() = default;
  KeyValueEntry(const int key, const double value) : key_(key), value_(value) {}

  const IntKey& key() const { return key_; }
  void set_key(const IntKey& key) { key_ = key; }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(key_), CEREAL_NVP(value_));
  }
};

struct KeyValueSubscriptionData {
  atomic_size_t seen_;
  string results_;
  KeyValueSubscriptionData() : seen_(0u) {}
};

struct KeyValueAggregateListener {
  KeyValueSubscriptionData& data_;
  size_t max_to_process_ = static_cast<size_t>(-1);

  KeyValueAggregateListener() = delete;
  explicit KeyValueAggregateListener(KeyValueSubscriptionData& data) : data_(data) {}

  KeyValueAggregateListener& SetMax(size_t cap) {
    max_to_process_ = cap;
    return *this;
  }

  bool Entry(const KeyValueEntry& entry, size_t index, size_t total) {
    static_cast<void>(index);
    static_cast<void>(total);
    if (data_.seen_) {
      data_.results_ += ",";
    }
    data_.results_ += Printf("%d=%.2lf", entry.key(), entry.value_);
    ++data_.seen_;
    return data_.seen_ < max_to_process_;
  }

  void Terminate() {
    if (data_.seen_) {
      data_.results_ += ",";
    }
    data_.results_ += "DONE";
  }
};

TEST(Sherlock, NonPolymorphicKeyValueStorage) {
  typedef sherlock::yoda::API<KeyValueEntry> TestAPI;
  TestAPI api("non_polymorphic_yoda");

  // Add the first key-value pair.
  // Use `UnsafeStream()`, since generally the only way to access the underlying stream is to make API calls.
  api.UnsafeStream().Emplace(2, 0.5);

  while (!api.CaughtUp()) {
    // Spin lock, for the purposes of this test.
    // Ensure that the data has reached the the processor that maintains the in-memory state of the API.
  }

  // Future expanded syntax.
  std::future<KeyValueEntry> f1 = api.AsyncGet(TestAPI::T_KEY(2));
  KeyValueEntry r1 = f1.get();
  EXPECT_EQ(2, r1.key()());
  EXPECT_EQ(0.5, r1.value_);

  // Future short syntax.
  EXPECT_EQ(0.5, api.AsyncGet(TestAPI::T_KEY(2)).get().value_);

  // Callback version.
  struct CallbackTest {
    explicit CallbackTest(int key, double value, bool expect_success = true)
        : key(key), value(value), expect_success(expect_success) {}

    void found(const KeyValueEntry& entry) const {
      ASSERT_FALSE(called);
      called = true;
      EXPECT_TRUE(expect_success);
      EXPECT_EQ(key, entry.key()());
      EXPECT_EQ(value, entry.value_);
    }
    void not_found(const IntKey& key) const {
      ASSERT_FALSE(called);
      called = true;
      EXPECT_FALSE(expect_success);
      EXPECT_EQ(this->key, key());
    }
    void added() const {
      ASSERT_FALSE(called);
      called = true;
      EXPECT_TRUE(expect_success);
    }
    void already_exists() const {
      ASSERT_FALSE(called);
      called = true;
      EXPECT_FALSE(expect_success);
    }

    const int key;
    const double value;
    const bool expect_success;
    mutable bool called = false;
  };

  const CallbackTest cbt1(2, 0.5);
  api.AsyncGet(TestAPI::T_KEY(2),
               std::bind(&CallbackTest::found, &cbt1, std::placeholders::_1),
               std::bind(&CallbackTest::not_found, &cbt1, std::placeholders::_1));
  while (!cbt1.called)
    ;

  // Add two more key-value pairs.
  api.UnsafeStream().Emplace(3, 0.33);
  api.UnsafeStream().Emplace(4, 0.25);

  while (api.EntriesSeen() < 3u) {
    // For the purposes of this test: Spin lock to ensure that the listener/MMQ consumer got the data published.
  }

  EXPECT_EQ(0.33, api.AsyncGet(TestAPI::T_KEY(3)).get().value_);
  EXPECT_EQ(0.25, api.Get(TestAPI::T_KEY(4)).value_);

  ASSERT_THROW(api.AsyncGet(TestAPI::T_KEY(5)).get(), TestAPI::T_KEY_NOT_FOUND_EXCEPTION);
  ASSERT_THROW(api.AsyncGet(TestAPI::T_KEY(5)).get(), sherlock::yoda::KeyNotFoundCoverException);
  ASSERT_THROW(api.Get(TestAPI::T_KEY(6)), TestAPI::T_KEY_NOT_FOUND_EXCEPTION);
  ASSERT_THROW(api.Get(TestAPI::T_KEY(6)), sherlock::yoda::KeyNotFoundCoverException);
  const CallbackTest cbt2(7, 0.0, false);
  api.AsyncGet(TestAPI::T_KEY(7),
               std::bind(&CallbackTest::found, &cbt2, std::placeholders::_1),
               std::bind(&CallbackTest::not_found, &cbt2, std::placeholders::_1));
  while (!cbt2.called)
    ;

  // Add three more key-value pairs, this time via the API.
  api.AsyncAdd(KeyValueEntry(5, 0.2)).wait();
  api.Add(KeyValueEntry(6, 0.17));
  const CallbackTest cbt3(7, 0.76);
  api.AsyncAdd(TestAPI::T_ENTRY(7, 0.76),
               std::bind(&CallbackTest::added, &cbt3),
               std::bind(&CallbackTest::already_exists, &cbt3));
  while (!cbt3.called)
    ;

  // Check that default policy doesn't allow overwriting on Add().
  ASSERT_THROW(api.AsyncAdd(KeyValueEntry(5, 1.1)).get(), TestAPI::T_KEY_ALREADY_EXISTS_EXCEPTION);
  ASSERT_THROW(api.AsyncAdd(KeyValueEntry(5, 1.1)).get(), sherlock::yoda::KeyAlreadyExistsCoverException);
  ASSERT_THROW(api.Add(KeyValueEntry(6, 0.28)), TestAPI::T_KEY_ALREADY_EXISTS_EXCEPTION);
  ASSERT_THROW(api.Add(KeyValueEntry(6, 0.28)), sherlock::yoda::KeyAlreadyExistsCoverException);
  const CallbackTest cbt4(7, 0.0, false);
  api.AsyncAdd(TestAPI::T_ENTRY(7, 0.0),
               std::bind(&CallbackTest::added, &cbt4),
               std::bind(&CallbackTest::already_exists, &cbt4));
  while (!cbt4.called)
    ;

  // Thanks to eventual consistency, we don't have to wait until the above calls fully propagate.
  // Even if the next two lines run before the entries are published into the stream,
  // the API will maintain the consistency of its own responses from its own in-memory state.
  EXPECT_EQ(0.20, api.AsyncGet(IntKey(5)).get().value_);
  EXPECT_EQ(0.17, api.Get(IntKey(6)).value_);

  ASSERT_THROW(api.AsyncGet(IntKey(8)).get(), TestAPI::T_KEY_NOT_FOUND_EXCEPTION);
  ASSERT_THROW(api.Get(IntKey(9)), TestAPI::T_KEY_NOT_FOUND_EXCEPTION);

  // Confirm that data updates have been pubished as stream entries as well.
  // This part is important since otherwise the API is no better than a wrapper over a hash map.
  KeyValueSubscriptionData data;
  KeyValueAggregateListener listener(data);
  listener.SetMax(6u);
  api.Subscribe(listener).Join();
  EXPECT_EQ(data.seen_, 6u);
  EXPECT_EQ("2=0.50,3=0.33,4=0.25,5=0.20,6=0.17,7=0.76", data.results_);
}