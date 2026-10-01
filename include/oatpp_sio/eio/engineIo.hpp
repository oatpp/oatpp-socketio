#pragma once

#include <string>
#include <memory>

#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/async/Coroutine.hpp"

#include "oatpp-websocket/AsyncWebSocket.hpp"

#include "oatpp_sio/eio/messagePool.hpp"

class WSConnection;

namespace oatpp_sio {
namespace eio {

class EioConnection;
class MessagePool;

class Engine
{
   protected:
    typedef std::shared_ptr<EioConnection> EioConnectionPtr;
    typedef oatpp::websocket::AsyncWebSocket AsyncWebSocket;
    typedef oatpp::web::server::api::ApiController ApiController;
    typedef oatpp::web::protocol::http::incoming::Request Request;
    typedef std::shared_ptr<ApiController::OutgoingResponse> ResponsePtr;
    typedef std::shared_ptr<oatpp_sio::eio::MessagePool> PoolPtr;
    typedef std::shared_ptr<oatpp::websocket::AsyncWebSocket> WebsocketPtr;

    std::shared_ptr<oatpp_sio::eio::MessagePool> theSpace;

   public:
    /** engine.io ping interval in milliseconds. It is advertised verbatim in
     *  the OPEN packet and used as-is by the ping coroutine - socket.io
     *  defaults are 25000 ms / 20000 ms. */
    unsigned int pingInterval = 25000;
    /** milliseconds to wait for a pong before the connection is dropped */
    unsigned int pingTimeout = 20000;
    /** maximum payload size in bytes, advertised to the client */
    unsigned int maxPayload = 1000000;
    // todo: list of known connections here?

   public:  // convenience typedefs
            // typedef std::shared_ptr<oatpp_sio::eio::MessagePool> PoolPtr;
   public:
    Engine() : theSpace(std::make_shared<MessagePool>()) {}
    virtual ~Engine() {}

    /**
     * Configure the engine.
     * @param interval ping interval in **milliseconds**.
     * @param timeout ping timeout in **milliseconds**.
     * @param maxSize maximum payload size in bytes.
     */
    void setConfig(unsigned int interval, unsigned int timeout,
                   unsigned int maxSize)
    {
        pingInterval = interval;
        pingTimeout = timeout;
        maxPayload = maxSize;
    }
    // for engine.io protocol tests enable this to override the actual socket.io layer:
    bool testMode = false;

    /** start a long-polling connection - assingns a sid and registers a connection onject.
     * @param controller
     * @param req
     */
    virtual ResponsePtr startLpConnection(
        const oatpp::web::server::api::ApiController* controller,
        const std::shared_ptr<oatpp::web::protocol::http::incoming::Request>
            req) = 0;

    // websocket
    virtual void registerConnection(std::shared_ptr<WSConnection> wsConn) = 0;

    virtual void removeConnection(std::string& sid) = 0;

    virtual EioConnectionPtr getConnection(const std::string& sid) = 0;

    virtual EioConnectionPtr getConnection(const WebsocketPtr& socket) = 0;

    void setSpace(PoolPtr spc) { theSpace = spc; }

    PoolPtr getSpace() const { return theSpace; }
};

extern Engine* theEngine;

}  // namespace eio
}  // namespace oatpp_sio