#include "NamespacePolicyTest.hpp"

#include "RawHttpClient.hpp"
#include "SioClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include "oatpp_sio/sio/sioServer.hpp"

#include <string>

using namespace siotest;
using oatpp_sio::sio::SioServer;

namespace {

/** puts the auto-create flag back the way it was, also when the test threw */
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

}  // namespace

void NamespacePolicyTest::onRun() {

  auto& srv = SioServer::serverInstance();

  // -- an undeclared namespace is refused ----------------------------------
  // The reference server answers with CONNECT_ERROR ("4") and the message
  // "Invalid namespace"; the socket.io client surfaces that as a
  // `connect_error` event. Our wire framing is engine.io MESSAGE ("4") in
  // front of the socket.io type, so what arrives is "44<nsp>,{...}".
  {
    const size_t spacesBefore = srv.spaceCount();

    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.post("0/nope,"));

    const std::string answer = c.poll();
    SIO_ASSERT_MSG(startsWith(answer, "44/nope,"),
                   std::string("expected a connect_error on /nope, got: ") + answer);
    SIO_ASSERT(answer.find("Invalid namespace") != std::string::npos);

    // refusing a namespace must not create it - otherwise a client can still
    // fill the registry by naming namespaces, it just gets an error with it
    SIO_ASSERT_EQ(srv.spaceCount(), spacesBefore);
  }

  // -- a refused CONNECT does not kill the transport ------------------------
  // One engine.io connection multiplexes several namespaces, and a client that
  // gets refused for one is not doing anything wrong on the others.
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());

    SIO_ASSERT(c.post("0/nope,"));
    SIO_ASSERT(startsWith(c.poll(), "44/nope,"));

    // ... and the very same connection is fine for a declared one
    SIO_ASSERT(c.sioConnect("/chat"));
    SIO_ASSERT(!c.sioSid().empty());
  }

  // -- declared namespaces are connectable ----------------------------------
  // (both with and without the separator comma the reference client writes)
  {
    PollClient withComma(g_testPort);
    SIO_ASSERT(withComma.open());
    SIO_ASSERT(withComma.sioConnect("/chat"));

    PollClient withoutComma(g_testPort);
    SIO_ASSERT(withoutComma.open());
    SIO_ASSERT(withoutComma.post("0/rooms"));
    SIO_ASSERT(startsWith(withoutComma.poll(), "40/rooms,"));
  }

  // -- the root namespace is always there -----------------------------------
  {
    PollClient root(g_testPort);
    SIO_ASSERT(root.open());
    SIO_ASSERT(root.sioConnect("/"));
  }

  // -- auto-create is an opt-in, and when it is on it means what it says ----
  // Flipping the flag back on is the old behaviour: a client names a namespace
  // and the server allocates it. That has to be observable, or the flag is a
  // knob that is not connected to anything.
  {
    const std::string clientNamed = "/client-decided";
    const size_t spacesBefore = srv.spaceCount();

    {
      AutoCreateGuard on(srv, true);
      PollClient c(g_testPort);
      SIO_ASSERT(c.open());
      SIO_ASSERT_MSG(c.sioConnect(clientNamed),
                     "with auto-create on, a client-named namespace must connect");
      SIO_ASSERT(srv.findSpace(clientNamed) != nullptr);
      SIO_ASSERT_EQ(srv.spaceCount(), spacesBefore + 1);

      // leave and retire it again, so the test does not change the server for
      // the ones after it
      std::string sid = c.sioSid();
      SIO_ASSERT(srv.leaveSpace(clientNamed, sid));
    }

    // off again: the same name is refused, exactly like any other unknown one
    SIO_ASSERT(srv.dropSpace(clientNamed));
    SIO_ASSERT_EQ(srv.spaceCount(), spacesBefore);
    PollClient after(g_testPort);
    SIO_ASSERT(after.open());
    SIO_ASSERT(after.post("0" + clientNamed + ","));
    SIO_ASSERT(startsWith(after.poll(), "44" + clientNamed + ","));
  }

  // -- a namespace can be dropped again, but not while it has members -------
  // This is the administration side of the same policy: the application can
  // retire a namespace it declared. Doing it from a test against the live
  // server is safe as long as nobody is in it.
  {
    SIO_ASSERT(srv.newSpace("/droppable") != nullptr);
    SIO_ASSERT(srv.findSpace("/droppable") != nullptr);

    PollClient inIt(g_testPort);
    SIO_ASSERT(inIt.open());
    SIO_ASSERT(inIt.sioConnect("/droppable"));

    SIO_ASSERT_EQ(srv.findSpace("/droppable")->size(), 1);
    SIO_ASSERT(!srv.dropSpace("/droppable")); // still has a member
    SIO_ASSERT(srv.findSpace("/droppable") != nullptr);

    std::string sid = inIt.sioSid();
    SIO_ASSERT(srv.leaveSpace("/droppable", sid));
    SIO_ASSERT(srv.dropSpace("/droppable"));
    SIO_ASSERT(srv.findSpace("/droppable") == nullptr);

    // '/' is the reference server's default namespace: always present
    SIO_ASSERT(!srv.dropSpace("/"));
    SIO_ASSERT(srv.findSpace("/") != nullptr);
  }
}
