#include "RegistryConcurrencyTest.hpp"

#include "TestAssert.hpp"

#include "oatpp_sio/sio/sioServer.hpp"
#include "oatpp_sio/sio/space.hpp"

#include "oatpp_sio/sio/auth.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using oatpp_sio::sio::AuthPlugin;
using oatpp_sio::sio::SioServer;
using oatpp_sio::sio::Space;
using oatpp_sio::sio::SpaceListener;

namespace {

const int kThreads = 8;
const int kRounds = 20;

/** a listener that only remembers whether it got in */
class QuietListener : public SpaceListener {
public:
  std::atomic<int> subscribedCount{0};

  explicit QuietListener(const std::string& id) : SpaceListener(id) {}

  void onSioMessage(std::shared_ptr<Space>, Ptr,
                    oatpp_sio::Message::Ptr) override {}

  void subscribed(std::shared_ptr<Space>) override { subscribedCount++; }
};

std::string uniqueName(const std::string& base) {
  static std::atomic<int> counter{0};
  return base + "-" + std::to_string(++counter);
}

/** puts the auto-create flag back when the test is done, also if it threw */
class AutoCreateGuard {
public:
  AutoCreateGuard(SioServer& server, bool enable)
      : m_server(server), m_previous(server.autoCreateSpacesEnabled()) {
    m_server.setAutoCreateSpaces(enable);
  }
  ~AutoCreateGuard() { m_server.setAutoCreateSpaces(m_previous); }

private:
  SioServer& m_server;
  bool m_previous;
};

/**
 * A plugin that takes a moment to decide, and always says yes.
 *
 * connectToSpace() looks the namespace up, asks the application, and joins -
 * and a plugin is allowed to be slow, which is exactly what makes the gap
 * between "is it there?" and "join it" wide enough for a concurrent drop to
 * drive through. Without this the window is nanoseconds wide and the test
 * catches the bug when it feels like it; with it, the race is offered on a
 * plate every single round.
 */
class SlowPlugin : public AuthPlugin {
public:
  bool mayConnect(const std::string&, const SpaceListener::Ptr&,
                  std::string&) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return true;
  }
};

/** puts the previous auth plugin back when the test is done */
class PluginGuard {
public:
  explicit PluginGuard(SioServer& server)
      : m_server(server), m_previous(server.authPlugin()) {}
  ~PluginGuard() { m_server.setAuthPlugin(m_previous); }

private:
  SioServer& m_server;
  AuthPlugin::Ptr m_previous;
};

}  // namespace

void RegistryConcurrencyTest::onRun() {

  auto& srv = SioServer::serverInstance();

  // -- 1. auto-create race: N threads, one name, exactly one namespace -----
  //
  // Without a lock this is concurrent unordered_map::insert from eight threads:
  // lost entries at best and a corrupted bucket chain at worst. The invariant
  // is exact, so a single lost insert is visible.
  {
    const std::string name = uniqueName("/race");
    AutoCreateGuard autoCreate(srv, true);
    const size_t before = srv.spaceCount();

    std::atomic<int> successes{0};
    std::vector<std::shared_ptr<QuietListener>> listeners;
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++) {
      auto listener =
          std::make_shared<QuietListener>("racer-" + std::to_string(i));
      listeners.push_back(listener);
    }

    for (int i = 0; i < kThreads; i++) {
      threads.emplace_back([&srv, &name, &listeners, &successes, i]() {
        std::string sioId, reason;
        if (srv.connectToSpace(name, listeners[i], sioId, reason)) {
          successes++;
        }
      });
    }
    for (auto& t : threads) {
      t.join();
    }

    SIO_ASSERT_MSG(successes == kThreads,
                   "every connect to an auto-created namespace should succeed, " +
                       std::to_string(successes.load()) + " of " +
                       std::to_string(kThreads) + " did");
    SIO_ASSERT_EQ(srv.spaceCount(), before + 1);
    SIO_ASSERT(srv.findSpace(name) != nullptr);
    SIO_ASSERT_EQ(srv.findSpace(name)->size(), kThreads);
  }

  // -- 2. churn: joins and leaves while another thread reads the registry ---
  //
  // Nothing here asserts a particular interleaving - it asserts that the map
  // survives being read while it is written, and that bookkeeping is exact
  // afterwards: everybody who left is out, and the namespace count is what it
  // was.
  {
    const std::string name = uniqueName("/churn");
    srv.newSpace(name);
    const size_t before = srv.spaceCount();

    std::atomic<int> joined{0};
    std::atomic<int> leaveFailed{0};
    std::atomic<bool> readersDone{false};
    std::atomic<int> readerLookups{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; i++) {
      threads.emplace_back([&srv, &name, &joined, &leaveFailed, i]() {
        for (int round = 0; round < kRounds; round++) {
          auto listener = std::make_shared<QuietListener>(
              "churn-" + std::to_string(i) + "-" + std::to_string(round));
          std::string sioId, reason;
          if (srv.connectToSpace(name, listener, sioId, reason)) {
            joined++;
            // counting rather than asserting: an exception thrown on this
            // thread would take the process down instead of failing the test
            if (!srv.leaveSpace(name, sioId)) {
              leaveFailed++;
            }
          }
        }
      });
    }

    // readers: lookups and counts while the writers are running
    std::vector<std::thread> readers;
    for (int r = 0; r < 2; r++) {
      readers.emplace_back([&srv, &name, &readersDone, &readerLookups]() {
        while (!readersDone) {
          srv.findSpace(name);
          srv.spaceCount();
          readerLookups++;
        }
      });
    }

    for (auto& t : threads) {
      t.join();
    }
    readersDone = true;
    for (auto& t : readers) {
      t.join();
    }

    SIO_ASSERT_EQ(joined.load(), kThreads * kRounds);
    SIO_ASSERT_EQ(leaveFailed.load(), 0);
    SIO_ASSERT(readerLookups.load() > 0);
    SIO_ASSERT_EQ(srv.findSpace(name)->size(), 0);
    SIO_ASSERT_EQ(srv.spaceCount(), before);
  }

  // -- 3. dropSpace() against joins: a namespace with members never goes ----
  //
  // The emptiness check and the erase are one critical section, and a join
  // takes the same lock, so "is anybody in it?" cannot be answered with "no"
  // about a namespace somebody is joining right now. Two invariants follow, and
  // both are checked:
  //
  //   a) if a drop succeeded, nobody joined - members never leave in this
  //      sub-test, so a successful drop means the namespace was still empty;
  //   b) if the server told a client it joined, then the namespace exists and
  //      contains that client - checked by the joiner itself immediately after,
  //      which is what catches a join that landed in a namespace that was
  //      retired between the lookup and the join.
  //
  // Repeated over rounds with the dropper spinning, and with a plugin that
  // keeps every joiner paused between the lookup and the join - a race you only
  // offer once, over a window of nanoseconds, is a race you do not catch.
  const int kDropAttempts = 2000;
  const int kDropRounds = 5;

  PluginGuard slowAuth(srv);
  srv.setAuthPlugin(std::make_shared<SlowPlugin>());

  std::atomic<int> orphanedJoins{0};
  std::atomic<int> impossibleDrops{0};

  for (int round = 0; round < kDropRounds; round++) {
    const std::string name = uniqueName("/drop-race");
    srv.newSpace(name);

    std::atomic<int> joined{0};
    std::atomic<int> drops{0};
    std::atomic<bool> go{false};

    std::vector<std::shared_ptr<QuietListener>> listeners;
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++) {
      listeners.push_back(
          std::make_shared<QuietListener>("droprace-" + std::to_string(i)));
    }

    for (int i = 0; i < kThreads; i++) {
      threads.emplace_back([&srv, &name, &listeners, &joined, &orphanedJoins,
                            &go, i]() {
        while (!go) {
        }
        std::string sioId, reason;
        if (srv.connectToSpace(name, listeners[i], sioId, reason)) {
          joined++;
          // invariant (b): the server said yes, so I must be in a namespace
          // that the server still knows about
          const auto space = srv.findSpace(name);
          if (!space || !space->getListener(listeners[i]->id())) {
            orphanedJoins++;
          }
        }
      });
    }

    std::thread dropper([&srv, &name, &drops, &go]() {
      while (!go) {
      }
      for (int attempt = 0; attempt < kDropAttempts; attempt++) {
        if (srv.dropSpace(name)) {
          drops++;
        }
      }
    });

    go = true;
    dropper.join();
    for (auto& t : threads) {
      t.join();
    }

    // invariant (a): members never leave in this sub-test, so a namespace that
    // had a member could not have been dropped
    if (drops.load() > 0 && joined.load() > 0) {
      impossibleDrops++;
    }

    // clean up for the next round
    for (const auto& listener : listeners) {
      std::string sioId = listener->id();
      srv.leaveSpace(name, sioId);
    }
    srv.dropSpace(name);
  }

  SIO_ASSERT_MSG(orphanedJoins.load() == 0,
                 std::to_string(orphanedJoins.load()) +
                     " connect(s) reported success into a namespace that was "
                     "not registered (or did not contain the listener) - the "
                     "join is not atomic against dropSpace()");
  SIO_ASSERT_MSG(
      impossibleDrops.load() == 0,
      std::to_string(impossibleDrops.load()) + " round(s) where dropSpace() "
      "succeeded on a namespace that also had members - the emptiness check "
      "and the erase are not one critical section");
}
