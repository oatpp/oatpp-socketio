/***************************************************************************
 *
 * Shared configuration for the integration tests.
 *
 ***************************************************************************/

#ifndef SIO_TEST_TestConfig_hpp
#define SIO_TEST_TestConfig_hpp

namespace siotest {

/** port the in-process test server listens on (set by TestMain) */
extern unsigned short g_testPort;

/** scratch directory served as the web root by the test server */
extern const char* g_webRoot;

}  // namespace siotest

#endif /* SIO_TEST_TestConfig_hpp */
