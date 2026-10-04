#include <memory>

#include "oatpp/base/Log.hpp"

#include "oatpp_sio/sio/adapter.hpp"
#include "oatpp_sio/sio/sioServer.hpp"
#include "oatpp_sio/sio/wire.hpp"

using namespace std;

using namespace oatpp_sio::sio;

Space::Ptr SioAdapter::joinedSpace(const std::string& name) const
{
    auto it = mySpaces.find(name);
    return it == mySpaces.end() ? Space::Ptr() : it->second;
}

void SioAdapter::dropConnection(const std::string& reason)
{
    OATPP_LOGw("SADAP", "protocol violation, dropping connection {}: {}", id(),
               reason);
    auto conn = eioConn;
    if (conn) {
        conn->shutdownConnection();
    }
}

void SioAdapter::shutdown()
{
    auto iter = mySpaces.begin();
    while (iter != mySpaces.end()) {
        iter->second->removeListener(id());
        iter++;
    }
    mySpaces.clear();
}

void SioAdapter::subscribed(std::shared_ptr<Space> space)
{
    mySpaces.insert({space->id(), space});
}

void SioAdapter::left(std::shared_ptr<Space> space)
{
    auto iter = mySpaces.find(space->id());
    if (iter != mySpaces.end()) {
        mySpaces.erase(iter);
    }
}

// low-level ->up
void SioAdapter::onEioMessage(oatpp_sio::Message::Ptr msg)
{
    OATPP_LOGi("SADAP", "SIo Packet... {}", msg->body);

    // an empty packet carries no type - ignore it instead of reading past the
    // end of the string
    if (msg->body.empty()) {
        OATPP_LOGw("SADAP", "empty socket.io packet, ignoring");
        return;
    }

    // Hold a reference to ourselves for the duration of the call: handling a
    // packet can drop the connection (a protocol violation does), and the
    // connection holds the only strong reference to this adapter, so without
    // this `this` could be destroyed while still executing.
    const std::shared_ptr<SioAdapter> keep =
        eioConn ? eioConn->getSio() : nullptr;

    // decode, then push
    const char pType = msg->body[0];
    const std::string rest = msg->body.substr(1);

    switch (pType) {
        case static_cast<char>(PacketType::connect):
            onSioConnect(rest);
            break;
        case static_cast<char>(PacketType::event):
            onSioEvent(rest);
            break;
        default:
            OATPP_LOGi("SADAP", "Unknown SIo Packet type '{}'", pType);
    }
}

// from socketio --> send down:
void SioAdapter::onSioMessage(std::shared_ptr<Space> space, Ptr sender,
                              oatpp_sio::Message::Ptr msg)
{
    OATPP_LOGi("SADAP", "INCOMING SIo Message {} : {}", space->id(), msg->body);
    if (sender->id() == id()) {
        OATPP_LOGi("SADAP", "MSG TO SELF {} : {}", sender->id(), id());
        return;
    }

    oatpp_sio::Message::Ptr m = std::make_shared<oatpp_sio::Message>(*msg);
    m->body = encodeEvent(space->id(), m->body);

    OATPP_LOGi("SADAP", "FORWARD SIo Message {} : {}", space->id(), m->body);
    eioConn->handleMessage(m);
}

void SioAdapter::onSioEvent(const std::string& data)
{
    OATPP_LOGi("SADAP", "onSioEvent |{}|", data);

    WirePacket packet;
    if (!parsePacket(data, packet)) {
        // never publish or ack a packet we could not make sense of
        OATPP_LOGw("SADAP", "malformed event packet, dropping: |{}|", data);
        return;
    }

    OATPP_LOGi("SADAP", "onSioEvent EMIT |{}|", packet.payload);

    // The namespace is client-supplied, so it cannot be used to look up the
    // global space registry: that lets any client publish into any other
    // namespace just by naming it on the packet, and getSpace() creates the
    // space on demand while it is at it. Only publish into spaces this
    // connection joined. A packet naming one it did not is a protocol
    // violation, and the reference server closes the connection over it.
    auto space = joinedSpace(packet.nsp);
    if (!space) {
        dropConnection("event for namespace '" + packet.nsp + "' this connection did not join");
        return;
    }

    {
        auto self = eioConn->getSio();
        auto msg = std::make_shared<oatpp_sio::Message>();
        msg->body = packet.payload;
        space->publish(space, self, msg);
    }

    // ack the client - only if it asked for an ack. Sending "3" with an empty
    // ack id confuses clients.
    if (!packet.ack.empty()) {
        auto msg = std::make_shared<oatpp_sio::Message>();
        msg->body = encodeAck(packet.nsp, packet.ack);
        this->eioConn->handleMessage(msg);
    }
}

// from socketio
void SioAdapter::onSioConnect(const std::string& connTo)
{
    OATPP_LOGi("SADAP", "onSioConnect... |{}|", connTo);

    WirePacket packet;
    bool success = parsePacket(connTo, packet);

    std::string sioId;
    auto self = eioConn->getSio();
    if (success) {
        success = SioServer::serverInstance().connectToSpace(packet.nsp, self,
                                                             sioId);
    }

    auto msg = std::make_shared<oatpp_sio::Message>();
    if (success) {
        msg->body = encodeConnectAck(packet.nsp, sioId);
    } else {
        OATPP_LOGw("SADAP", "connect refused: |{}|", connTo);
        msg->body = encodeDisconnect(packet.nsp);
    }
    this->eioConn->handleMessage(msg);
}
