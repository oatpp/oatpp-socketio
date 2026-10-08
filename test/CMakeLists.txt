################################################################################
# oatpp-socketio test suite
#
#   unit/          protocol building blocks, no sockets involved
#   integration/   starts the web api on a local port and talks to it
#
# run everything:      ctest --test-dir build --output-on-failure
# run one test:        ctest --test-dir build -R unit.SpaceTest -V
################################################################################

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

################################################################################
# unit tests
################################################################################

set(sio_unit_tests
    AuthPluginTest
    EioPacketTest
    EngineConfigTest
    RegistryConcurrencyTest
    SioServerTest
    SpaceTest
    UtilTest
    WireTest
)

# The sources are derived from the test list, so that a test cannot be
# registered with CTest (and in unit/TestMain.cpp) while being left out of the
# binary - which fails at link time with "undefined reference to vtable".
set(sio_unit_test_sources TestRunner.cpp unit/TestMain.cpp)
foreach(_unit_test ${sio_unit_tests})
    list(APPEND sio_unit_test_sources "unit/${_unit_test}.cpp")
endforeach()

add_executable(sio-unit-tests ${sio_unit_test_sources})

target_include_directories(sio-unit-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sio-unit-tests PRIVATE oatpp-socketio ${LIBS})

foreach(_unit_test ${sio_unit_tests})
    add_test(NAME unit.${_unit_test} COMMAND sio-unit-tests ${_unit_test})
endforeach()

################################################################################
# integration tests
#
# The web api is a process-wide singleton, so all integration tests share one
# in-process server and are registered as a single CTest test. To debug one
# test on its own, run the binary with its name:
#   ./build/test/sio-integration-tests WebSocketTest -V
################################################################################

set(sio_integration_tests
    AuthTest
    ConnectionCloseTest
    EngineIoTest
    MaxPayloadTest
    NamespacePolicyTest
    SocketIoTest
    WebSocketTest
    WebApiTest
)

# same as for the unit tests: the sources follow the test list
set(sio_integration_test_sources
    TestRunner.cpp
    integration/TestMain.cpp
    integration/RawHttpClient.cpp
    integration/RawWsClient.cpp
    integration/SioClient.cpp
)
foreach(_integration_test ${sio_integration_tests})
    list(APPEND sio_integration_test_sources "integration/${_integration_test}.cpp")
endforeach()

add_executable(sio-integration-tests ${sio_integration_test_sources})

target_include_directories(sio-integration-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sio-integration-tests PRIVATE oatpp-socketio ${LIBS})

add_test(NAME integration.all COMMAND sio-integration-tests)
