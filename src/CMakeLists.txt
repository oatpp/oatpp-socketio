


add_library(oatpp-socketio SHARED
    globals.cpp
    util.cpp
    eio/engineIoImpl.cpp
    eio/connection.cpp
    eio/messagePool.cpp
    eio/wsConnection.cpp
    sio/space.cpp
    sio/wire.cpp
    sio/adapter.cpp
    sio/sioServer.cpp
    webapi/swaggerComponent.cpp
    webapi/webApi.cpp
    )

target_link_libraries( oatpp-socketio PUBLIC    ${LIBS}  )
set_property(TARGET oatpp-socketio PROPERTY VERSION ${PROJECT_VERSION})
set_property(TARGET oatpp-socketio PROPERTY SOVERSION ${PROJECT_VERSION})
install (TARGETS oatpp-socketio
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

