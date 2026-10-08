#include "AuthPluginTest.hpp"

#include "TestAssert.hpp"

#include "oatpp_sio/sio/auth.hpp"
#include "oatpp_sio/sio/sioServer.hpp"
#include "oatpp_sio/sio/space.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using oatpp_sio::sio::AllowAllAuth;
using oatpp_sio::sio::AuthPlugin;
using oatpp_sio::sio::DenyAllAuth;
using oatpp_sio::sio::SioServer;
using oatpp_sio::sio::Space;
using oatpp_sio::sio::SpaceListener;

namespace {

/** listener that only records how often it was let in */
class NullListener : public SpaceListener {
public:
  int subscribedCount = 0;
  int leftCount = 0;

  explicit NullListener(const std::string& id) : SpaceListener(id) {}

  void onSioMessage(std::shared_ptr<Space>, Ptr,
                    oatpp_sio::Message::Ptr) override {}

  void subscribed(std::shared_ptr<Space>) override { subscribedCount++; }
  void left(std::shared_ptr<Space>) override { leftCount++; }
};

/** plugin with an allow-list, recording every call it received */
class ListPlugin : public AuthPlugin {
public:
  std::vector<std::string> allowed;
  /** what to report on a refusal; left empty on purpose in one case below */
  std::string refusal = "not on the list";

  std::vector<std::string> connectCalls;
  std::vector<std::string> publishCalls;

  bool mayConnect(const std::string& spaceName,
                  const SpaceListener::Ptr& listener,
                  std::string& reason) override {
    connectCalls.push_back(spaceName + "|" + (listener ? listener->id() : "<null>"));
    if (std::find(allowed.begin(), allowed.end(), spaceName) != allowed.end()) {
      return true;
    }
    reason = refusal;
    return false;
  }

  bool mayPublish(const std::string& spaceName,
                  const SpaceListener::Ptr& listener) override {
    publishCalls.push_back(spaceName + "|" + (listener ? listener->id() : "<null>"));
    return allowPublish;
  }

  bool allowPublish = true;
};

std::string uniqueName(const std::string& base) {
  static int counter = 0;
  return base + "-" + std::to_string(++counter);
}

/** puts the plugin back the way it was, also when the test threw */
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

void AuthPluginTest::onRun() {

  auto& srv = SioServer::serverInstance();

  // -- the default is allow-everything, and there always is one -------------
  SIO_ASSERT(srv.authPlugin() != nullptr);
  SIO_ASSERT(std::dynamic_pointer_cast<AllowAllAuth>(srv.authPlugin()) != nullptr);

  PluginGuard guard(srv);

  const std::string open = uniqueName("/auth-open");
  const std::string closed = uniqueName("/auth-closed");
  SIO_ASSERT(srv.newSpace(open) != nullptr);
  SIO_ASSERT(srv.newSpace(closed) != nullptr);

  // -- a plugin can refuse, and say why ------------------------------------
  {
    auto plugin = std::make_shared<ListPlugin>();
    plugin->allowed = {open};
    srv.setAuthPlugin(plugin);

    auto listener = std::make_shared<NullListener>("auth-listener-1");

    std::string sioId;
    std::string reason;
    SIO_ASSERT(srv.connectToSpace(open, listener, sioId, reason));
    SIO_ASSERT(reason.empty());
    SIO_ASSERT(!sioId.empty());
    SIO_ASSERT_EQ(listener->subscribedCount, 1);

    // sioId is what the space knows this listener under, so that is the id
    // leaveSpace() has to be given
    SIO_ASSERT(srv.leaveSpace(open, sioId));
    SIO_ASSERT_EQ(listener->leftCount, 1);

    // -- a refusal: reason carries the plugin's message, sioId stays empty ---
    // both are output parameters and are cleared on entry, so a caller may
    // reuse the same two strings for every connect instead of resetting them
    std::string refusedId = "stale-id";
    std::string refusedReason = "stale reason";
    SIO_ASSERT(!srv.connectToSpace(closed, listener, refusedId, refusedReason));
    SIO_ASSERT_EQ(refusedReason, std::string("not on the list"));
    SIO_ASSERT(refusedId.empty());
    // a refused connect must not have subscribed anybody
    SIO_ASSERT_EQ(listener->subscribedCount, 1);
    SIO_ASSERT_EQ(srv.findSpace(closed)->size(), 0);

    // the plugin is told which namespace and which connection it is deciding
    // about - without both it cannot do anything useful
    SIO_ASSERT_EQ(plugin->connectCalls.size(), size_t(2));
    SIO_ASSERT_EQ(plugin->connectCalls[0], open + "|auth-listener-1");
    SIO_ASSERT_EQ(plugin->connectCalls[1], closed + "|auth-listener-1");

    // the 3-argument form keeps working (no reason wanted)
    std::string alsoRefused;
    SIO_ASSERT(!srv.connectToSpace(closed, listener, alsoRefused));
    SIO_ASSERT(alsoRefused.empty());
  }

  // -- a plugin that refuses without a reason still gets a message ----------
  {
    class SilentPlugin : public AuthPlugin {
    public:
      bool mayConnect(const std::string&, const SpaceListener::Ptr&,
                      std::string&) override {
        return false;
      }
    };

    auto listener = std::make_shared<NullListener>("auth-listener-2");
    std::string sioId, reason = "untouched";
    srv.setAuthPlugin(std::make_shared<SilentPlugin>());
    SIO_ASSERT(!srv.connectToSpace(open, listener, sioId, reason));
    SIO_ASSERT(!reason.empty());
    SIO_ASSERT(reason != "untouched");
  }

  // -- the shipped plugins -------------------------------------------------
  {
    auto listener = std::make_shared<NullListener>("auth-listener-3");
    std::string sioId, reason;

    srv.setAuthPlugin(std::make_shared<DenyAllAuth>());
    SIO_ASSERT(!srv.connectToSpace(open, listener, sioId, reason));
    SIO_ASSERT(reason.find(open) != std::string::npos);
    SIO_ASSERT_EQ(listener->subscribedCount, 0);
    SIO_ASSERT(!std::make_shared<DenyAllAuth>()->mayPublish(open, listener));

    srv.setAuthPlugin(std::make_shared<AllowAllAuth>());
    SIO_ASSERT(srv.connectToSpace(open, listener, sioId, reason));
    SIO_ASSERT_EQ(listener->subscribedCount, 1);
    // mayPublish is not pure: a plugin that does not care about publishing
    // does not have to override it
    SIO_ASSERT(srv.authPlugin()->mayPublish(open, listener));
    std::string left = sioId;
    SIO_ASSERT(srv.leaveSpace(open, left));
  }

  // -- a null plugin is not installed --------------------------------------
  {
    auto current = srv.authPlugin();
    srv.setAuthPlugin(nullptr);
    SIO_ASSERT(srv.authPlugin() == current);
  }
}
