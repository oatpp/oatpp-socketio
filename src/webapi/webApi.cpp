/*
   Copyright 2012-2025 Simon Vogl <svogl@voxel.at> VoXel Interaction Design -
   www.voxel.at

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

#include <iostream>
#include <thread>

#include "oatpp/async/Executor.hpp"
#include "oatpp/network/Server.hpp"
#include "oatpp/network/tcp/client/ConnectionProvider.hpp"

#include "oatpp-swagger/AsyncController.hpp"
#include "oatpp-swagger/Controller.hpp"

// #include "oatpp_sio/webapi/controller/eioController.hpp"
#include "oatpp_sio/webapi/controller/sioController.hpp"
#include "oatpp_sio/webapi/controller/staticContentsController.hpp"

#include "oatpp_sio/webapi/appComponent.hpp"
#include "oatpp_sio/webapi/swaggerComponent.hpp"
#include "oatpp_sio/webapi/webApp.hpp"

#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>

// #include "oatpp_sio/globals.hpp"

static oatpp::network::Server *theServer = nullptr;
static std::shared_ptr<oatpp::network::ServerConnectionProvider> theConnectionProvider;
static std::thread *runner = nullptr;
static oatpp_sio::WebApiState *systemState = nullptr;

/* startup handshake: webApiStop() must not race with the runner thread
 * bringing the server up */
static std::mutex startupMutex;
static std::condition_variable startupCv;
static bool serverUp = false;
static bool stopRequested = false;

using namespace std;

namespace oatpp_sio {
namespace webapi {

/** helper class to enforce calling init() before initializing the
 * AppComponent(!) */
class Init
{
   public:
    Init() { oatpp::Environment::init(); }
};

class WebApi
{
   public:
    Init init;  // n.b. keep these two in order!
    AppComponent components;

    std::vector<std::shared_ptr<oatpp::web::server::api::ApiController>>
        controllers;

    WebApi() {}

    void run()
    {
        /* Get router component */
        OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>,
                        router);

        oatpp::web::server::api::Endpoints docEndpoints;

        /**
     * REGISTER CONTROLLERS
     */

        for (size_t i = 0; i < controllers.size(); i++) {
            auto ctrl = controllers[i];
            router->addController(ctrl);
            docEndpoints.append(ctrl->getEndpoints());
        }

        if (systemState->enableSwaggerUi) {
            router->addController(
                oatpp::swagger::AsyncController::createShared(docEndpoints));
        }

        // static contents:
        router->addController(std::make_shared<StaticContentsController>());

        /**
     * /REGISTER CONTROLLERS
     */

        /* Get connection handler component */
        OATPP_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>,
                        connectionHandler, "http");

        /* Get connection provider component */
        OATPP_COMPONENT(
            std::shared_ptr<oatpp::network::ServerConnectionProvider>,
            connectionProvider);

        /* Create server which takes provided TCP connections and passes them to
     * HTTP connection handler */
        oatpp::network::Server server(connectionProvider, connectionHandler);
        theServer = &server;
        theConnectionProvider = connectionProvider;

        {
            std::lock_guard<std::mutex> guard(startupMutex);
            serverUp = true;
            if (stopRequested) {
                // stop() was called before we got up and running - do not
                // enter the (blocking) accept loop at all
                OATPP_LOGi("WEBAPI", "stop requested before startup");
                theServer = nullptr;
                theConnectionProvider.reset();
                return;
            }
        }
        startupCv.notify_all();

        /* Print info about server port */
        OATPP_LOGi("WEBAPI", "Server running on port {}",
                   connectionProvider->getProperty("port").toString());

        /* Run server. The condition is re-checked before every accept, so a
         * stopRequested that raced with the startup still stops the loop
         * instead of blocking in accept() forever. The explicit std::function
         * avoids the deprecated run(bool) overload. */
        const std::function<bool()> stillRunning = [] { return !stopRequested; };
        server.run(stillRunning);

        OATPP_LOGi("WEBAPI", "Server stopped on port {}",
                   connectionProvider->getProperty("port").toString());
    }
};

static WebApi *api = nullptr;

void registerController(
    std::shared_ptr<oatpp::web::server::api::ApiController> controller)
{
    api->controllers.push_back(controller);
}

static void addInternalControllers()
{
    //add controllers that are generally needed here.
    // registerController(std::make_shared<EngineIoController>());
    registerController(std::make_shared<SocketIoController>("/sio"));
    registerController(std::make_shared<SocketIoController>("/socket.io"));
    registerController(std::make_shared<SocketIoController>("/engine.io"));
}

void addSwaggerServerUrl(const std::string &url, const std::string &name)
{
    SwaggerComponent::addSwaggerServerUrl(url, name);
}

void webApiInit()
{
    api = new WebApi();
}

/**
 *  main
 */
static int webApiRunner()
{
    addInternalControllers();

    api->run();

    // note: tearing down the oatpp Environment is up to the application - a
    // library must not shut down the environment of its caller
    return 0;
}

void webApiStart(oatpp_sio::WebApiState &state)
{
    cout << "oatpp_sio::WEBAPPSTART" << endl;
    systemState = &state;
    runner = new std::thread(webApiRunner);
}

void webApiStop()
{
    {
        std::unique_lock<std::mutex> lock(startupMutex);
        stopRequested = true;
        if (!serverUp) {
            // give the runner thread a moment to publish the server (or to
            // notice that it must not start it at all)
            startupCv.wait_for(lock, std::chrono::seconds(5),
                               [] { return serverUp; });
        }
    }

    if (theServer) {
        theServer->stop();

        // Server::stop() only sets a status flag. The server thread sits in
        // accept(), which does not return when the status changes - and on
        // Linux closing the listening socket does not reliably wake it
        // either. One connection makes the accept return, the main loop then
        // sees the new status and leaves, which unblocks the join below.
        try {
            auto poke = oatpp::network::tcp::client::ConnectionProvider::createShared(
                {"127.0.0.1", getListenPort(), oatpp::network::Address::IP_4});
            poke->get();
        } catch (const std::exception& e) {
            OATPP_LOGd("WEBAPI", "could not wake the accept loop: {}", e.what());
        }
    }

    if (theConnectionProvider) {
        theConnectionProvider->stop();
    }

    theServer = nullptr;
    theConnectionProvider.reset();

    if (runner) {
        if (runner->joinable()) {
            runner->join();
        }
        delete runner;
        runner = nullptr;
    }
}

void webApiDestroy()
{
    if (api == nullptr) {
        return;
    }

    // stop the executors the components created before they are destroyed -
    // ~Executor() on a running executor aborts the process
    const auto stopExecutor = [](const std::shared_ptr<oatpp::async::Executor>& executor) {
        if (executor) {
            executor->stop();
            executor->join();
        }
    };

    {
        OATPP_COMPONENT(std::shared_ptr<oatpp::async::Executor>, httpExecutor, "http");
        stopExecutor(httpExecutor);
    }
    {
        OATPP_COMPONENT(std::shared_ptr<oatpp::async::Executor>, wsExecutor, "ws");
        stopExecutor(wsExecutor);
    }

    // destroys the AppComponent, which unregisters the oatpp components so
    // that oatpp::Environment::destroy() no longer sees them as leaking
    delete api;
    api = nullptr;
}

std::string getListenHost()
{
    const char* host = std::getenv("OATPP_SIO_HOST");
    if (host != nullptr && *host != '\0') {
        return std::string(host);
    }
    return std::string("0.0.0.0");
}

unsigned short getListenPort()
{
    const unsigned short defaultPort = 8000;

    const char* port = std::getenv("OATPP_SIO_PORT");
    if (port == nullptr || *port == '\0') {
        return defaultPort;
    }

    char* end = nullptr;
    const long value = std::strtol(port, &end, 10);
    if (end == port || *end != '\0' || value <= 0 || value > 65535) {
        OATPP_LOGw("WEBAPI",
                   "invalid OATPP_SIO_PORT '{}', using {}", port,
                   defaultPort);
        return defaultPort;
    }
    return static_cast<unsigned short>(value);
}

}  // namespace webapi
}  // namespace oatpp_sio
