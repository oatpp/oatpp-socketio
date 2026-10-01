#include <iostream>
#include <sstream>

#include <oatpp/encoding/Base64.hpp>

#include "oatpp_sio/eio/connection.hpp"
#include "oatpp_sio/eio/wsConnection.hpp"
#include "oatpp_sio/eio/engineIo.hpp"

#include "oatpp_sio/sio/adapter.hpp"

using namespace oatpp_sio::eio;
using namespace std;

static const bool dbg = true;

static const char SEPARATOR = '\x1e';  // ASCII RS

static oatpp::encoding::Base64 scrambler;

EioConnection::EioConnection(Engine& engine, std::string sid)
    : sid(sid), state(connInit), engine(engine), connType(polling)
{
    if (dbg) cout << "EICO::EICO " << sid << "  " << this << endl;
}

EioConnection::~EioConnection()
{
    if (dbg) cout << "EICO:~EICO " << sid << "  " << this << endl;
}

void EioConnection::print() const
{
    if (dbg)
        cout << "EICO " << sid << " " << (char)getState() << " " << getWsConn()
             << endl;
}

void EioConnection::setSio(SioAdapterPtr adapter, Ptr self)
{
    sioFunnel = adapter;
    adapter->eioConn = self;
}

// from pool:
void EioConnection::handleMessage(std::shared_ptr<Message> msg)
{
    handleMessageAsync(theEngine->getConnection(sid), msg);
}

// the synchronous part:
void EioConnection::handleMessageReal(std::shared_ptr<Message> msg)
{
    if (dbg)
        OATPP_LOGd("EICO", "{} handleMessage state {} ws {} msg {}", sid,
                   (char)getState(), wsConn.get() != nullptr, msg->body);
    if (getState() == connClosed) {
        if (dbg)
            OATPP_LOGw("EICO",
                       "UNSUB - should not happen!");  // we don't come here!
        if (space.get()) {
            space->unsubscribe(sid);
        }
        if (sioFunnel.get()) {
            sioFunnel->shutdown();
        }
        return;
    }
    if (getState() == connWebSocket) {
        if (msg->binary) {
            this->wsConn->sendMessageAsync(msg->body, msg->binary);
        } else {
            this->wsConn->sendMessageAsync(pktEncode(eioMessage, msg->body),
                                           msg->binary);
        }
        return;
    }
    // else queue for long-polling:
    if (msg->binary) {
        auto bin = scrambler.encode(msg->body);
        enqMsg(pktEncode(eioBinary, bin));
    } else {
        enqMsg(pktEncode(eioMessage, msg->body));
    }
}

void EioConnection::upgrade(WsConnPtr ws, bool webSockOnly)
{
    OATPP_LOGw("EICO", "{} upgrade()??? webSockOnly {} ", sid, webSockOnly);
    wsConn = ws;
    // get the shared pointer of self:
    wsConn->setMessageReceiver(theEngine->getConnection(sid));

    if (webSockOnly) {
        connType = websocket;
        state = connWebSocket;
    } else {
        // in the middle of the upgrade process
        state = connUpgrading;
    }
}

bool EioConnection::handleMessageFromWs(const std::string& body, bool isBinary)
{
    // OATPP_LOGd("EICO", "{} handleMessageFromWs msg {} {}", sid, msgs.size(),
    //            body);

    if (isBinary) {
        auto msg = std::make_shared<Message>();
        msg->body = body;
        msg->binary = isBinary;
        if (sioFunnel.get()) {
            OATPP_LOGd("EICO", "{} handleMessageFromWs msg {} {} FUNNEL!", sid,
                       msgs.size(), body);
            sioFunnel->onEioMessage(msg);
        } else {
            OATPP_LOGd("EICO", "{} handleMessageFromWs msg {} {} SPACY!", sid,
                       msgs.size(), body);
            space->publish(msg);
        }
        return true;
    }

    handleEioMessage(body, false);
    return true;
}

void EioConnection::handleEioMessage(const std::string& body, bool isBinary)
{
    char pType = body[0];

    // if (dbg)
    //     OATPP_LOGd("EICO", "{} handleEioMessage |{}| {} {} st {} pt {}", sid,
    //                body, body.size(), isBinary, (int)state, pType);

    switch (pType) {
        case eioClose:
            if (dbg) OATPP_LOGd("EICO", "{} handleEioMessage close", sid);
            shutdownConnection();
            break;
        case eioPing:
            if (state == connUpgrading) {
                if (body == "2probe") {
                    if (dbg)
                        OATPP_LOGd("EICO", "{} handleEioMessage ping probe! :D",
                                   sid);
                    this->wsConn->sendMessageAsync(pktEncode(eioPong, "probe"),
                                                   false);
                    checkUpgradeTimeout(theEngine->getConnection(sid));
                    sendDelayedNoop(theEngine->getConnection(sid), 100);
                }
            } else {
                OATPP_LOGw("EICO", "{} handleEioMessage NO PING ALLOWED!", sid);
                shutdownConnection();
            }
            break;
        case eioPong:
            if (dbg) OATPP_LOGd("EICO", "{} handleEioMessage pong", sid);
            pongCount++;
            // reset timer
            break;
        case eiouUgrade:
            if (dbg)
                OATPP_LOGd("EICO",
                           "{} handleEioMessage upgrade #MSGS {} LP? {}", sid,
                           msgs.size(), longPollRequest.get() != nullptr);

            connType = websocket;
            state = connWebSocket;

            // close pending long-poll request
            sendDelayedNoop(theEngine->getConnection(sid), 1);

            // send any pending messages that might have queued up:
            if (msgs.size()) {
                this->wsConn->sendMessageAsync(deqMsg(), false);
            }
            break;
        case eioMessage: {
            if (dbg) OATPP_LOGd("EICO", "{} handleEioMessage msg", sid);
            // strip off header and forward data:
            auto msg = std::make_shared<Message>();
            msg->body = std::string(body.data() + 1, body.size() - 1);
            msg->binary = false;
            if (sioFunnel.get()) {
                sioFunnel->onEioMessage(msg);
            } else {
                space->publish(msg);
            }
            break;
        }
        case eioBinary: {
            auto bin =
                scrambler.decode(std::string(body.data() + 1, body.size() - 1));
            auto msg = std::make_shared<Message>();
            msg->body = bin;
            msg->binary = true;
            if (sioFunnel.get()) {
                sioFunnel->onEioMessage(msg);
            } else {
                space->publish(msg);
            }
            break;
        }
        default: {
            OATPP_LOGw("EICO", "handleEioMessage unknown type {}", (int)pType);

            // this->wsConn->sendMessageAsync(pktEncode(eioClose, ""), false);

            shutdownConnection();
        }
    }
}
void EioConnection::setLongPoll(RequestPtr r)
{
    longPollRequest = r;
    // noop it
    if (getState() == connUpgrading) {
        sendDelayedNoop(theEngine->getConnection(sid), theEngine->pingTimeout);
    }
}

void EioConnection::wsHasClosed()
{
    state = connClosed;
    shutdownConnection();
}

// from long-poll:
void EioConnection::handleLpPostMessage(const std::string& body)
{
    // handleEioMessage(body, false);
    stringstream ss(body);
    string tok;

    while (getline(ss, tok, SEPARATOR)) {
        // cout << "HDL " << tok << endl;
        handleEioMessage(tok, false);
    }
}

// unsubscribe from space, close wsconn, remove sid from engine
void EioConnection::shutdownConnection()
{
    state = connClosed;
    // clear downstream refs
    engine.removeConnection(sid);

    // clear upstream refs
    if (space.get()) {
        space->unsubscribe(sid);
        space.reset();
    }
    if (sioFunnel.get()) {
        sioFunnel->shutdown();
        sioFunnel.reset();
    }
    msgs.clear();

    if (wsConn.get()) {
        wsConn->closeSocketAsync();
        wsConn->wsHasClosed();
        wsConn.reset();
    }
    if (longPollRequest) {
        cout << "SHUT: LPnoti " << endl;
        lpSema.notifyAll();
        longPollRequest.reset();
    }
}

std::string EioConnection::deqMsg()
{
    oatpp::async::LockGuard guard(&lpLock);
    // if (!msgs.size()) {
    //     return "";
    // }
    if (dbg) OATPP_LOGd("EICO", "deqMsg # {} ", msgs.size());
    stringstream ss;
    auto iter = msgs.begin();

    while (iter != msgs.end()) {
        ss << *iter;
        iter++;
        if (iter != msgs.end()) {
            ss << SEPARATOR;
        }
    }
    msgs.clear();
    return ss.str();
}

bool EioConnection::hasMsgs() const { return msgs.size() > 0; }

void EioConnection::enqMsg(const std::string& msg)
{
    {
        oatpp::async::LockGuard guard(&lpLock);
        msgs.push_back(msg);
    }
    if (dbg)
        OATPP_LOGd("EICO", "{} ENQ MSG {} s {} |{}|", sid, msgs.size(),
                   state == connUpgrading, msg);
    if (state != connUpgrading) {
        lpSema.notifyAll();
    }
}

void EioConnection::sendPingAsync()
{
    if (getState() == connWebSocket) {
        // assert wsConn
        if (wsConn.get()) {
            wsConn->sendMessageAsync(pktEncode(eioPing, ""), false);
        }
    } else {
        enqMsg(pktEncode(eioPing, ""));
    }
}

// run handleMessage in coroutine
void EioConnection::handleMessageAsync(EioConnection::Ptr conn,
                                       Message::Ptr msg)
{
    class HandleMsg : public oatpp::async::Coroutine<HandleMsg>
    {
       private:
        // EioConnection::Ptr conn;
        std::string sid;
        EioConnection::Ptr conn;
        Message::Ptr msg;
        unsigned long delay;
        bool first = true;

       public:
        HandleMsg(const std::string& sid, EioConnection::Ptr conn,
                  Message::Ptr msg, unsigned long timeout)
            : sid(sid), conn(conn), msg(msg), delay(timeout)
        {
        }

        Action act() override
        {
            OATPP_LOGd("EICO:MSG", "{} TO....", sid);
            if (first) {
                // perform initial delay
                first = false;
                return waitRepeat(1 * std::chrono::milliseconds(delay));
            }
            conn->handleMessageReal(msg);
            return finish();
        }
    };

    async->execute<HandleMsg>(sid, conn, msg, 100);
}

// execute a timer & close connection if not upgraded after timeout
void EioConnection::sendDelayedNoop(EioConnection::Ptr conn,
                                    unsigned int delayMs)
{
    class DelayNoop : public oatpp::async::Coroutine<DelayNoop>
    {
       private:
        // EioConnection::Ptr conn;
        std::string sid;
        EioConnection::Ptr conn;
        unsigned long delay;
        bool first = true;

       public:
        DelayNoop(const std::string& sid, EioConnection::Ptr conn,
                  unsigned long timeout)
            : sid(sid), conn(conn), delay(timeout)
        {
        }

        Action act() override
        {
            // OATPP_LOGd("EICO:NOOP", "{} TO....", sid);
            if (first) {
                // perform initial delay
                first = false;
                return waitRepeat(1 * std::chrono::milliseconds(delay));
            }
            if (conn->longPollRequest.get()) {
                if (dbg) OATPP_LOGi("EICO:NOOP", "{} sending..", sid);
                conn->enqMsg(pktEncode(eioNoop, ""));
            }
            return finish();
        }
    };

    async->execute<DelayNoop>(sid, conn, delayMs);
}

void EioConnection::checkUpgradeTimeout(EioConnection::Ptr conn)
{
    class UpgradeTo : public oatpp::async::Coroutine<UpgradeTo>
    {
       private:
        // EioConnection::Ptr conn;
        std::string sid;
        EioConnection::Ptr conn;
        unsigned long to;
        bool first = true;

       public:
        UpgradeTo(const std::string& sid, EioConnection::Ptr conn,
                  unsigned long timeout)
            : sid(sid), conn(conn), to(timeout)
        {
        }

        Action act() override
        {
            OATPP_LOGd("EICO:UPTO", "{} TO....", sid);
            if (first) {
                // perform initial delay
                first = false;
                return waitRepeat(1 * std::chrono::milliseconds(to));
            }
            if (conn->getState() == connClosed) {
                // abort - socket was closed elsewhere
                if (dbg)
                    OATPP_LOGd("EICO:UPTO", "{} conn was closed, leaving", sid);
                return finish();
            }
            if (conn->getState() != connWebSocket) {
                // timed out - did not upgrade in time
                if (dbg)
                    OATPP_LOGi("EICO:UPTO",
                               "{} Conn not upgraded! s={} - closing!", sid,
                               (char)conn->getState());
                // conn->shutdownConnection();
                return finish();
            } else {
                // upgraded. stop.
                return finish();
            }
        }
    };

    async->execute<UpgradeTo>(sid, conn, engine.pingTimeout);
}

void EioConnection::checkPongTimeout(EioConnection::Ptr conn)
{
    class TimeoutCoRo : public oatpp::async::Coroutine<TimeoutCoRo>
    {
       private:
        std::string sid;
        EioConnection::Ptr conn;
        unsigned long to;
        bool first = true;

       public:
        TimeoutCoRo(const std::string& sid, EioConnection::Ptr conn,
                    unsigned long timeout)
            : sid(sid), conn(conn), to(timeout)
        {
        }

        Action shutdown()
        {
            OATPP_LOGd("EICO:PO", "{} shutdown", sid);
            conn->shutdownConnection();
            return finish();
        }

        Action act() override
        {
            if (first) {  // perform initial delay
                first = false;
                return waitRepeat(1 * std::chrono::milliseconds(to));
            }
            OATPP_LOGd("EICO:PO", "{} PO # {} ", sid, conn->pongCount);

            if (conn->getState() == connClosed) {
                if (dbg)
                    OATPP_LOGd("EICO:PO", "{} conn was closed, leaving", sid);
                return finish();
            }
            if (conn->getState() == connUpgrading) {
                // ignore during upgrade process.
                if (dbg)
                    OATPP_LOGd("EICO:TO", "{} conn upgrading - ignore..", sid);
                return finish();
            }
            if (!conn->pongCount) {
                if (dbg) OATPP_LOGi("EICO:PO", "{} NO Pong! - closing!", sid);
                conn->enqMsg(pktEncode(eioClose, ""));
                return yieldTo(&TimeoutCoRo::shutdown);
            }
            conn->pongCount = 0;
            return finish();
        }
    };

    async->execute<TimeoutCoRo>(sid, conn, engine.pingTimeout);
}

void EioConnection::scheduleDelayedPingMsg()
{
    class SendDelayedPingCoroutine
        : public oatpp::async::Coroutine<SendDelayedPingCoroutine>
    {
       private:
        std::string sid;
        EioConnection::Ptr conn;
        unsigned long pi, pt;
        bool pingSent = false;
        long count, idle, notYet;
        bool first = true;

       public:
        SendDelayedPingCoroutine(const std::string& sid,
                                 EioConnection::Ptr conn,
                                 unsigned long pingInterval,
                                 unsigned long pingTimeout)
            : sid(sid),
              conn(conn),
              pi(pingInterval),
              pt(pingTimeout),
              count(-1),
              idle(0),
              notYet(0)
        {
        }

        Action act() override
        {
            if (first) {  // perform initial delay
                first = false;
                return waitRepeat(std::chrono::milliseconds(pi));
            }

            int pongs = conn->getPongCount();

            if (conn->getState() == connClosed) {
                if (dbg)
                    OATPP_LOGi("EICO:SCHED",
                               "{} ping state {} p={} conn was closed, leaving",
                               sid, conn->getState(), pongs);
                return finish();
            }
            if (conn->getState() == connUpgrading) {
                if (dbg)
                    OATPP_LOGd("EICO:SCHED",
                               "{} detected upgrade, bailing out.", sid,
                               conn->getState(), pongs);
                return finish();
            }
            // if (conn->getState() == connUpgrading) {
            //     // skip during upgrade..
            //     return waitRepeat(std::chrono::milliseconds(pi));
            // }

            OATPP_LOGi("EICO:SCHED", "{} ping.. # {}", sid, conn->pongCount);
            // pongCount is reset in the pongtimeout handler
            conn->checkPongTimeout(conn);
            conn->sendPingAsync();

            return waitRepeat(std::chrono::milliseconds(pi));
        }
    };
    EioConnection::Ptr conn = theEngine->getConnection(sid);
    if (!conn.get()) {
        OATPP_LOGi("EICO:PING", "NO CONN YET?!? Try next time....");
    }
    OATPP_LOGd("EICO:SCHED", "{} START", sid);

    async->execute<SendDelayedPingCoroutine>(sid, conn, engine.pingInterval,
                                             engine.pingTimeout);
}