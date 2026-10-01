#include "oatpp_sio/sio/sioServer.hpp"

using namespace oatpp_sio::sio;

SioServer* SioServer::universe = nullptr;

SioServer::SioServer()
{
    auth = std::make_shared<SioAuth>();
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

Space::Ptr SioServer::getSpace(const std::string& id)
{
    auto iter = mySpaces.find(id);
    if (iter == mySpaces.end()) {
        if (AUTOCREATE_SPACES) {
            return newSpace(id);
        }
        throw std::runtime_error("space does not exist!");
    }
    return iter->second;
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

bool SioServer::connectToSpace(const std::string& spaceName,
                               oatpp_sio::sio::SpaceListener::Ptr listener,
                               std::string& sioId)
{
    Space::Ptr space = getSpace(spaceName);

    OATPP_LOGd("SioServer", "connectToSpace -> {} ", spaceName);

    if (!space.get()) {
        OATPP_LOGd("SioServer", "connectToSpace() SioServer could not find {} ",
                   spaceName);
        return false;
    }
    // @TODO: AUTH connection here...
    bool authed = true;
    if (authed) {
        // The space keys its subscriptions by listener id, so the id handed
        // back to the client must be that id. It used to be a fresh random
        // string, which no subscription was stored under: leaveSpace() could
        // never find the entry again and spaces filled up with listeners of
        // long-gone clients.
        sioId = listener->id();
        space->addListener(listener);
        listener->subscribed(space);
    }
    return authed;
}

bool SioServer::leaveSpace(const std::string& spaceName, std::string& sioId)
{
    // lookup only - getSpace() would auto-create the space we want to leave
    auto iter = mySpaces.find(spaceName);
    if (iter == mySpaces.end()) {
        OATPP_LOGw("SioServer", "leaveSpace could not find {} ", spaceName);
        return false;
    }
    Space::Ptr space = iter->second;

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