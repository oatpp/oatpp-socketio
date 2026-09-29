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
                // another poll request pending. kick.
                OATPP_LOGw("SIO", "SioGet {} handleLp DUP REQ", sid);
                std::string ssid = sid;
                conn->injectClose();
                theEngine->removeConnection(ssid);
                auto response =
                    controller->createResponse(Status::CODE_400, "dup req");
                return _return(response);
            }
            conn->setLongPoll(request);
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

            if (!body->size()) {
                auto response =
                    controller->createResponse(Status::CODE_400, "no body");
                return _return(response);
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
