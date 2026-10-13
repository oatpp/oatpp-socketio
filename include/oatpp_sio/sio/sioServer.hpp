#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "oatpp_sio/message.hpp"

#include "oatpp_sio/eio/engineIo.hpp"
#include "oatpp_sio/eio/connection.hpp"

#include "oatpp_sio/sio/auth.hpp"
#include "oatpp_sio/sio/space.hpp"

namespace oatpp_sio {
namespace sio {

/** A connector between the lower-level engine connection and a number of socket.io
 * namespaces. this is also responsible for en/decoding the messages into the wire format  */
class SioServer
{
    static SioServer* universe;

    /**
     * The namespace registry, and the lock that makes it usable from more than
     * one thread. It guards the map, the auto-create flag and the plugin
     * pointer - the mutable state of the server.
     *
     * Every read of and every write to that state happens with stateLock held.
     * Two rules keep that from turning into a deadlock:
     *
     *  - the critical sections are map operations and pointer copies. Nothing
     *    else is called while holding the lock except Space's own membership
     *    functions, which take Space::lock and return, so the order is always
     *    registry -> space and never the other way round (Space does not know
     *    this class exists, so the order cannot be inverted by accident). No
     *    listener and no auth-plugin callback ever runs under the lock - a
     *    plugin is free to call back into the server;
     *  - it is a plain std::mutex, not an oatpp::async::Lock: the sections are
     *    short and never block, and oatpp's own locks must not be taken in
     *    thread-blocking mode from inside a coroutine.
     */
    std::unordered_map<std::string, Space::Ptr> mySpaces;
    mutable std::mutex stateLock;

    /** lookup with stateLock already held */
    Space::Ptr findSpaceLocked(const std::string& id) const;
    /** creation with stateLock already held */
    Space::Ptr newSpaceLocked(const std::string& id);

    /**
     * Whether an unknown namespace is created when a client asks for it.
     * Off by default: a client must not be able to make the server allocate a
     * namespace - and keep it forever - just by naming one on a CONNECT. The
     * reference implementation is the same way round; it answers an unknown
     * namespace with connect_error "Invalid namespace" unless a dynamic
     * namespace factory was registered.
     */
    bool autoCreateSpaces = false;

    AuthPlugin::Ptr auth;

    SioServer();
    virtual ~SioServer();

   public:
    static SioServer& serverInstance();

    /**
     * Create a namespace. This is the normal way to make one available: the
     * application declares what it serves, at start up.
     * @throws std::runtime_error if there already is one.
     */
    Space::Ptr newSpace(const std::string& id);

    /**
     * Look a namespace up without creating it.
     * @return the namespace, or null if there is none. Never throws - this is
     *         the lookup to use on a request path.
     */
    Space::Ptr findSpace(const std::string& id) const;

    /**
     * Look a namespace up, creating it if auto-create is enabled.
     * @throws std::runtime_error if there is none and auto-create is off.
     *         Prefer findSpace() where a missing namespace is expected.
     */
    Space::Ptr getSpace(const std::string& id);

    /**
     * Remove a namespace.
     *
     * Refuses the root namespace "/" (always present, like the reference
     * server's default namespace) and any namespace that still has listeners,
     * so dropping one cannot silently orphan subscribers. The emptiness check
     * and the removal are one critical section, and a join takes the same lock,
     * so a namespace cannot go away between "is anybody in it?" and its being
     * retired - which is the race that makes retiring namespaces under traffic
     * interesting.
     *
     * @return true if it was removed, false if it was not there or was
     *         refused.
     */
    bool dropSpace(const std::string& id);

    /** how many namespaces are registered (diagnostics and tests) */
    size_t spaceCount() const;

    /**
     * Let unknown namespaces be created on first connect.
     *
     * Off by default; turn it on only where clients, not the application,
     * decide the set of namespaces. That means a registry write per new name on
     * the request path, which is what the registry lock is for.
     */
    void setAutoCreateSpaces(bool enable);
    bool autoCreateSpacesEnabled() const;

    /**
     * Install the authentication plugin. A null plugin is not accepted - the
     * always-allow default is restored instead, so forgetting to configure one
     * fails open the way it always has rather than locking everyone out.
     *
     * Safe to call while traffic is running: every connect and publish takes
     * its own copy of the pointer under the lock, so a plugin is never replaced
     * underneath a decision, and a plugin may call back into the server.
     */
    void setAuthPlugin(AuthPlugin::Ptr plugin);

    /** the plugin in force; never null */
    AuthPlugin::Ptr authPlugin() const;

    /**
     * Subscribe a listener to a namespace and notify it.
     *
     * @return true if the listener joined. false if the namespace does not
     *         exist or the auth plugin refused the connection - the reason is
     *         then "Invalid namespace" or whatever the plugin supplied.
     */
    bool connectToSpace(const std::string& spaceName,
                        oatpp_sio::sio::SpaceListener::Ptr listener,
                        std::string& sioId);

    /**
     * As above, reporting why a refusal happened so it can be sent to the
     * client as connect_error. Both @p sioId and @p reason are output only and
     * are cleared on entry, so a caller may reuse the same strings every time.
     */
    bool connectToSpace(const std::string& spaceName,
                        oatpp_sio::sio::SpaceListener::Ptr listener,
                        std::string& sioId, std::string& reason);

    /** removes a lister from a space and notifies the listener */
    bool leaveSpace(const std::string& spaceName, std::string& sioId);
};

}  // namespace sio
}  // namespace oatpp_sio
