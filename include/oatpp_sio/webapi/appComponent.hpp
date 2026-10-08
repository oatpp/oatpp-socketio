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

#include "oatpp/web/server/interceptor/RequestInterceptor.hpp"
#include "oatpp/web/server/AsyncHttpConnectionHandler.hpp"
#include "oatpp/web/mime/ContentMappers.hpp"
#include "oatpp/web/server/HttpRouter.hpp"

#include "oatpp/network/tcp/server/ConnectionProvider.hpp"

#include "oatpp/json/ObjectMapper.hpp"

#include "oatpp/macro/component.hpp"

#include "oatpp_sio/webapi/swaggerComponent.hpp"
#include "oatpp_sio/webapi/webApp.hpp"

#include "oatpp_sio/eio/wsConnection.hpp"

/**
 *  Class which creates and holds Application components and registers components in oatpp::base::Environment
 *  Order of components initialization is from top to bottom
 */
class AppComponent
{
    // class RedirectInterceptor
    //     : public oatpp::web::server::interceptor::RequestInterceptor
    // {
    //    private:
    //     OATPP_COMPONENT(oatpp::Object<ConfigDto>, appConfig);

    //    public:
    //     std::shared_ptr<OutgoingResponse> intercept(
    //         const std::shared_ptr<IncomingRequest>& request) override
    //     {
    //         auto host =
    //             request->getHeader(oatpp::web::protocol::http::Header::HOST);
    //         auto siteHost = appConfig->getHostString();
    //         if (!host || host != siteHost) {
    //             auto response = OutgoingResponse::createShared(
    //                 oatpp::web::protocol::http::Status::CODE_301, nullptr);
    //             response->putHeader(
    //                 "Location", appConfig->getCanonicalBaseUrl() +
    //                                 request->getStartingLine().path.toString());
    //             return response;
    //         }
    //         return nullptr;
    //     }
    // };

   public:
    /**
   *  Swagger component
   */
    SwaggerComponent swaggerComponent;

    /**
   * Create Async Executor
   */
    OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::async::Executor>, executor)
    ("http", [] {
        return std::make_shared<oatpp::async::Executor>(
            1 /* Data threads */, 1 /* I/O  */, 1 /* Timer  */
        );
    }());
    OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::async::Executor>, executorWs)
    ("ws", [] {
        return std::make_shared<oatpp::async::Executor>(
            4 /* Data threads */, 1 /* I/O  */, 1 /* Timer  */
        );
    }());
    /**
   *  Create ConnectionProvider component which listens on the port
   */
    OATPP_CREATE_COMPONENT(
        std::shared_ptr<oatpp::network::ServerConnectionProvider>,
        serverConnectionProvider)
    ([] {
        // host/port come from OATPP_SIO_HOST / OATPP_SIO_PORT (8000 default)
        const std::string host = oatpp_sio::webapi::getListenHost();
        const unsigned short port = oatpp_sio::webapi::getListenPort();

        return oatpp::network::tcp::server::ConnectionProvider::createShared(
            {host.c_str(), port, oatpp::network::Address::IP_4});
    }());

    /**
   *  Create Router component
   */
    OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>,
                           httpRouter)
    ([] { return oatpp::web::server::HttpRouter::createShared(); }());

    /**
       *  Create ConnectionHandler component which uses Router component to route requests
       */
    OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>,
                           serverConnectionHandler)
    ("http", [] {
        OATPP_COMPONENT(std::shared_ptr<oatpp::async::Executor>,
                        executor, "http");  // get Async executor component
        OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>,
                        router);  // get Router component
        auto handler =
            oatpp::web::server::AsyncHttpConnectionHandler::createShared(
                router, executor);
        // handler->addRequestInterceptor(std::make_shared<RedirectInterceptor>());
        return handler;
    }());

    /**
   *  Create ObjectMapper component to serialize/deserialize DTOs in Contoller's API
   */
    OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::web::mime::ContentMappers>,
                           apiContentMappers)
    ([] {
        auto json = std::make_shared<oatpp::json::ObjectMapper>();
        json->serializerConfig().json.useBeautifier = true;

        auto mappers = std::make_shared<oatpp::web::mime::ContentMappers>();
        mappers->putMapper(json);

        return mappers;
    }());

    /**
 *  Create websocket connection handler
 */
    OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>,
                           websocketConnectionHandler)
    ("websocket", [] {
        OATPP_COMPONENT(std::shared_ptr<oatpp::async::Executor>, executor, "http");
        auto connectionHandler =
            oatpp::websocket::AsyncConnectionHandler::createShared(executor);
        connectionHandler->setSocketInstanceListener(
            std::make_shared<WSInstanceListener>());
        return connectionHandler;
    }());
};
