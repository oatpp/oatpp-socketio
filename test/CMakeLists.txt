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
    EioPacketTest
    EngineConfigTest
    SioServerTest
    SpaceTest
    UtilTest
    WireTest
)

add_executable(sio-unit-tests
    TestRunner.cpp
    unit/TestMain.cpp
    unit/EioPacketTest.cpp
    unit/EngineConfigTest.cpp
    unit/SioServerTest.cpp
    unit/SpaceTest.cpp
    unit/UtilTest.cpp
    unit/WireTest.cpp
)

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

add_executable(sio-integration-tests
    TestRunner.cpp
    integration/TestMain.cpp
    integration/RawHttpClient.cpp
    integration/RawWsClient.cpp
    integration/SioClient.cpp
    integration/ConnectionCloseTest.cpp
    integration/EngineIoTest.cpp
    integration/SocketIoTest.cpp
    integration/WebSocketTest.cpp
    integration/WebApiTest.cpp
)

target_include_directories(sio-integration-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sio-integration-tests PRIVATE oatpp-socketio ${LIBS})

add_test(NAME integration.all COMMAND sio-integration-tests)
