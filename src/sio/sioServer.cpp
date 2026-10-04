#include "oatpp_sio/sio/sioServer.hpp"

#include "oatpp/base/Log.hpp"

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
    auth = plugin;
}

Space::Ptr SioServer::findSpace(const std::string& id) const
{
    auto iter = mySpaces.find(id);
    if (iter == mySpaces.end()) {
        return Space::Ptr();
    }
    return iter->second;
}

Space::Ptr SioServer::getSpace(const std::string& id)
{
    if (auto space = findSpace(id)) {
        return space;
    }
    if (autoCreateSpaces) {
        return newSpace(id);
    }
    throw std::runtime_error("space does not exist: " + id);
}

Space::Ptr SioServer::newSpace(const std::string& id)
{
    auto iter = mySpaces.find(id);
    if (iter != mySpaces.end()) {
        throw std::runtime_error("space exists!");
    }
    auto spc = std::make_shared<Space>(id);
    mySpaces.insert({id, spc});
    return spc;
}

bool SioServer::dropSpace(const std::string& id)
{
    if (id == "/") {
        OATPP_LOGw("SioServer", "dropSpace: '/' cannot be dropped");
        return false;
    }

    auto space = findSpace(id);
    if (!space) {
        OATPP_LOGd("SioServer", "dropSpace: no namespace '{}'", id);
        return false;
    }

    // dropping a namespace with members would orphan their subscriptions: they
    // would keep a reference to a space the server no longer knows about
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
    Space::Ptr space = findSpace(spaceName);
    if (!space && autoCreateSpaces) {
        try {
            space = newSpace(spaceName);
            OATPP_LOGi("SioServer",
                       "connectToSpace: auto-created namespace '{}'", spaceName);
        } catch (const std::runtime_error&) {
            // somebody else won the race for this name; use theirs
            space = findSpace(spaceName);
        }
    }
    if (!space) {
        OATPP_LOGw("SioServer", "connectToSpace: no namespace '{}'", spaceName);
        reason = "Invalid namespace";
        return false;
    }

    // 2. the application decides who gets in
    if (!auth->mayConnect(spaceName, listener, reason)) {
        if (reason.empty()) {
            // the plugin declined to say why; do not send an empty message
            reason = "Not authorized";
        }
        OATPP_LOGw("SioServer", "connectToSpace: '{}' refused for listener {}: {}",
                   spaceName, listener->id(), reason);
        return false;
    }

    // 3. join
    //
    // The space keys its subscriptions by listener id, so the id handed back
    // to the client must be that id. It used to be a fresh random string,
    // which no subscription was stored under: leaveSpace() could never find
    // the entry again and spaces filled up with listeners of long-gone
    // clients.
    sioId = listener->id();
    space->addListener(listener);
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
