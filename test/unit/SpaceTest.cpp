#include "SpaceTest.hpp"

#include "TestAssert.hpp"

#include "oatpp/async/Executor.hpp"
#include "oatpp/macro/component.hpp"

#include "oatpp_sio/sio/space.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using oatpp_sio::sio::Space;
using oatpp_sio::sio::SpaceListener;

namespace {

/** listener that records everything delivered to it (thread safe) */
class TestListener : public SpaceListener {
  mutable std::mutex m_lock;
  std::vector<std::string> m_received;
  std::thread::id m_deliveryThread;

public:
  TestListener(const std::string& id) : SpaceListener(id) {}

  size_t count() const {
    std::lock_guard<std::mutex> guard(m_lock);
    return m_received.size();
  }

  std::string at(size_t i) const {
    std::lock_guard<std::mutex> guard(m_lock);
    return m_received.at(i);
  }

  std::thread::id deliveryThread() const {
    std::lock_guard<std::mutex> guard(m_lock);
    return m_deliveryThread;
  }

  void onSioMessage(std::shared_ptr<Space> space, Ptr sender,
                    oatpp_sio::Message::Ptr msg) override {
    std::lock_guard<std::mutex> guard(m_lock);
    m_deliveryThread = std::this_thread::get_id();
    m_received.push_back(space->id() + ":" + msg->body + ":" + sender->id());
  }
};

/** listener that unsubscribes itself while being notified */
class SelfRemoving : public SpaceListener {
public:
  int delivered = 0;

  SelfRemoving(const std::string& id) : SpaceListener(id) {}

  void onSioMessage(std::shared_ptr<Space> space, Ptr,
                    oatpp_sio::Message::Ptr) override {
    delivered++;
    space->removeListener(id()); // reentrant call into the same space
  }
};

} // namespace

void SpaceTest::onRun() {

  auto space = std::make_shared<Space>("/test");

  auto a = std::make_shared<TestListener>("a");
  auto b = std::make_shared<TestListener>("b");

  // -- empty space -----------------------------------------------------------
  SIO_ASSERT_EQ(space->id(), std::string("/test"));
  SIO_ASSERT_EQ(space->size(), 0);
  SIO_ASSERT(space->getListener("a") == nullptr);

  // -- subscribe -------------------------------------------------------------
  space->addListener(a);
  space->addListener(b);
  SIO_ASSERT_EQ(space->size(), 2);
  SIO_ASSERT(space->getListener("a") != nullptr);
  SIO_ASSERT(space->getListener("b") != nullptr);

  // -- publish ---------------------------------------------------------------
  // delivers to *all* subscribers, the sender included - filtering the sender
  // is the listener's job (see SioAdapter::onSioMessage)
  auto msg = std::make_shared<oatpp_sio::Message>();
  msg->body = "hello";
  space->publish(space, a, msg);

  SIO_ASSERT_EQ(a->count(), size_t(1));
  SIO_ASSERT_EQ(b->count(), size_t(1));
  SIO_ASSERT_EQ(a->at(0), std::string("/test:hello:a"));
  SIO_ASSERT_EQ(b->at(0), std::string("/test:hello:a"));

  // a second message carries its own payload
  msg = std::make_shared<oatpp_sio::Message>();
  msg->body = "world";
  space->publish(space, b, msg);
  SIO_ASSERT_EQ(b->count(), size_t(2));
  SIO_ASSERT_EQ(b->at(1), std::string("/test:world:b"));

  // -- unsubscribe -----------------------------------------------------------
  space->removeListener("a");
  SIO_ASSERT_EQ(space->size(), 1);
  SIO_ASSERT(space->getListener("a") == nullptr);

  space->publish(space, b, msg);
  SIO_ASSERT_EQ(a->count(), size_t(2)); // 'a' got nothing more
  SIO_ASSERT_EQ(b->count(), size_t(3));

  // removing an unknown id is a no-op
  space->removeListener("does-not-exist");
  SIO_ASSERT_EQ(space->size(), 1);

  // -- re-subscribing an id replaces the listener ----------------------------
  {
    auto first = std::make_shared<TestListener>("dup");
    auto second = std::make_shared<TestListener>("dup");
    space->addListener(first);
    space->addListener(second);
    SIO_ASSERT_EQ(space->size(), 2); // 'b' + 'dup'
    SIO_ASSERT(space->getListener("dup") == second);

    space->publish(space, second, msg);
    SIO_ASSERT_EQ(first->count(), size_t(0));
    SIO_ASSERT_EQ(second->count(), size_t(1));
  }

  // -- a listener may unsubscribe while it is being notified ------------------
  // SioAdapter::shutdown() -> Space::removeListener() does exactly this. The
  // subscription lock is not recursive, so publish() must not hold it while
  // calling back into listeners (that deadlocked).
  {
    auto selfRemoving = std::make_shared<SelfRemoving>("self");
    space->addListener(selfRemoving);
    SIO_ASSERT_EQ(space->size(), 3);

    space->publish(space, b, msg);

    SIO_ASSERT_EQ(selfRemoving->delivered, 1);
    SIO_ASSERT(space->getListener("self") == nullptr);
    SIO_ASSERT_EQ(space->size(), 2);
  }

  // -- publishAsync() delivers on the executor, not on the caller -------------
  {
    OATPP_COMPONENT(std::shared_ptr<oatpp::async::Executor>, executor, "ws");

    auto asyncSpace = std::make_shared<Space>("/async");
    auto l1 = std::make_shared<TestListener>("l1");
    auto l2 = std::make_shared<TestListener>("l2");
    asyncSpace->addListener(l1);
    asyncSpace->addListener(l2);

    auto asyncMsg = std::make_shared<oatpp_sio::Message>();
    asyncMsg->body = "async-hello";
    asyncSpace->publishAsync(asyncSpace, l1, asyncMsg);

    bool delivered = false;
    for (int i = 0; i < 200 && !delivered; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      delivered = l1->count() == 1 && l2->count() == 1;
    }

    SIO_ASSERT(delivered);
    SIO_ASSERT_EQ(l1->at(0), std::string("/async:async-hello:l1"));
    // the delivery must not happen on the calling thread - the old
    // implementation did the work in the coroutine constructor
    SIO_ASSERT(l1->deliveryThread() != std::this_thread::get_id());
  }
}
