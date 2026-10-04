#pragma once

#include <unordered_map>

#include "oatpp_sio/eio/engineIo.hpp"
#include "oatpp_sio/eio/connection.hpp"

#include "oatpp_sio/sio/space.hpp"
#include "oatpp_sio/sio/wire.hpp"

namespace oatpp_sio {

namespace eio {

class EioConnection;

}

namespace sio {

/**
 * The packet types moved to oatpp_sio/sio/wire.hpp (as `PacketType`, with
 * camel case enumerators). Kept as an alias for source compatibility.
 */
using SioPacketType = PacketType;


/** A connector between the lower-level engine connection and a number of socket.io
 * namespaces. this is also responsible for en/decoding the messages into the wire 
 * format and funneling events to the different spaces that are connected  
 */
class SioAdapter : public SpaceListener
{
   public:
    SioAdapter(const std::string& id) : SpaceListener(id) {}

    std::shared_ptr<oatpp_sio::eio::EioConnection> eioConn;

    /**
     * The spaces this connection joined, maintained by subscribed()/left().
     * This is the authorisation set for publishing: the namespace on an
     * incoming packet comes off the wire, so it is client-supplied and has to
     * be checked against this instead of being used to look up the global
     * space registry - which would let one client publish into namespaces it
     * never connected to, and would create them on demand.
     */
    std::unordered_map<std::string, Space::Ptr> mySpaces;

    // low-level ->up
    virtual void onEioMessage(oatpp_sio::Message::Ptr msg);

    // from socketio
    virtual void onSioMessage(std::shared_ptr<Space> space, Ptr sender,
                              oatpp_sio::Message::Ptr msg) override;

    virtual void subscribed(std::shared_ptr<Space> space) override;

    virtual void left(std::shared_ptr<Space> space) override;

    void shutdown();

   private:
    void onSioConnect(const std::string& data);

    void onSioEvent(const std::string& data);

    /**
     * The space with this id, but only if this connection joined it.
     * Returns null otherwise; never creates one.
     */
    Space::Ptr joinedSpace(const std::string& name) const;

    /**
     * Protocol violation: log and drop the connection, the way the reference
     * server does when a packet arrives for a namespace this client has no
     * socket for.
     */
    void dropConnection(const std::string& reason);
};

}  // namespace sio
}  // namespace oatpp_sio