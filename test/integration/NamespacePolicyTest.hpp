/***************************************************************************
 *
 * Which namespaces a client may join, checked against the running server.
 *
 * The policy itself lives in SioServer (and is unit-tested there); what this
 * test pins is that the policy is what a client actually sees on the wire:
 * a CONNECT for a namespace the application did not declare is answered with
 * a socket.io CONNECT_ERROR carrying a reason, and it does not disturb the
 * engine.io connection the client is multiplexing.
 *
 ***************************************************************************/

#ifndef SIO_TEST_NamespacePolicyTest_hpp
#define SIO_TEST_NamespacePolicyTest_hpp

#include "IntegrationTest.hpp"

class NamespacePolicyTest : public siotest::IntegrationTest {
public:
  NamespacePolicyTest() : IntegrationTest("NamespacePolicyTest") {}

  void onRun() override;
};

#endif /* SIO_TEST_NamespacePolicyTest_hpp */
