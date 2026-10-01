#include "oatpp/async/ConditionVariable.hpp"
#include "oatpp/async/Coroutine.hpp"

#include "oatpp_sio/sio/space.hpp"

#include "oatpp_sio/eio/engineIo.hpp"

#include <vector>

using namespace oatpp_sio::sio;
using namespace oatpp_sio;

static const bool dbg = false;

void Space::addListener(SpaceListener::Ptr listener)
{
    oatpp::async::LockGuard guard(&lock);
    // subscriptions are keyed by listener id; re-subscribing the same id
    // replaces the previous listener instead of silently doing nothing
    subscriptions[listener->id()] = listener;
}

void Space::removeListener(const std::string& id)
{
    oatpp::async::LockGuard guard(&lock);
    subscriptions.erase(id);
}

SpaceListener::Ptr Space::getListener(const std::string& id) const
{
    // note: the map is keyed by listener id
    auto iter = subscriptions.find(id);
    if (iter != subscriptions.end()) {
        return iter->second;
    } else {
        return nullptr;
    }
}

void Space::publish(std::shared_ptr<Space> space, SpaceListener::Ptr sender,
                    std::shared_ptr<oatpp_sio::Message> msg)
{
    // Take a snapshot of the subscribers under the lock and deliver outside
    // of it. A listener may subscribe/unsubscribe while it is being notified
    // (SioAdapter::shutdown() -> leave() does exactly that), and the lock is
    // not recursive - calling back into the space while holding it deadlocks.
    std::vector<SpaceListener::Ptr> targets;
    {
        oatpp::async::LockGuard guard(&lock);
        targets.reserve(subscriptions.size());
        for (const auto& entry : subscriptions) {
            targets.push_back(entry.second);
        }
    }

    for (const auto& target : targets) {
        target->onSioMessage(space, sender, msg);
    }
}

void Space::publishAsync(std::shared_ptr<Space> space,
                         SpaceListener::Ptr sender,
                         std::shared_ptr<oatpp_sio::Message> msg)
{
    std::vector<SpaceListener::Ptr> targets;
    {
        oatpp::async::LockGuard guard(&lock);
        targets.reserve(subscriptions.size());
        for (const auto& entry : subscriptions) {
            targets.push_back(entry.second);
        }
    }

    class PublishCoRo : public oatpp::async::Coroutine<PublishCoRo>
    {
       private:
        std::shared_ptr<Space> space;
        SpaceListener::Ptr sender, dest;
        std::shared_ptr<oatpp_sio::Message> msg;

       public:
        PublishCoRo(std::shared_ptr<Space> space, SpaceListener::Ptr sender,
                    SpaceListener::Ptr dest,
                    std::shared_ptr<oatpp_sio::Message> msg)
            : space(space), sender(sender), dest(dest), msg(msg)
        {
            // deliberately empty: the delivery happens in act() on the
            // executor, that is the whole point of publishAsync()
        }

        Action act() override
        {
            dest->onSioMessage(space, sender, msg);
            return finish();
        }
    };

    for (const auto& target : targets) {
        async->execute<PublishCoRo>(space, sender, target, msg);
    }
}
