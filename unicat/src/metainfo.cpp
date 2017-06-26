#include "cocaine/auth/metainfo.hpp"
#include "cocaine/idl/unicat.hpp"

#if 0
#define dbg(msg) std::cerr << msg << '\n';
#else
#define dbg(msg)
#endif

namespace cocaine { namespace auth {

auto operator<<(std::ostream& os, const metainfo_t& meta) -> std::ostream&
{
   os << "cids:\n";
   for(const auto& cid : meta.c_perms) {
       os << "  " << cid.first << " : " << cid.second << '\n';
   }

   os << "uids:\n";
   for(const auto& uid : meta.u_perms) {
       os << "  " << uid.first << " : " << uid.second << '\n';
   }

   return os;
}


namespace detail {
    using base_type = std::underlying_type<auth::flags_t>::type;

    template<typename Flags>
    auto
    flags_to_uint(Flags&& fl) -> base_type {
        return static_cast<base_type>(fl);
    }

    auto
    uint_to_flags(const base_type base) -> auth::flags_t {
        return static_cast<auth::flags_t>(base);
    }

    auth::flags_t operator|=(auth::flags_t& a, const auth::flags_t b) {
        return a = uint_to_flags(flags_to_uint(a) | flags_to_uint(b));
    }

    auth::flags_t operator|=(auth::flags_t& a, const base_type b) {
        return a = uint_to_flags(flags_to_uint(a) | b);
    }

    auth::flags_t operator&=(auth::flags_t& a, const auth::flags_t b) {
        return a = uint_to_flags(flags_to_uint(a) & flags_to_uint(b));
    }

    auth::flags_t operator&=(auth::flags_t& a, const base_type b) {
        return a = uint_to_flags(flags_to_uint(a) & b);
    }

    auth::flags_t operator~(const auth::flags_t a) {
        return uint_to_flags( ~flags_to_uint(a) );
    }
}

template<>
auto
alter<io::unicat::revoke>(auth::metainfo_t& metainfo, const auth::alter_data_t& data) -> void
{
    using namespace detail;

    dbg("unsetting flags " << data.flags);

    for(const auto& cid : data.cids) {
        dbg("removing for cid: " << cid);
        auto it = metainfo.c_perms.find(cid);
        if (it != std::end(metainfo.c_perms)) {
            it->second &= ~ data.flags;
        }
    }

    for(const auto& uid : data.uids) {
        dbg("removing for uid: " << uid);
        auto it = metainfo.u_perms.find(uid);
        if (it != std::end(metainfo.u_perms)) {
            it->second &= ~ data.flags;
        }
    }
}

template<>
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo, const auth::alter_data_t& data) -> void
{
    using namespace detail;

    dbg("setting flags " << data.flags);
    for(const auto& cid : data.cids) {
        dbg("adding for cid: " << cid);
        metainfo.c_perms[cid] |= data.flags;
    }

    for(const auto& uid : data.uids) {
        dbg("adding for uid: " << uid);
        metainfo.c_perms[uid] |= data.flags;
    }
}

template
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo, const auth::alter_data_t&) -> void;

template
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo, const auth::alter_data_t&) -> void;

}
}
