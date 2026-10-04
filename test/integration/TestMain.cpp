/***************************************************************************
 *
 * oatpp-socketio integration test driver.
 *
 * Starts the real web api in-process (on the port below) and talks to it the
 * way a browser would. Because the web api is a process-wide singleton the
 * server is started once for all tests; CTest therefore registers this as a
 * single test. Run one test only while debugging with:
 *
 *   ./build/test/sio-integration-tests WebSocketTest
 *
 ***************************************************************************/

#include "RawHttpClient.hpp"
#include "TestConfig.hpp"
#include "TestRunner.hpp"

#include "oatpp/Environment.hpp"
#include "oatpp_sio/globals.hpp"
#include "oatpp_sio/sio/sioServer.hpp"
#include "oatpp_sio/webapi/webApp.hpp"

#include "integration/AuthTest.hpp"
#include "integration/ConnectionCloseTest.hpp"
#include "integration/EngineIoTest.hpp"
#include "integration/NamespacePolicyTest.hpp"
#include "integration/SocketIoTest.hpp"
#include "integration/WebApiTest.hpp"
#include "integration/WebSocketTest.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

unsigned short siotest::g_testPort = 18321;
const char* siotest::g_webRoot = "/tmp/oatpp-socketio-tests-web/";

SIO_REGISTER_TEST(AuthTest, AuthTest);
SIO_REGISTER_TEST(ConnectionCloseTest, ConnectionCloseTest);
SIO_REGISTER_TEST(EngineIoTest, EngineIoTest);
SIO_REGISTER_TEST(NamespacePolicyTest, NamespacePolicyTest);
SIO_REGISTER_TEST(SocketIoTest, SocketIoTest);
SIO_REGISTER_TEST(WebSocketTest, WebSocketTest);
SIO_REGISTER_TEST(WebApiTest, WebApiTest);

int main(int argc, const char* argv[]) {

  // web root with a single file for the static content tests
  ::mkdir(siotest::g_webRoot, 0755);
  {
    std::ofstream file(std::string(siotest::g_webRoot) + "hello.txt");
    file << "served-by-oatpp-socketio-tests\n";
  }

  // the port is read by the connection provider component, so it has to be
  // in the environment before webApiInit()
  ::setenv("OATPP_SIO_PORT", std::to_string(siotest::g_testPort).c_str(), 1);

  oatpp::Environment::init();

  oatpp_sio::WebApiState& state = oatpp_sio::getGlobalState();
  state.webRoot = siotest::g_webRoot;
  state.enableSwaggerUi = true;

  oatpp_sio::webapi::webApiInit();

  // Namespaces are declared by the application, not invented by the server
  // when a client names one (that is what an attacker would do). The tests
  // below use these two; SioServerTest covers the policy itself.
  auto& sioServer = oatpp_sio::sio::SioServer::serverInstance();
  sioServer.newSpace("/chat");
  sioServer.newSpace("/rooms");

  oatpp_sio::webapi::webApiStart(state);

  if (!siotest::waitForPort(siotest::g_testPort)) {
    std::cerr << "ERROR: test server did not come up on port "
              << siotest::g_testPort << "\n";
    return 2;
  }

  const int result = siotest::runFromArgs(argc, argv);

  oatpp_sio::webapi::webApiStop();
  oatpp_sio::webapi::webApiDestroy();
  oatpp::Environment::destroy();

  return result;
}
