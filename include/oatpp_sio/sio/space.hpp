#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// timer functionality...:
#include "oatpp/async/Lock.hpp"
#include "oatpp/async/ConditionVariable.hpp"

#include "oatpp/async/Executor.hpp"

#include "oatpp/macro/component.hpp"

#include "oatpp_sio/message.hpp"
// #include "oatpp_sio/eio/messageReceiver.hpp"

namespace oatpp_sio {
namespace sio {

class Space;

class SpaceListener
{
    std::string myId;

    SpaceListener(const SpaceListener&) = delete;
    SpaceListener& operator=(const SpaceListener&) = delete;

   public:  // convenience type-defs
    typedef std::shared_ptr<SpaceListener> Ptr;

   public:
    SpaceListener(const std::string& id) : myId(id) {}

    const std::string& id() const { return myId; }

    virtual void onSioMessage(std::shared_ptr<Space> space, Ptr sender,
                              oatpp_sio::Message::Ptr msg) = 0;

    virtual void subscribed(std::shared_ptr<Space> space) {}
    virtual void left(std::shared_ptr<Space> space) {}
};

class Space
{
    std::string myId;

    Space(const Space&) = delete;
    Space& operator=(const Space&) = delete;

    std::unordered_map<std::string, SpaceListener::Ptr> subscriptions;

    /**
     * Lock for the subscriptions map.
     *
     * A plain std::mutex rather than an oatpp::async::Lock. Every critical
     * section here is a map operation: it does not block, and it never calls
     * back into a listener (publish() takes a snapshot and delivers outside the
     * lock), so holding it never keeps an executor thread busy. It also has to
     * be takeable from getListener() and size(), which run on both plain
     * threads and coroutines - and oatpp's coroutine lock must not be taken in
     * thread-blocking mode from inside a coroutine, which is how the two forms
     * of using one lock deadlock: the holder yields its coroutine, the waiter
     * blocks the thread that coroutine needs.
     *
     * Order against SioServer's registry lock is registry -> space and never
     * the other way round; Space does not know that class exists.
     */
    mutable std::mutex lock;

    OATPP_COMPONENT(std::shared_ptr<oatpp::async::Executor>, async, "ws");

   public:
    Space(const std::string& id) : myId(id) {}
    virtual ~Space() {}

    const std::string& id() { return myId; }

    const std::unordered_map<std::string, SpaceListener::Ptr>& subs()
    {
        return subscriptions;
    }

    /** number of subscribers. Synchronised; returns int for now (a narrowing
     *  the whole code base assumes, worth changing in one go) */
    int size() const {
        std::lock_guard<std::mutex> guard(lock);
        return static_cast<int>(subscriptions.size());
    }

    void addListener(SpaceListener::Ptr listener);

    void removeListener(const std::string& id);

    /** @return the listener registered under @p id, or null. Synchronised -
     *  reading the map while another thread inserts into it is a rehash under
     *  your feet, not a benign race. */
    SpaceListener::Ptr getListener(const std::string& id) const;

    void publish(std::shared_ptr<Space> space, SpaceListener::Ptr sender,
                 std::shared_ptr<oatpp_sio::Message> msg);

    void publishAsync(std::shared_ptr<Space> space, SpaceListener::Ptr sender,
                      std::shared_ptr<oatpp_sio::Message> msg);

   public:  // convenience type-defs
    typedef std::shared_ptr<Space> Ptr;
};

}  // namespace sio
}  // namespace oatpp_sio
