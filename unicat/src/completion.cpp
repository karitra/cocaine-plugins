#include "completion.hpp"

namespace cocaine { namespace unicat {

completion_t::completion_t(const url_t url):
    url(url),
    done(true)
{}

completion_t::completion_t(const url_t url, const int ec, const Opcode opcode):
    url(url),
    done(false),
    error_code(ec),
    opcode(opcode)
{}

completion_t::completion_t(const url_t url, std::exception_ptr eptr):
    url(url),
    done(false),
    error_code{},
    eptr(std::move(eptr))
{}

auto completion_t::make_access_error() const -> std::string {
    const auto sch = scheme_to_string(url.scheme);

    switch (opcode) {
        case ReadOp:
            return cocaine::format("read permission denied {}:{}", sch, url.entity);
        case WriteOp:
            return cocaine::format("write permission denied {}:{}", sch, url.entity);
        case Nop:
            break;
    }

    return "unknown access error";
}

auto completion_t::make_exception_error() const -> std::string {
    try {
        std::rethrow_exception(eptr);
    } catch(const std::system_error& err) {
        return cocaine::format("code: {} msg: {}", err.code(), err.what());
    } catch(const std::exception& err) {
        return cocaine::format("code: nil msg {}", err.what());
    } // TODO: else?
}

auto completion_t::has_error() const -> bool {
    return done == false;
}

auto completion_t::has_error_code() const -> bool {
    return error_code;
}

}
}
