#pragma once

#include <string>

namespace cocaine {
namespace service {
namespace node {

class pool_observer {
public:
    virtual ~pool_observer() = default;

    /// Called when a slave was spawned.
    virtual
    auto
    spawned() -> void = 0;

    /// Called when a slave was despawned.
    virtual
    auto
    despawned() -> void = 0;

    virtual
    auto
    despawned_with_id(const std::string& /* id */) -> void
    {
        despawned();
    }
};

} // namespace node
} // namespace service
} // namespace cocaine
