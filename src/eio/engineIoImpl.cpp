#include <iostream>
#include "oatpp/json/Serializer.hpp"

#include "oatpp_sio/eio/impl/engineIoImpl.hpp"

#include "oatpp_sio/eio/wsConnection.hpp"

#include "oatpp_sio/util.hpp"

using namespace std;

using namespace oatpp_sio::eio;

Engine* oatpp_sio::eio::theEngine = new EngineImpl();

EngineImpl::EngineImpl() {}
EngineImpl::~EngineImpl() {}

/*******************************
 * *****************************
 * *****************************
 * *****************************
 * *****************************
 * *****************************
 * *****************************
 */

void EngineImpl::removeConnection(std::string& sid)
{
    oatpp::async::LockGuard guard(&lock);
    auto iter = connections.find(sid);
    if (iter != connections.end()) {
        // iter->second.close();
        connections.erase(iter);
    }
}

EioConnection::Ptr EngineImpl::getConnection(const std::string& sid)
{
    oatpp::async::LockGuard guard(&lock);
    auto iter = connections.find(sid);
    if (iter != connections.end()) {
        return iter->second;
    }
    OATPP_LOGw("EI", "getConnection could not find matching sid: " + sid);
    printSockets();
    return nullptr;
}

EioConnection::Ptr EngineImpl::getConnection(const WebsocketPtr& socket)
{
    oatpp::async::LockGuard guard(&lock);
    auto iter = connections.begin();
    while (iter != connections.end()) {
        if (iter->second->getWsConn() &&
            iter->second->getWsConn()->theSocket() == socket) {
            return iter->second;
        }
        iter++;
    }

    OATPP_LOGw("EI", "getConnection could not find matching websocket! + ws");
    printSockets();
    return nullptr;
}

void EngineImpl::printSockets() const
{
    auto iter = connections.begin();
    cout << "EI STORE #entries " << connections.size() << endl;
    while (iter != connections.end()) {
        EioConnectionPtr conn = iter->second;
        cout << "EI STORE ";
        conn->print();
        iter++;
    }
}

Engine::ResponsePtr EngineImpl::startLpConnection(
    const oatpp::web::server::api::ApiController* controller,
    const std::shared_ptr<oatpp::web::protocol::http::incoming::Request> req)

{
    std::string sid = generateSid();
    std::shared_ptr<EioConnection> conn;
    {
        oatpp::async::LockGuard guard(&lock);
        conn = std::make_shared<EioConnection>(*this, sid);

        connections.insert({sid, conn});

        if (testMode) {  // @TODO remove test code
            conn->setSpace(theSpace);
            theSpace->subscribe(sid, conn);
        } else {
            conn->setSio(std::make_shared<oatpp_sio::sio::SioAdapter>(sid),
                         conn);
        }
    }

    OATPP_LOGd("EIO", "EngineIoController: startLpConnection > {}", sid);

    auto dto = EioStartup::createShared();
    dto->sid = sid;
    dto->upgrades->push_back("websocket");
    dto->pingInterval = pingInterval;
    dto->pingTimeout = pingTimeout;
    dto->maxPayload = maxPayload;

    oatpp::String s = om->writeToString(dto);
    const std::string& ss = *s;

    OATPP_LOGd("EIO", "EngineIoController: startLpConnection {}", s);

    std::string msg = pktEncode(eioOpen, ss);

    auto response = controller->createResponse(Status::CODE_200, msg);
    response->putHeader("Content-Type", "text/plain");

    conn->scheduleDelayedPingMsg();

    return response;
}

std::string EngineImpl::generateSid()
{
    std::string sid = generateRandomString(12);

    return "sid_" + sid;
}

void EngineImpl::registerConnection(std::shared_ptr<WSConnection> wsConn)
{
    OATPP_LOGd("EIO", "EngineIoController: registerConnection");
    // no sid. start new.
    std::string sid = generateSid();
    auto conn = std::make_shared<EioConnection>(*this, sid);
    connections.emplace(sid, conn);

    auto dto = EioStartup::createShared();
    dto->sid = sid;
    // no upgrades, as we've upgraded already
    // dto->upgrades->push_back("websocket");
    dto->pingInterval = pingInterval;
    dto->pingTimeout = pingTimeout;
    dto->maxPayload = maxPayload;

    oatpp::String s = om->writeToString(dto);

    const std::string& ss = *s;

    OATPP_LOGd("EIO", "EngineIoController: registerConnection {}", s);

    if (testMode) {
        theSpace->subscribe(sid, conn);
        conn->setSpace(theSpace);
    } else {
        conn->setSio(std::make_shared<oatpp_sio::sio::SioAdapter>(sid), conn);
    }
    wsConn->setMessageReceiver(conn);

    std::string msg = pktEncode(eioOpen, ss);

    wsConn->sendMessageAsync(msg);

    conn->upgrade(wsConn, true);

    conn->scheduleDelayedPingMsg();
}
