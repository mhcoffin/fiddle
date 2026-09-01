#include "RealtimeMpscQueue.h"
#include "RealtimeObjectPublisher.h"
#include "RealtimeReadGuard.h"
#include "RealtimeSpscQueue.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                     \
                << "]: " << #condition << std::endl;                           \
    }                                                                          \
  } while (false)

void testSpscCapacityAndOrder() {
  fiddle::RealtimeSpscQueue<int, 4> queue;
  CHECK(queue.usableCapacity() == 3);
  CHECK(queue.tryPush(10));
  CHECK(queue.tryPush(20));
  CHECK(queue.tryPush(30));
  CHECK(!queue.tryPush(40));

  int value = 0;
  CHECK(queue.tryPop(value) && value == 10);
  CHECK(queue.tryPop(value) && value == 20);
  CHECK(queue.tryPush(40));
  CHECK(queue.tryPop(value) && value == 30);
  CHECK(queue.tryPop(value) && value == 40);
  CHECK(!queue.tryPop(value));
}

void testMpscCapacityAndOrder() {
  fiddle::RealtimeMpscQueue<int, 4> queue;
  CHECK(queue.tryPush(1));
  CHECK(queue.tryPush(2));
  CHECK(queue.tryPush(3));
  CHECK(queue.tryPush(4));
  CHECK(!queue.tryPush(5));

  int value = 0;
  for (int expected = 1; expected <= 4; ++expected)
    CHECK(queue.tryPop(value) && value == expected);
  CHECK(!queue.tryPop(value));
}

void testPublishedPointerReaderLifetime() {
  int oldValue = 1;
  int newValue = 2;
  std::atomic<int *> published{&oldValue};
  std::atomic<uint32_t> readers{0};

  {
    fiddle::RealtimeReadGuard<int> read(published, readers);
    CHECK(read.get() == &oldValue);
    CHECK(readers.load(std::memory_order_acquire) == 1);

    auto *retired = published.exchange(&newValue, std::memory_order_acq_rel);
    CHECK(retired == &oldValue);
    CHECK(read.get() == &oldValue);
    CHECK(readers.load(std::memory_order_acquire) != 0);
  }

  CHECK(readers.load(std::memory_order_acquire) == 0);
  fiddle::RealtimeReadGuard<int> nextRead(published, readers);
  CHECK(nextRead.get() == &newValue);
}

struct MockRuntime {
  explicit MockRuntime(int runtimeId) : id(runtimeId) {}
  ~MockRuntime() { ++destroyed; }

  int id = 0;
  static inline int destroyed = 0;
};

void testRuntimeReplacementWaitsForReaders() {
  MockRuntime::destroyed = 0;
  fiddle::RealtimeObjectPublisher<MockRuntime> publisher;
  publisher.publish(std::make_unique<MockRuntime>(1));

  {
    auto reader = publisher.read();
    CHECK(reader.get() != nullptr && reader.get()->id == 1);
    publisher.publish(std::make_unique<MockRuntime>(2));
    CHECK(!publisher.reclaimRetired());
    CHECK(MockRuntime::destroyed == 0);
    CHECK(reader.get()->id == 1);
  }

  CHECK(publisher.reclaimRetired());
  CHECK(MockRuntime::destroyed == 1);
  auto nextReader = publisher.read();
  CHECK(nextReader.get() != nullptr && nextReader.get()->id == 2);
}

void testGraphReclamationIncludesDependentProcessors() {
  struct MockGraph {
    int generation = 0;
  };

  MockRuntime::destroyed = 0;
  fiddle::RealtimeObjectPublisher<MockGraph> graphs;
  std::vector<std::unique_ptr<MockRuntime>> retiredProcessors;
  graphs.publish(std::make_unique<MockGraph>(MockGraph{1}));

  {
    auto audioRead = graphs.read();
    CHECK(audioRead.get() != nullptr && audioRead.get()->generation == 1);
    graphs.publish(std::make_unique<MockGraph>(MockGraph{2}));
    retiredProcessors.push_back(std::make_unique<MockRuntime>(10));

    std::vector<std::unique_ptr<MockRuntime>> reclaimable;
    CHECK(!graphs.reclaimRetiredWith(
        [&] { reclaimable.swap(retiredProcessors); }, [](MockGraph &) {}));
    CHECK(reclaimable.empty());
    CHECK(MockRuntime::destroyed == 0);
  }

  std::vector<std::unique_ptr<MockRuntime>> reclaimable;
  CHECK(graphs.reclaimRetiredWith([&] { reclaimable.swap(retiredProcessors); },
                                  [](MockGraph &) {}));
  CHECK(reclaimable.size() == 1);
  reclaimable.clear();
  CHECK(MockRuntime::destroyed == 1);
}

void testConcurrentMpscDelivery() {
  constexpr int producerCount = 4;
  constexpr int valuesPerProducer = 20000;
  fiddle::RealtimeMpscQueue<uint32_t, 1024> queue;
  std::atomic<int> producersFinished{0};
  std::vector<std::thread> producers;

  for (int producer = 0; producer < producerCount; ++producer) {
    producers.emplace_back([producer, &queue, &producersFinished] {
      for (int sequence = 0; sequence < valuesPerProducer; ++sequence) {
        const uint32_t value =
            static_cast<uint32_t>(producer * valuesPerProducer + sequence);
        while (!queue.tryPush(value))
          std::this_thread::yield();
      }
      producersFinished.fetch_add(1, std::memory_order_release);
    });
  }

  std::vector<bool> seen(producerCount * valuesPerProducer, false);
  int received = 0;
  while (producersFinished.load(std::memory_order_acquire) != producerCount ||
         received < producerCount * valuesPerProducer) {
    uint32_t value = 0;
    if (queue.tryPop(value)) {
      CHECK(value < seen.size());
      if (value < seen.size()) {
        CHECK(!seen[value]);
        seen[value] = true;
      }
      ++received;
    } else {
      std::this_thread::yield();
    }
  }

  for (auto &producer : producers)
    producer.join();
  CHECK(received == producerCount * valuesPerProducer);
  for (bool delivered : seen)
    CHECK(delivered);
}

} // namespace

int main() {
  testSpscCapacityAndOrder();
  testMpscCapacityAndOrder();
  testPublishedPointerReaderLifetime();
  testRuntimeReplacementWaitsForReaders();
  testGraphReclamationIncludesDependentProcessors();
  testConcurrentMpscDelivery();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
