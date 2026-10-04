#include "SioServerTest.hpp"

#include "TestAssert.hpp"

#include "oatpp_sio/sio/sioServer.hpp"
#include "oatpp_sio/sio/space.hpp"

#include <exception>
#include <memory>
#include <string>

using oatpp_sio::sio::SioServer;
using oatpp_sio::sio::Space;
using oatpp_sio::sio::SpaceListener;

namespace {

/** listener that records the space lifecycle callbacks */
class RecordingListener : public SpaceListener {
public:
  int subscribedCount = 0;
  int leftCount = 0;
  std::string lastSpace;

  RecordingListener(const std::string& id) : SpaceListener(id) {}

  void onSioMessage(std::shared_ptr<Space>, Ptr,
                    oatpp_sio::Message::Ptr) override {}

  void subscribed(std::shared_ptr<Space> space) override {
    subscribedCount++;
    lastSpace = space->id();
  }

  void left(std::shared_ptr<Space> space) override {
    leftCount++;
    lastSpace = space->id();
  }
};

/** unique space name, so the test is re-runnable against the singleton */
std::string uniqueName(const std::string& base) {
  static int counter = 0;
  return base + "-" + std::to_string(++counter);
}

} // namespace

void SioServerTest::onRun() {

  auto& srv = SioServer::serverInstance();

  // -- singleton -------------------------------------------------------------
  SIO_ASSERT(&SioServer::serverInstance() == &srv);

  // -- the root space is created with the server -----------------------------
  SIO_ASSERT(srv.getSpace("/") != nullptr);
  SIO_ASSERT_EQ(srv.getSpace("/")->id(), std::string("/"));

  // -- explicit creation -----------------------------------------------------
  auto name = uniqueName("/unit");
  auto spc = srv.newSpace(name);
  SIO_ASSERT(spc != nullptr);
  SIO_ASSERT_EQ(spc->id(), name);
  SIO_ASSERT(srv.getSpace(name) == spc); // getSpace returns the same object

  // creating the same space twice is an error
  bool threw = false;
  try {
    srv.newSpace(name);
  } catch (const std::exception&) {
    threw = true;
  }
  SIO_ASSERT(threw);

  // -- an unknown namespace is not created by looking it up ------------------
  {
    const auto missing = uniqueName("/missing");
    const size_t before = srv.spaceCount();

    // findSpace() is the lookup for a request path: null, never an exception,
    // and it does not create anything either
    SIO_ASSERT(srv.findSpace(missing) == nullptr);
    SIO_ASSERT_EQ(srv.spaceCount(), before);

    bool threwOnMissing = false;
    try {
      srv.getSpace(missing);
    } catch (const std::exception&) {
      threwOnMissing = true;
    }
    SIO_ASSERT(threwOnMissing);
    SIO_ASSERT_EQ(srv.spaceCount(), before); // a failed lookup creates nothing

    // Auto-create is the opt-in for applications whose clients decide the set
    // of namespaces. It is off by default because a client that may name a
    // namespace may then make the server allocate one - and keep it.
    SIO_ASSERT(!srv.autoCreateSpacesEnabled());
    srv.setAutoCreateSpaces(true);
    auto created = srv.getSpace(missing);
    SIO_ASSERT(created != nullptr);
    SIO_ASSERT_EQ(created->id(), missing);
    SIO_ASSERT(srv.getSpace(missing) == created); // ...and stable
    srv.setAutoCreateSpaces(false);
    SIO_ASSERT(!srv.autoCreateSpacesEnabled());
  }

  // -- dropSpace() retires a namespace again ---------------------------------
  {
    const auto temp = uniqueName("/temp");
    const size_t before = srv.spaceCount();

    SIO_ASSERT(!srv.dropSpace(temp)); // was not there

    srv.newSpace(temp);
    SIO_ASSERT_EQ(srv.spaceCount(), before + 1);

    auto member = std::make_shared<RecordingListener>("dropper");
    std::string memberId;
    SIO_ASSERT(srv.connectToSpace(temp, member, memberId));

    // dropping a namespace somebody is in would orphan their subscription
    SIO_ASSERT(!srv.dropSpace(temp));
    SIO_ASSERT(srv.findSpace(temp) != nullptr);

    SIO_ASSERT(srv.leaveSpace(temp, memberId));
    SIO_ASSERT(srv.dropSpace(temp));
    SIO_ASSERT(srv.findSpace(temp) == nullptr);
    SIO_ASSERT_EQ(srv.spaceCount(), before);

    // the root namespace is always there, like the reference server's default
    SIO_ASSERT(!srv.dropSpace("/"));
    SIO_ASSERT(srv.findSpace("/") != nullptr);
  }

  // -- connecting to a namespace that does not exist fails, quietly ----------
  {
    const auto nowhere = uniqueName("/nowhere");
    auto listener = std::make_shared<RecordingListener>("ghost");
    std::string sioId;
    std::string reason;
    SIO_ASSERT(!srv.connectToSpace(nowhere, listener, sioId, reason));
    SIO_ASSERT(!reason.empty());
    SIO_ASSERT_EQ(listener->subscribedCount, 0);
    SIO_ASSERT(srv.findSpace(nowhere) == nullptr);
  }

  // -- connectToSpace subscribes + notifies ----------------------------------
  auto listener = std::make_shared<RecordingListener>("listener-1");
  std::string sioId;
  SIO_ASSERT(srv.connectToSpace(name, listener, sioId));
  SIO_ASSERT(!sioId.empty());
  SIO_ASSERT_EQ(listener->subscribedCount, 1);
  SIO_ASSERT_EQ(listener->lastSpace, name);
  SIO_ASSERT_EQ(srv.getSpace(name)->size(), 1);

  // the id handed back to the client must be the key the space knows the
  // listener under - otherwise it can never be removed again
  SIO_ASSERT(srv.getSpace(name)->getListener(sioId) != nullptr);

  // -- leaveSpace unsubscribes + notifies ------------------------------------
  SIO_ASSERT(srv.leaveSpace(name, sioId));
  SIO_ASSERT_EQ(srv.getSpace(name)->size(), 0);
  SIO_ASSERT_EQ(listener->leftCount, 1);

  // leaving a space we are not a member of fails
  SIO_ASSERT(!srv.leaveSpace(name, sioId));

  // ... and so does an unknown space (it must not be auto-created here)
  SIO_ASSERT(!srv.leaveSpace("/definitely-not-a-space", sioId));

  // -- two listeners in one space --------------------------------------------
  {
    auto l1 = std::make_shared<RecordingListener>("l1");
    auto l2 = std::make_shared<RecordingListener>("l2");
    std::string id1, id2;
    SIO_ASSERT(srv.connectToSpace(name, l1, id1));
    SIO_ASSERT(srv.connectToSpace(name, l2, id2));
    SIO_ASSERT_EQ(srv.getSpace(name)->size(), 2);

    SIO_ASSERT(srv.leaveSpace(name, id1));
    SIO_ASSERT_EQ(l1->leftCount, 1);
    SIO_ASSERT_EQ(l2->leftCount, 0);
    SIO_ASSERT_EQ(srv.getSpace(name)->size(), 1);

    SIO_ASSERT(srv.leaveSpace(name, id2));
    SIO_ASSERT_EQ(srv.getSpace(name)->size(), 0);
  }
}
