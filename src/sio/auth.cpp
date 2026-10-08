#include "oatpp_sio/sio/auth.hpp"

namespace oatpp_sio {
namespace sio {

// Definition of the one virtual with a body in the interface, which makes it
// the key function: the vtable is emitted here rather than in every
// translation unit that uses AuthPlugin.

bool AuthPlugin::mayPublish(const std::string& spaceName,
                            const SpaceListener::Ptr& listener)
{
    (void)spaceName;
    (void)listener;
    return true;
}

}  // namespace sio
}  // namespace oatpp_sio
