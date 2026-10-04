/*
   Copyright 2012-2025 Simon Vogl <svogl@voxel.at> VoXel Interaction Design - www.voxel.at

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#pragma once

#include <string>
#include <memory>

#include "oatpp/async/Coroutine.hpp"

#include "oatpp/macro/codegen.hpp"
#include "oatpp/macro/component.hpp"

#include "oatpp/web/server/api/ApiController.hpp"

#include "oatpp-websocket/Handshaker.hpp"

#include "oatpp_sio/eio/engineIo.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)  /// <-- Begin Code-Gen

// static bool debugSync = false;

class SocketIoController : public oatpp::web::server::api::ApiController
{
    std::string prefix;

   private:
    OATPP_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>,
                    websocketConnectionHandler, "websocket");

   private:
    typedef SocketIoController __ControllerType;

    std::shared_ptr<oatpp::data::mapping::ObjectMapper> om;

   private:
    // OATPP_COMPONENT(std::shared_ptr<Lobby>, lobby);

   public:
    SocketIoController(
        std::string prefix,
        OATPP_COMPONENT(std::shared_ptr<oatpp::web::mime::ContentMappers>,
                        apiContentMappers))
        : oatpp::web::server::api::ApiController(apiContentMappers),
          prefix(prefix)
    {
        om = apiContentMappers->getDefaultMapper();
        OATPP_LOGd("SIO", "SocketIoController: PFX {}", this->prefix);

        OATPP_LOGd("SIO", "SocketIoController: mapper serializing to {}/{}",
                   om->getInfo().mimeType, om->getInfo().mimeSubtype);
    }

   public:  // ENDPOINTS:
            // GET - connection startup
    ENDPOINT_INFO(SioGet)
    {
        info->summary = "Socket.IO";
        info->description = "Socket.IO default endpoint";
        info->addResponse<String>(Status::CODE_200, "text/plain");
        info->queryParams.add<String>("SIO").description =
            "protocol version (4)";
        info->queryParams.add<String>("transport").description =
            "transport method (polling)";
    }
    ENDPOINT_ASYNC("GET", prefix + String("/*"), SioGet)
    {
        ENDPOINT_ASYNC_INIT(SioGet);

        const bool dbg = true;
        oatpp_sio::eio::EioConnection::Ptr conn;

        /** microsecond tick at which this request started holding the poll */
        v_int64 waitStartedAt = 0;

        Action handleWebSocket()
        {
            using namespace oatpp_sio::eio;

            auto sid = request->getQueryParameter("sid");
            // immediately start a ws connection, sending the open packet over ws
            if (!sid) {
                // the ws / engine handling is done in the websocketConnectionHandler
                auto response =
                    oatpp::websocket::Handshaker::serversideHandshake(
                        request->getHeaders(),
                        controller->websocketConnectionHandler);
                return _return(response);
            } else {
                conn = theEngine->getConnection(sid);
                if (!conn) {
                    // stale connection id
                    auto response =
                        controller->createResponse(Status::CODE_400, "no sid");
                    return _return(response);
                }
    
                if (conn->getState() == connUpgrading || conn->getState() == connWebSocket) {
                    // already upgraded to websocket. deny access.
                    auto response = controller->createResponse(Status::CODE_400, "busy upgrading");
                    return _return(response);
                }
                
                if (dbg) OATPP_LOGd("SIO", "SioGet {} WS UPGRADE", sid);

                auto parameters = std::make_shared<
                    oatpp::network::ConnectionHandler::ParameterMap>();
                (*parameters)["sid"] = sid;

                auto response =
                    oatpp::websocket::Handshaker::serversideHandshake(
                        request->getHeaders(),
                        controller->websocketConnectionHandler);
                response->setConnectionUpgradeParameters(parameters);

                return _return(response);
            }
        }

        Action handleLongPoll()
        {
            using namespace oatpp_sio::eio;
            auto sid = request->getQueryParameter("sid");
            // OATPP_LOGd("SIO", "SioGet {} handleLp", sid);

            if (!sid) {
                if (dbg) OATPP_LOGd("SIO", "SioGet {} handleLp START", sid);
                // create a new connection
                auto response =
                    theEngine->startLpConnection(controller, request);
                return _return(response);
            }
            // new
            conn = theEngine->getConnection(sid);
            if (!conn.get()) {
                // stale connection id
                auto response =
                    controller->createResponse(Status::CODE_400, "no conn");
                return _return(response);
            }

            if (conn->getState() == connWebSocket) {
                // already upgraded to websocket. deny access.
                auto response = controller->createResponse(Status::CODE_400);
                return _return(response);
            }
            // if (conn->getState() == connUpgrading) {
            //     if (dbg)
            //         OATPP_LOGd("SIO", "SioGet {} handleLp IN UPGRADE", sid);

            //     // auto response =
            //     //     controller->createResponse(Status::CODE_200, "6");  // NOOP
            //     // return _return(response);

            //     // return yieldTo(&SioGet::sendNoop);
            //     return yieldTo(&SioGet::wait4Msgs);
            // }
            if (conn->hasLongPoll()) {
                // Another poll is already pending. Answer the overlap with 400
                // and leave the connection alone, which is what the reference
                // does (engine.io transports/polling.ts onPollRequest reports
                // "overlap from client", answers 400, and keeps the
                // connection). Closing it here, as this used to, meant that a
                // client which re-polled after its own client-side timeout
                // destroyed its own session.
                OATPP_LOGw("SIO", "SioGet {} handleLp DUP REQ (rejected; "
                                  "connection kept)",
                           sid);
                auto response = controller->createResponse(
                    Status::CODE_400, "duplicate poll request");
                return _return(response);
            }
            conn->setLongPoll(request);
            waitStartedAt = oatpp::Environment::getMicroTickCount();
            return yieldTo(&SioGet::wait4Msgs);
        }

        Action sendNoop()
        {
            auto response =
                controller->createResponse(Status::CODE_200, "6");  // NOOP
            return _return(response);
        }

        Action wait4Msgs()
        {
            using namespace oatpp_sio::eio;
            auto sid = request->getQueryParameter("sid");

            // assert conn is set...
            if (conn->getState() == connClosed) {
                if (dbg)
                    OATPP_LOGd("CTRL", "SioGet {} wait4Msgs connClosed", sid);
                return _return(
                    controller->createResponse(Status::CODE_400, "closed"));
            }
            if (conn->hasMsgs()) {
                auto msg = conn->deqMsg();

                conn->clearLongPoll();

                if (dbg)
                    OATPP_LOGd("CTRL", "SioGet {} wait4Msgs GOT [{}]", sid,
                               msg);
                auto response =
                    controller->createResponse(Status::CODE_200, msg);
                response->putHeader("Content-Type", "text/plain");
                return _return(response);
            }

            // A long-poll must not be held forever. The reference ends the
            // response after pingInterval (with a ping) so the client can
            // re-poll; holding it open indefinitely meant a client whose own
            // timeout was shorter than forever - i.e. every client - either
            // stalled or, on re-polling, tripped the overlap check above. An
            // empty 200 is a valid "nothing to send" answer.
            const v_int64 heldUs =
                oatpp::Environment::getMicroTickCount() - waitStartedAt;
            if (heldUs > (v_int64)theEngine->pingInterval * 1000) {
                if (dbg)
                    OATPP_LOGd("CTRL", "SioGet {} wait4Msgs held {}us, ending",
                               sid, heldUs);
                conn->clearLongPoll();
                auto response = controller->createResponse(Status::CODE_200, "");
                response->putHeader("Content-Type", "text/plain");
                return _return(response);
            }

            if (dbg) OATPP_LOGd("CTRL", "SioGet {} wait4Msgs... {}", sid, conn->getSid());
            return oatpp::async::Action::createWaitRepeatAction(
                100 * 1000 + oatpp::Environment::getMicroTickCount());
        }

        Action act() override
        {
            auto sio = request->getQueryParameter("EIO");
            auto transport = request->getQueryParameter("transport");
            auto sid = request->getQueryParameter("sid");
            if (dbg)
                OATPP_LOGd("SIO", "SioGet {} ************** GET {} t {}", sid,
                           sio, transport);

            // check pre-conditions:
            if (!sio || sio != "4") {
                auto response = controller->createResponse(Status::CODE_400);
                return _return(response);
            }

            if (!transport ||
                (transport != "polling" && transport != "websocket")) {
                return _return(controller->createResponse(Status::CODE_400));
            }

            if (transport == "websocket") {
                return yieldTo(&SioGet::handleWebSocket);
            } else {
                return yieldTo(&SioGet::handleLongPoll);
            }
        }
    };

    ENDPOINT_ASYNC("POST", prefix + String("/*"), SioPost)
    {
        ENDPOINT_ASYNC_INIT(SioPost);

        const bool dbg = true;
        oatpp_sio::eio::EioConnection::Ptr conn;

        Action withBody(const String& body)
        {
            // assert conn is set!

            if (!body || !body->size()) {
                auto response =
                    controller->createResponse(Status::CODE_400, "no body");
                return _return(response);
            }

            // The second of the two maxPayload checks: Content-Length is a
            // claim, and a chunked body never declared one at all, so the size
            // that actually arrived is what decides here.
            if (oatpp_sio::eio::theEngine->exceedsMaxPayload(
                    static_cast<long long>(body->size()))) {
                OATPP_LOGw("SIO",
                           "POST body of {} bytes exceeds maxPayload {}, refusing",
                           body->size(),
                           oatpp_sio::eio::theEngine->maxPayload);
                return _return(controller->createResponse(
                    Status::CODE_413, "payload too large"));
            }

            conn->handleLpPostMessage(body);

            auto response = controller->createResponse(Status::CODE_200, "ok");
            return _return(response);
        }

        Action act() override
        {
            using namespace oatpp_sio::eio;

            auto sio = request->getQueryParameter("EIO");
            auto transport = request->getQueryParameter("transport");
            auto sid = request->getQueryParameter("sid");

            if (dbg)
                OATPP_LOGd("SIO", "SioPost {} POST {} t {}", sid, sio,
                           transport);

            if (!sio || sio != "4" || !sid) {
                return _return(controller->createResponse(Status::CODE_400));
            }

            if (!transport ||
                (transport != "polling" && transport != "websocket")) {
                return _return(controller->createResponse(Status::CODE_400));
            }

            conn = theEngine->getConnection(sid);
            if (!conn) {
                return _return(controller->createResponse(Status::CODE_400,
                                                          "sid not found"));
            }
            if (dbg)
                OATPP_LOGd("SIO", "SioPost {} POST ok {} t {} s {}", sid, sio,
                           transport);

            // First maxPayload check, before the body is read: a client that
            // ignores what the OPEN packet advertises must not be able to make
            // this server buffer whatever it decides to send. Only the request
            // is refused - the session survives, because an oversized body is
            // not a protocol violation on the connection.
            const long long declared = declaredBodyLength(request);
            if (theEngine->exceedsMaxPayload(declared)) {
                OATPP_LOGw("SIO",
                           "POST declares {} bytes, maxPayload is {}, refusing",
                           declared, theEngine->maxPayload);
                return _return(controller->createResponse(
                    Status::CODE_413, "payload too large"));
            }

            return request->readBodyToStringAsync().callbackTo(
                &SioPost::withBody);
        }
    };

    // make the test-suite happy (otherwise we report 404)
    ENDPOINT_ASYNC("PUT", prefix + String("/*"), SioPut)
    {
        ENDPOINT_ASYNC_INIT(SioPut);

        Action act() override
        {
            auto response = controller->createResponse(Status::CODE_400);
            return _return(response);
        }
    };
};

#include OATPP_CODEGEN_END(ApiController)  /// <-- End Code-Gen
