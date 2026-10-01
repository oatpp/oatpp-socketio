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
};

}  // namespace sio
}  // namespace oatpp_sio