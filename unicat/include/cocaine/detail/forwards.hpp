#pragma once

#include <map>
#include <tuple>
#include <string>
#include <cstdint>

#include <cocaine/auth/uid.hpp>

#include <boost/optional.hpp>

namespace cocaine { namespace unicat {
    // (0) scheme: service type e.g. "storage", "unicorn"
    // (1) service: system wide name, if empty it is equal to scheme
    // (2) entity: path to entity - storage collection, unicorn prefix, etc.
    using entity_type = std::tuple<std::string, boost::optional<std::string>, std::string>;

    class backend_t;
}
}
