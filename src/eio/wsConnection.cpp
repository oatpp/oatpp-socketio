#include "oatpp/async/Lock.hpp"

#include "oatpp/base/Log.hpp"

#include "oatpp-websocket/Frame.hpp"

#include "oatpp_sio/eio/wsConnection.hpp"

#include "oatpp_sio/eio/impl/engineIoImpl.hpp"

#include <iostream>
using namespace std;

static const bool dbg = true;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WSListener

// n.b. that the websocket-layer ping/pong protocol is not used; engine.io/sio uses it's own messages.
oatpp::async::CoroutineStarter WSConnection::onPing(
    const std::shared_ptr<AsyncWebSocket>& socket, const oatpp::String& message)
{
    if (dbg) OATPP_LOGd(TAG, "onPing");
    return nullptr;
}

oatpp::async::CoroutineStarter WSConnection::onPong(
    const std::shared_ptr<AsyncWebSocket>& socket, const oatpp::String& message)
{
    if (dbg) OATPP_LOGd(TAG, "onPong");
    return nullptr;  // do nothing
}

oatpp::async::CoroutineStarter WSConnection::onClose(
    const std::shared_ptr<AsyncWebSocket>& socket, v_uint16 code,
    const oatpp::String& message)
{
    if (dbg) OATPP_LOGd(TAG, "onClose code={} {}", code, message);

    return nullptr;  // do nothing
}

oatpp::async::CoroutineStarter WSConnection::readMessage(
    const std::shared_ptr<AsyncWebSocket>& socket, v_uint8 opcode, p_char8 data,
    oatpp::v_io_size size)
{
    // OATPP_LOGd(TAG, "readMsg s {}", size);
    if (size > 0) {  // message frame received
        // maxPayload is more than advertising: this buffer grows with whatever
        // arrives, so a client that ignores the limit it was handed would
        // otherwise be able to size our memory usage. The reference closes the
        // socket over an oversized message and so do we - the message is
        // dropped, not truncated, because a half packet is not meaningful.
        const auto limit =
            static_cast<unsigned long long>(oatpp_sio::eio::theEngine->maxPayload);
        const auto buffered =
            static_cast<unsigned long long>(m_messageBuffer.getCurrentPosition());
        if (buffered + static_cast<unsigned long long>(size) > limit) {
            OATPP_LOGw(TAG,
                       "ws message {}+{} bytes exceeds maxPayload {}, dropping connection",
                       buffered, size, limit);
            m_messageBuffer.setCurrentPosition(0);
            if (recv) {
                // takes the socket down as well, and unregisters the session
                recv->shutdownConnection();
            } else {
                closeSocketAsync();
            }
            return nullptr;
        }
        m_messageBuffer.writeSimple(data, size);
        return nullptr;
    }
    // else size == 0 --> packet fully received
    using namespace oatpp::websocket;

    auto wholeMessage = m_messageBuffer.toStdString();
    m_messageBuffer.setCurrentPosition(0);
    OATPP_LOGd(TAG, "onMessage RCV {}", wholeMessage);
    if (this->recv) {
        switch (opcode) {
            case Frame::OPCODE_TEXT:
                recv->handleMessageFromWs(wholeMessage, false);
                break;
            case Frame::OPCODE_BINARY:
                recv->handleMessageFromWs(wholeMessage, true);
                break;
            default:
                OATPP_LOGe(TAG, "onMessage need to handle opcode  {} for {}",
                           opcode, wholeMessage);
        }
        return nullptr;
    } else {
        OATPP_LOGe(TAG, "onMessage NO RECV YET message='{}'  {} {} op {}",
                   wholeMessage, wholeMessage.size(), this->recv == nullptr,
                   opcode);
    }

    /* Send message in reply [ to be deleted, shall not happen.. ]*/
    return socket->sendOneFrameTextAsync("Hello from oatpp!");
}

typedef oatpp::async::CoroutineStarter CoroutineStarter;

void WSConnection::wsHasClosed() { m_socket.reset(); }

void WSConnection::closeSocketAsync()
{
    class CloseConnCoroutine
        : public oatpp::async::Coroutine<CloseConnCoroutine>
    {
        const std::shared_ptr<AsyncWebSocket> sock;

       public:
        explicit CloseConnCoroutine(
            const std::shared_ptr<AsyncWebSocket>& websocket)
            : sock(websocket)
        {
        }

        Action act() override
        {
            // OATPP_LOGd(TAG, "CCCo invalidate.. ");
            sock->getConnection().invalidate();
            return finish();
        }
    };

    if (m_socket) {
        async->execute<CloseConnCoroutine>(m_socket);
    }
}

void WSConnection::sendMessageAsync(const String& message, bool isBinary)
{
    class SendMessageCoroutine
        : public oatpp::async::Coroutine<SendMessageCoroutine>
    {
       private:
        oatpp::async::Lock* m_lock;
        const std::shared_ptr<AsyncWebSocket> sock;
        oatpp::String m_message;
        bool m_binary;

       public:
        SendMessageCoroutine(oatpp::async::Lock* lock,
                             const std::shared_ptr<AsyncWebSocket>& websocket,
                             const oatpp::String& message, bool isBinary)
            : m_lock(lock),
              sock(websocket),
              m_message(message),
              m_binary(isBinary)
        {
        }

        Action act() override
        {
            if (dbg)
                OATPP_LOGd(TAG, "sendmsg message='{}' {} {}", m_message,
                           (*m_message).size(), m_binary);

            if (m_binary) {
                return oatpp::async::synchronize(
                           m_lock, sock->sendOneFrameBinaryAsync(m_message))
                    .next(finish());
            } else {
                return oatpp::async::synchronize(
                           m_lock, sock->sendOneFrameTextAsync(m_message))
                    .next(finish());
            }
            return finish();
        }
    };

    if (m_socket) {
        async->execute<SendMessageCoroutine>(&wLock, m_socket, message,
                                             isBinary);
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WSInstanceListener

std::atomic<v_int32> WSInstanceListener::SOCKETS(0);

using namespace oatpp_sio::eio;

void WSInstanceListener::onAfterCreate_NonBlocking(
    const std::shared_ptr<WSConnection::AsyncWebSocket>& socket,
    const std::shared_ptr<const ParameterMap>& params)
{
    SOCKETS++;
    String sid;

    if (params.get()) {
        auto iter = params->find("sid");
        if (iter != params->end()) {
            sid = iter->second;
        }
    }

    OATPP_LOGd(TAG, "afterCreate Connection sid? {}", sid);

    /* In this particular case we create one WSConnection per each connection */
    /* Which may be redundant in many cases */
    auto wsConn = std::make_shared<WSConnection>(socket, SOCKETS.load());
    socket->setListener(wsConn);

    if (sid) {
        // known -> upgrade
        auto eiConn = theEngine->getConnection(sid);
        if (!eiConn.get()) {
            // fake connection request or deleted stale socket id
            OATPP_LOGw(TAG, "NO Engine.io connection found for {} - ignoring!",
                       sid);
            return;
        }
        if (eiConn->getState() == connWebSocket) {
            OATPP_LOGw(
                TAG, "DUP websocket! ALREADY HAVE ONE for {} - ignoring!", sid);
            // this is a duplicate connect request - kick it.
            wsConn->closeSocketAsync();
            return;
        }
        wsConn->setMessageReceiver(eiConn);
        eiConn->upgrade(wsConn);
    } else {
        OATPP_LOGi(TAG, "registerConnection NO sid! {}", sid);
        theEngine->registerConnection(wsConn);
    }
}

void WSInstanceListener::onBeforeDestroy_NonBlocking(
    const std::shared_ptr<WSConnection::AsyncWebSocket>& socket)
{
    SOCKETS--;
    OATPP_LOGd(TAG, "Connection closed. Connection count={}", SOCKETS.load());

    auto conn = theEngine->getConnection(socket);
    if (conn.get()) {
        conn->shutdownConnection();
    } else {
        OATPP_LOGi(TAG, "could not find upper layer connections. ignoring.");
    }
}
