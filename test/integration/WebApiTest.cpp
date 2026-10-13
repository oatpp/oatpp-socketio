#include "WebApiTest.hpp"

#include "RawHttpClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <string>

using namespace siotest;

void WebApiTest::onRun() {

  // -- a file in the web root is served ------------------------------------
  {
    const RawResponse response = httpRequest(g_testPort, "GET", "/hello.txt");
    SIO_ASSERT_EQ(response.status, 200);
    SIO_ASSERT_EQ(response.body, std::string("served-by-oatpp-socketio-tests\n"));
  }

  // -- a missing file is a 404 ---------------------------------------------
  SIO_ASSERT_EQ(httpRequest(g_testPort, "GET", "/does-not-exist.txt").status,
                404);

  // -- path traversal must not escape the web root -------------------------
  // (the oatpp router normalizes most of these, but the static controller
  //  itself has no jail - this pins the behaviour)
  const char* attacks[] = {
      "/../../etc/passwd",
      "/../../../etc/passwd",
      "/%2e%2e/%2e%2e/etc/passwd",
      "/%2e%2e%2f%2e%2e%2fetc%2fpasswd",
      "/....//etc/passwd",
      "/hello.txt/../../../../etc/passwd",
      "/./../../etc/passwd",
      "/subdir/../../../../etc/passwd",
      "/..%00/etc/passwd",  // a NUL must not truncate the path that gets
                            // opened relative to the one that got checked
      "\\..\\..\\etc\\passwd",
  };

  for (const char* path : attacks) {
    const RawResponse response = httpRequest(g_testPort, "GET", path);
    if (response.body.find("root:") != std::string::npos) {
      ::siotest::fail(std::string("path traversal via ") + path,
                      "the server served /etc/passwd");
    }
    if (response.status == 200) {
      ::siotest::fail(std::string("path traversal via ") + path,
                      "HTTP 200 for a path outside the web root");
    }
  }

  // -- a symlink in the web root must not be a way out of it ---------------
  // Pure lexical normalisation would accept this one: the request path stays
  // below the root the whole time, but the file that ends up open does not.
  {
    const std::string link = std::string(siotest::g_webRoot) + "escape";
    ::unlink(link.c_str());  // left over from an earlier run
    if (::symlink("/etc", link.c_str()) == 0) {
      const RawResponse response =
          httpRequest(g_testPort, "GET", "/escape/passwd");
      if (response.status == 200 &&
          response.body.find("root:x:0:0") != std::string::npos) {
        ::siotest::fail("symlink escape via /escape/passwd",
                        "the server followed a link out of the web root");
      }
      ::unlink(link.c_str());
    }
  }

  // -- swagger -------------------------------------------------------------
  // the swagger-ui resources are an install-time path; skip when they are
  // not there instead of failing for an unrelated reason
#ifdef OATPP_SWAGGER_RES_PATH
  {
    struct stat info;
    if (::stat(OATPP_SWAGGER_RES_PATH, &info) != 0) {
      std::cout << "  (skipped: swagger resources not installed in "
                << OATPP_SWAGGER_RES_PATH << ")\n";
      return;
    }
  }

  {
    const RawResponse ui = httpRequest(g_testPort, "GET", "/swagger/ui");
    SIO_ASSERT_EQ(ui.status, 200);
    SIO_ASSERT(ui.body.find("<html") != std::string::npos ||
               ui.body.find("<!DOCTYPE") != std::string::npos);
  }
  {
    const RawResponse doc =
        httpRequest(g_testPort, "GET", "/api-docs/oas-3.0.0.json");
    SIO_ASSERT_EQ(doc.status, 200);
    SIO_ASSERT(doc.body.find("\"openapi\"") != std::string::npos);
    // the socket.io endpoints are documented
    SIO_ASSERT(doc.body.find("socket.io") != std::string::npos);
  }
#else
  std::cout << "  (skipped: built without OATPP_SWAGGER_RES_PATH)\n";
#endif
}
