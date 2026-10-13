/***************************************************************************
 *
 * Authentication / authorisation hook for socket.io connections.
 *
 * The library has no opinion about who may connect. It ships a permissive
 * default (AllowAllAuth) and calls whatever plugin the application installs
 * with SioServer::setAuthPlugin(). A plugin is asked before a client joins a
 * namespace, and again before an event is published into one.
 *
 ***************************************************************************/

#ifndef oatpp_sio_sio_auth_hpp
#define oatpp_sio_sio_auth_hpp

#include "oatpp_sio/sio/space.hpp"

#include <memory>
#include <string>

namespace oatpp_sio {
namespace sio {

/**
 * Hook the application uses to accept or refuse namespace access.
 *
 * One instance is shared by all connections and called from the server's
 * coroutine threads, so an implementation has to be thread safe and must not
 * block: it runs on the thread serving the connection.
 */
class AuthPlugin
{
   public:
    typedef std::shared_ptr<AuthPlugin> Ptr;

    virtual ~AuthPlugin() = default;

    /**
     * May @p listener join the namespace @p spaceName?
     *
     * Called for every socket.io CONNECT. It runs after the namespace has been
     * found, so a plugin can also keep clients out of namespaces that exist
     * but should not be public.
     *
     * @param spaceName namespace named on the CONNECT packet.
     * @param listener  the connection asking; listener->id() identifies it.
     * @param reason    set when returning false. It is sent to the client as
     *                  `connect_error {"message": reason}`, so it should be
     *                  something a user can act on. Leave it empty to get a
     *                  generic message.
     */
    virtual bool mayConnect(const std::string& spaceName,
                            const SpaceListener::Ptr& listener,
                            std::string& reason) = 0;

    /**
     * May @p listener publish an event into @p spaceName?
     *
     * Called after the connection has been checked against the namespaces it
     * actually joined - that check is not configurable and this is not the
     * line of defence against cross-namespace injection, just an extra policy
     * layer on top of it. Defaults to allow.
     */
    virtual bool mayPublish(const std::string& spaceName,
                            const SpaceListener::Ptr& listener);
};

/**
 * The default plugin: allow everything, which is what the library did before
 * there was a plugin at all. Install it explicitly to say so in the code, or
 * rely on it being the default.
 */
class AllowAllAuth : public AuthPlugin
{
   public:
    bool mayConnect(const std::string& spaceName,
                    const SpaceListener::Ptr& listener,
                    std::string& reason) override
    {
        (void)spaceName;
        (void)listener;
        (void)reason;
        return true;
    }
};

/**
 * A plugin that refuses everything, useful as a deny-by-default starting
 * point and in tests.
 */
class DenyAllAuth : public AuthPlugin
{
   public:
    bool mayConnect(const std::string& spaceName,
                    const SpaceListener::Ptr& listener,
                    std::string& reason) override
    {
        (void)listener;
        reason = "Namespace '" + spaceName + "' is not open";
        return false;
    }

    bool mayPublish(const std::string& spaceName,
                    const SpaceListener::Ptr& listener) override
    {
        (void)spaceName;
        (void)listener;
        return false;
    }
};

/**
 * Old name for the always-allow behaviour.
 * @deprecated use AllowAllAuth. The previous SioAuth was never called by
 * anything; its mayConnect() had a different signature and was dead code.
 */
using SioAuth = AllowAllAuth;

}  // namespace sio
}  // namespace oatpp_sio

#endif /* oatpp_sio_sio_auth_hpp */
