#pragma once

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
    std::unordered_map<std::string, Space::Ptr> mySpaces;

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
     * so dropping one can never silently orphan subscribers.
     *
     * Like newSpace(), this is a start-up / administration operation: the
     * registry is not synchronised against the request path, because in normal
     * use it is written once and only read afterwards.
     *
     * @return true if it was removed, false if it was not there or was
     *         refused.
     */
    bool dropSpace(const std::string& id);

    /** how many namespaces are registered (diagnostics and tests) */
    size_t spaceCount() const { return mySpaces.size(); }

    /**
     * Let unknown namespaces be created on first connect.
     *
     * Off by default; turn it on only where clients, not the application,
     * decide the set of namespaces. Note that turning it on moves a write to
     * the namespace registry onto the request path - see the note on
     * dropSpace() about what that costs in terms of synchronisation.
     */
    void setAutoCreateSpaces(bool enable) { autoCreateSpaces = enable; }
    bool autoCreateSpacesEnabled() const { return autoCreateSpaces; }

    /**
     * Install the authentication plugin. A null plugin is not accepted - the
     * always-allow default is restored instead, so forgetting to configure one
     * fails open the way it always has rather than locking everyone out.
     *
     * Like newSpace()/dropSpace(), this is a start-up operation: the plugin is
     * read on every connect and publish from the threads serving those
     * requests, and swapping it while traffic is running is not synchronised
     * against them. Install it before webApiStart().
     */
    void setAuthPlugin(AuthPlugin::Ptr plugin);

    /** the plugin in force; never null */
    AuthPlugin::Ptr authPlugin() const { return auth; }

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
