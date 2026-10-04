# Oatpp-Socket IO Project

## Overview

The `oatpp-socketio` project is an implementation of the [Socket.IO](https://socket.io/docs/v4/socket-io-protocol/) / [Engine.IO, v4](https://socket.io/docs/v4/engine-io-protocol/) protocol in C++, specifically for [OAT++](https://oatpp.io/).

**Status** engine.io v4 and the socket.io v4 text protocol are implemented and covered by a test suite: long-polling and websocket transports, the engine.io ping/pong keepalive, namespaces, publish/subscribe, and event/ack round-trips. Still open: binary packets (`5`/`6`), socket.io `disconnect`/`ack` packets on the inbound path, and `maxPayload`, which is advertised in the engine.io OPEN packet but not yet enforced on what comes back. See [Limitations](#limitations).

## Project Structure

The layering follows the two tiers of the Socket.IO protocol:

```
include/oatpp_sio/eio/    engine.io v4: packet framing, the connection store,
                          long-polling and websocket transports
include/oatpp_sio/sio/    socket.io v4: the wire codec, namespaces (Space),
                          the per-connection engine.io <-> socket.io bridge
                          (SioAdapter), the namespace registry (SioServer),
                          the auth hook (AuthPlugin)
include/oatpp_sio/webapi/ oatpp glue: controllers, DTOs, app components
src/                      implementations
app/                      two runnable servers
test/                     unit + integration suites (see Testing below)
```

## Project Setup

This uses minimal prerequisites, esp.:

- **CMake** (build project)
- **Oatpp** (used for HTTP handling)
  - **oatpp-websockets** (websockets implementation)
  - **oatpp-Swagger** (for API documentation)

As this has been implemented on Linux, build the oatpp prerequisites with the shared library option turned on.

### Create a build directory and run CMake:

   ```sh
   mkdir build
   cd build
   cmake ..
   ```

### Build the project:

   ```sh
   cmake --build build -j$(nproc)
   ```

## Running

   ```sh
   ./build/app/engineio_server     # engine.io only
   ./build/app/sio_server          # engine.io + socket.io
   ```

Then stop it with Ctrl-C (or SIGTERM); both servers shut the web api down
before exiting.

| environment variable | default | meaning |
|---|---|---|
| `OATPP_SIO_HOST` | `0.0.0.0` | listen address |
| `OATPP_SIO_PORT` | `8000` | listen port |
| `WEBAPI` | `1` | `0` turns the swagger UI off |

The Swagger API documentation is then at http://localhost:8000/swagger/ui#/
and the OpenAPI description at `/api-docs/oas-3.0.0.json`.

## Using the library

### Declare the namespaces you serve

A client does not get to decide what namespaces exist. `"/"` is always
there; anything else has to be declared, the same way the reference server
requires `io.of("/chat")`:

```cpp
#include "oatpp_sio/sio/sioServer.hpp"

oatpp_sio::sio::SioServer::serverInstance().newSpace("/chat");
```

A CONNECT for a name that was not declared is answered with
`connect_error {"message": "Invalid namespace"}` and the namespace is *not*
created. The engine.io connection stays up, because one transport carries
all the namespaces this client is on - being refused for one is not an error
on the others.

If your application really wants clients to invent namespaces, say so:

```cpp
SioServer::serverInstance().setAutoCreateSpaces(true);
```

Look a namespace up with `findSpace()` (returns null, never throws, creates
nothing) rather than `getSpace()` (throws; creates if auto-create is on).
`dropSpace()` retires one again and refuses `"/"` and any namespace that
still has members.

### Decide who may connect

`AuthPlugin` is asked before a client joins a namespace, and again before an
event is published into one:

```cpp
class ChatAuth : public oatpp_sio::sio::AuthPlugin {
public:
  bool mayConnect(const std::string& spaceName,
                  const SpaceListener::Ptr& listener,
                  std::string& reason) override {
    if (!tokenChecksOut(listener)) {
      reason = "chat needs a ticket";   // -> connect_error {"message": reason}
      return false;
    }
    return true;
  }
};

SioServer::serverInstance().setAuthPlugin(std::make_shared<ChatAuth>());
```

There is always a plugin in force - `AllowAllAuth` until you install another
one, so forgetting to configure one fails open rather than locking everyone
out, and `setAuthPlugin(nullptr)` is ignored with a warning. `mayPublish()`
defaults to allow and can only narrow what a connection may do: the check
that a connection may only publish into namespaces it actually joined is not
configurable and is not a plugin's to switch off. Install the plugin before
`webApiStart()`.

## Testing

   ```sh
   cmake -DBUILD_TESTS=ON .. && cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure
   ```

Two binaries, both driven through CTest:

- `unit.*` - protocol building blocks, no sockets: the engine.io framing, the
  socket.io wire codec (table-driven against the reference decoder, plus a
  truncation sweep over hostile inputs), namespace registry and pub/sub
  bookkeeping, session ids, the auth hook. Each unit test is its own CTest
  process, so a hard crash in one cannot take the others with it, and each
  one checks the global oatpp object count for leaks.
- `integration.all` - starts the web api in-process on a local port and talks
  to it with hand-written protocol clients (raw HTTP over BSD sockets, a
  minimal RFC 6455 client, and an engine.io long-poll client). Deliberately
  not oatpp's client and not a third-party socket.io client: the point is to
  be able to send what a hostile or merely nonconforming client sends.

Run one test, or list them:

   ```sh
   ./build/test/sio-unit-tests WireTest
   ./build/test/sio-integration-tests SocketIoTest
   ./build/test/sio-integration-tests --list
   ctest --test-dir build -R unit.SpaceTest -V     # -V is ctest's own flag
   ```

To check that a test really tests the behaviour it claims, revert the
behaviour and see whether anything notices:

   ```sh
   ./tools_mutation_check.py --list
   ./tools_mutation_check.py
   ```

## Limitations

- Inbound socket.io packets: `connect` and `event` are handled; `disconnect`,
  `ack`, `connect_error` and both binary packet types are logged and ignored.
- `maxPayload` is advertised in the engine.io OPEN packet and not enforced on
  inbound POST bodies or websocket frames.
- The namespace registry is written at start-up and read from the request
  path; it is not synchronised. `newSpace()`/`dropSpace()`/`setAuthPlugin()`
  are start-up or administration operations. Turning auto-create on moves a
  registry write onto the request path and is not safe under concurrent
  connects.
- A refused publish is silent: if the client asked for an ack, its ack
  callback is never called.

## Acknowledgements

Parts of this project have been developed with funding from the European Union’s Horizon Europe research and innovation programme under Grant Agreement No 101070599.
