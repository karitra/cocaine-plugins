// TODO: deprecated, remove candidat
#pragma once

#include "cocaine/idl/unicat.hpp"

namespace cocaine { namespace unicat {

    // Mapping among different access flags representations,
    // probably may be transformed into CRUD+ flags someday
    // (TODO: should be descussed).
    // TODO: deprecated, should be removed, use flags_t directly
    struct rights_t {

        rights_t() = default;
        rights_t(auth::flags_t flags) : flags{flags}
        {}

        auto is_read() const -> bool  { return static_cast<int>(flags) & 1; }
        auto is_write() const -> bool { return static_cast<int>(flags) & 2; }
        auto is_both() const -> bool  { return static_cast<int>(flags) & 3; }

        // CRUD+ section, experimental
        auto can_create() const -> bool { return is_both();  }
        auto can_read() const -> bool   { return is_read();  }
        auto can_update() const -> bool { return is_write(); }
        auto can_delete() const -> bool { return is_write(); }
        auto can_execute() const -> bool { return true; }

        auto get_flags() const -> auth::flags_t { return flags; }
    private:

        auth::flags_t flags;
    };
}
}
