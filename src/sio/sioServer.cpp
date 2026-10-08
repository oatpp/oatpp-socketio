#include "oatpp_sio/sio/sioServer.hpp"

#include "oatpp/base/Log.hpp"

#include <utility>

using namespace oatpp_sio::sio;

SioServer* SioServer::universe = nullptr;

SioServer::SioServer()
{
    // fail open, as the library always did when nothing was configured
    auth = std::make_shared<AllowAllAuth>();
    newSpace("/");
}

SioServer::~SioServer() {
}

SioServer& SioServer::serverInstance()
{
    if (!universe) {
        universe = new SioServer();
    }
    return *universe;
}

void SioServer::setAuthPlugin(AuthPlugin::Ptr plugin)
{
    if (!plugin) {
        OATPP_LOGw("SioServer",
                   "setAuthPlugin(null) ignored, keeping the current plugin");
        return;
    }
    std::lock_guard<std::mutex> guard(stateLock);
    auth = std::move(plugin);
}

AuthPlugin::Ptr SioServer::authPlugin() const
{
    std::lock_guard<std::mutex> guard(stateLock);
    return auth;
}

void SioServer::setAutoCreateSpaces(bool enable)
{
    std::lock_guard<std::mutex> guard(stateLock);
    autoCreateSpaces = enable;
}

bool SioServer::autoCreateSpacesEnabled() const
{
    std::lock_guard<std::mutex> guard(stateLock);
    return autoCreateSpaces;
}

Space::Ptr SioServer::findSpaceLocked(const std::string& id) const
{
    auto iter = mySpaces.find(id);
    if (iter == mySpaces.end()) {
        return Space::Ptr();
    }
    return iter->second;
}

Space::Ptr SioServer::findSpace(const std::string& id) const
{
    std::lock_guard<std::mutex> guard(stateLock);
    return findSpaceLocked(id);
}

Space::Ptr SioServer::getSpace(const std::string& id)
{
    std::lock_guard<std::mutex> guard(stateLock);
    if (auto space = findSpaceLocked(id)) {
        return space;
    }
    if (autoCreateSpaces) {
        return newSpaceLocked(id);
    }
    throw std::runtime_error("space does not exist: " + id);
}

Space::Ptr SioServer::newSpaceLocked(const std::string& id)
{
    if (mySpaces.find(id) != mySpaces.end()) {
        throw std::runtime_error("space exists!");
    }
    auto spc = std::make_shared<Space>(id);
    mySpaces.insert({id, spc});
    return spc;
}

Space::Ptr SioServer::newSpace(const std::string& id)
{
    std::lock_guard<std::mutex> guard(stateLock);
    return newSpaceLocked(id);
}

size_t SioServer::spaceCount() const
{
    std::lock_guard<std::mutex> guard(stateLock);
    return mySpaces.size();
}

bool SioServer::dropSpace(const std::string& id)
{
    if (id == "/") {
        OATPP_LOGw("SioServer", "dropSpace: '/' cannot be dropped");
        return false;
    }

    std::lock_guard<std::mutex> guard(stateLock);

    auto space = findSpaceLocked(id);
    if (!space) {
        OATPP_LOGd("SioServer", "dropSpace: no namespace '{}'", id);
        return false;
    }

    // dropping a namespace with members would orphan their subscriptions: they
    // would keep a reference to a space the server no longer knows about. This
    // check and the erase below are one critical section, and connectToSpace()
    // joins under the same lock, so a member cannot appear in between.
    if (space->size() > 0) {
        OATPP_LOGw("SioServer",
                   "dropSpace: '{}' still has {} listener(s), refusing", id,
                   space->size());
        return false;
    }

    mySpaces.erase(id);
    OATPP_LOGi("SioServer", "dropped namespace '{}'", id);
    return true;
}

bool SioServer::connectToSpace(const std::string& spaceName,
                               oatpp_sio::sio::SpaceListener::Ptr listener,
                               std::string& sioId)
{
    std::string ignored;
    return connectToSpace(spaceName, listener, sioId, ignored);
}

bool SioServer::connectToSpace(const std::string& spaceName,
                               oatpp_sio::sio::SpaceListener::Ptr listener,
                               std::string& sioId, std::string& reason)
{
    OATPP_LOGd("SioServer", "connectToSpace -> {} ", spaceName);

    // both are output parameters: whatever the caller left in them is not
    // ours to interpret. Without this, a reused buffer reads back as the
    // refusal reason and a plugin that declines silently inherits it.
    sioId.clear();
    reason.clear();

    // 1. the namespace has to exist. It is only created here when the
    // application opted in to clients deciding the namespace set; with the
    // default (off) this is a lookup and nothing else, so a client cannot make
    // the server allocate a namespace by naming one.
    Space::Ptr space;
    {
        std::lock_guard<std::mutex> guard(stateLock);
        space = findSpaceLocked(spaceName);
        if (!space && autoCreateSpaces) {
            try {
                space = newSpaceLocked(spaceName);
                OATPP_LOGi("SioServer",
                           "connectToSpace: auto-created namespace '{}'",
                           spaceName);
            } catch (const std::runtime_error&) {
                // cannot happen while holding the lock - nothing else can have
                // created the name in the meantime - so it is a bug, not a race
                OATPP_LOGe("SioServer",
                           "connectToSpace: '{}' vanished while being created",
                           spaceName);
                return false;
            }
        }
    }

    if (!space) {
        OATPP_LOGw("SioServer", "connectToSpace: no namespace '{}'", spaceName);
        reason = "Invalid namespace";
        return false;
    }

    // 2. the application decides who gets in. Outside the lock and on a local
    // copy of the pointer: a plugin may call back into the server, and it must
    // not be able to make a decision with a plugin that was replaced while it
    // was running.
    const AuthPlugin::Ptr plugin = authPlugin();
    if (!plugin->mayConnect(spaceName, listener, reason)) {
        if (reason.empty()) {
            // the plugin declined to say why; do not send an empty message
            reason = "Not authorized";
        }
        OATPP_LOGw("SioServer",
                   "connectToSpace: '{}' refused for listener {}: {}", spaceName,
                   listener->id(), reason);
        return false;
    }

    // 3. join, under the lock, and only if the namespace is still the one that
    // was looked up - dropSpace() may have retired it while the plugin was
    // thinking.
    {
        std::lock_guard<std::mutex> guard(stateLock);
        if (findSpaceLocked(spaceName) != space) {
            OATPP_LOGw("SioServer",
                       "connectToSpace: '{}' was retired while connecting",
                       spaceName);
            reason = "Invalid namespace";
            return false;
        }

        // The space keys its subscriptions by listener id, so the id handed
        // back to the client must be that id. It used to be a fresh random
        // string, which no subscription was stored under: leaveSpace() could
        // never find the entry again and spaces filled up with listeners of
        // long-gone clients.
        sioId = listener->id();
        space->addListener(listener);
    }

    // notified outside the lock: this is application code, and it may well
    // want to look namespaces up
    listener->subscribed(space);
    return true;
}

bool SioServer::leaveSpace(const std::string& spaceName, std::string& sioId)
{
    // lookup only - getSpace() would create the space we want to leave
    auto space = findSpace(spaceName);
    if (!space) {
        OATPP_LOGw("SioServer", "leaveSpace could not find {} ", spaceName);
        return false;
    }

    // notify before removing: getListener() cannot find it once it is gone
    // (the old order made the left() callback dead code)
    auto listener = space->getListener(sioId);
    if (!listener.get()) {
        OATPP_LOGd("SioServer", "leaveSpace: {} is not a member of {}", sioId,
                   spaceName);
        return false;
    }

    listener->left(space);
    space->removeListener(sioId);
    return true;
}
