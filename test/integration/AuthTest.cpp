#include "AuthTest.hpp"

#include "RawHttpClient.hpp"
#include "SioClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include "oatpp_sio/sio/sioServer.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace siotest;
using oatpp_sio::sio::AuthPlugin;
using oatpp_sio::sio::SioServer;
using oatpp_sio::sio::SpaceListener;

namespace {

/**
 * Plugin with a table of refusals, recording what it was asked.
 *
 * It runs on the server's coroutine threads, so the recording is behind a
 * mutex and the plugin itself never asserts - an exception thrown here would
 * surface inside the connection handler, not in the test.
 */
class TablePlugin : public AuthPlugin {
public:
  /** namespaces refused on CONNECT, with the reason to report */
  std::unordered_map<std::string, std::string> deniedConnect;
  /** allow publishes? (mayPublish has no per-namespace table on purpose) */
  bool allowPublish = true;

  bool mayConnect(const std::string& spaceName, const SpaceListener::Ptr& listener,
                  std::string& reason) override {
    {
      std::lock_guard<std::mutex> guard(m_mutex);
      m_connects.push_back(spaceName + "#" + (listener ? listener->id() : "<null>"));
    }
    const auto it = deniedConnect.find(spaceName);
    if (it == deniedConnect.end()) {
      return true;
    }
    reason = it->second;
    return false;
  }

  bool mayPublish(const std::string& spaceName, const SpaceListener::Ptr& listener) override {
    {
      std::lock_guard<std::mutex> guard(m_mutex);
      m_publishes.push_back(spaceName + "#" + (listener ? listener->id() : "<null>"));
    }
    return allowPublish;
  }

  std::vector<std::string> connects() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_connects;
  }

  std::vector<std::string> publishes() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_publishes;
  }

private:
  mutable std::mutex m_mutex;
  std::vector<std::string> m_connects;
  std::vector<std::string> m_publishes;
};

/**
 * Puts the plugin that was in force back when the test is done - also when it
 * threw, because a plugin left installed would change the behaviour of every
 * test after it (they all share one server process).
 */
class PluginGuard {
public:
  explicit PluginGuard(SioServer& server)
      : m_server(server), m_previous(server.authPlugin()) {}
  ~PluginGuard() { m_server.setAuthPlugin(m_previous); }

private:
  SioServer& m_server;
  AuthPlugin::Ptr m_previous;
};

bool sawNamespace(const std::vector<std::string>& calls, const std::string& nsp) {
  for (const auto& call : calls) {
    if (call.size() >= nsp.size() && call.compare(0, nsp.size(), nsp) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

void AuthTest::onRun() {

  auto& srv = SioServer::serverInstance();

  // -- there always is a plugin in force -----------------------------------
  SIO_ASSERT(srv.authPlugin() != nullptr);

  PluginGuard guard(srv);

  auto plugin = std::make_shared<TablePlugin>();
  plugin->deniedConnect["/rooms"] = "rooms need a ticket";
  srv.setAuthPlugin(plugin);

  // -- a refused connect reaches the client as connect_error ---------------
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.post("0/rooms,"));

    const std::string answer = c.poll();
    SIO_ASSERT_MSG(startsWith(answer, "44/rooms,"),
                   std::string("expected a connect_error on /rooms, got: ") + answer);
    SIO_ASSERT(answer.find("rooms need a ticket") != std::string::npos);

    // the plugin's own sid must be the one the client already knows - a
    // plugin cannot tell connections apart otherwise
    const auto connects = plugin->connects();
    SIO_ASSERT(sawNamespace(connects, "/rooms"));
    bool reportedTheRightSid = false;
    for (const auto& call : connects) {
      if (call.find(c.sid()) != std::string::npos) {
        reportedTheRightSid = true;
      }
    }
    SIO_ASSERT(reportedTheRightSid);
  }

  // -- a namespace the plugin allows still works, on the same connection ---
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.sioConnect("/chat"));
    SIO_ASSERT(sawNamespace(plugin->connects(), "/chat"));
  }

  // -- mayPublish=false stops the event, and the reason is not the transport
  {
    PollClient sender(g_testPort);
    SIO_ASSERT(sender.open());
    SIO_ASSERT(sender.sioConnect("/chat"));

    PollClient other(g_testPort);
    SIO_ASSERT(other.open());
    SIO_ASSERT(other.sioConnect("/chat"));

    // positive control first: with the plugin allowing the publish the packet
    // does get through. Without it, the assertion below would also pass on a
    // server that never delivers anything at all.
    SIO_ASSERT(sender.post("2/chat,[\"allowed\",2]"));
    const std::string allowed = other.poll();
    SIO_ASSERT(allowed.find("allowed") != std::string::npos);

    // ... and now the same thing with the plugin saying no. The event is
    // dropped and the connection stays up - the client did nothing wrong at
    // the protocol level, it is just not allowed to say this.
    plugin->allowPublish = false;
    SIO_ASSERT(sender.post("2/chat,[\"blocked\",1]"));

    std::string delivered;
    SIO_ASSERT(other.state(&delivered) == PollClient::ConnState::Alive);
    SIO_ASSERT(delivered.find("blocked") == std::string::npos);
    SIO_ASSERT(sawNamespace(plugin->publishes(), "/chat"));
  }

  // -- a null plugin is not accepted ---------------------------------------
  // Forgetting to configure one must not lock every client out, so the
  // library keeps whatever was in force instead of installing a null.
  {
    srv.setAuthPlugin(nullptr);
    SIO_ASSERT(srv.authPlugin() == plugin);

    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.sioConnect("/chat"));
  }
}
