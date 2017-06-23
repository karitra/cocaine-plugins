/*
* 2015+ Copyright (c) Anton Matveenko <antmat@yandex-team.ru>
* All rights reserved.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*/
//
// Copy-pasted from unicorn plugin IDL
//
#include <cocaine/rpc/protocol.hpp>

#include <cocaine/unicorn/path.hpp>
#include <cocaine/unicorn/value.hpp>

#include <boost/mpl/list.hpp>

#include <vector>

namespace cocaine { namespace io {

struct unicorn_tag;
struct unicorn_final_tag;

/**
* Protocol starts with initial dispatch.
*
* All methods except lock move protocol to unicorn_final_tag,
* which provides only close leading to terminal transition.
* This is done in order to create a dispatch during transition which controls lifetime of the session.
*
* "lock" moves protocol to locked_tag which also controls lifetime of the lock.
* It has only unlock method leading to terminal transition.
*/
struct unicorn {

    struct put {
        typedef unicorn_tag tag;

        static const char* alias() {
            return "put";
        }

        /**
        *  put command implements cas behaviour. It accepts:
        *
        * path_t - path to change.
        * value_t - value to write in path
        * version_t - version to compare with. If version in zk do not match - error will be returned.
        **/
        typedef boost::mpl::list<
            cocaine::unicorn::path_t,
            cocaine::unicorn::value_t,
            cocaine::unicorn::version_t
        > argument_type;

        /**
        * Return value is current value in ZK.
        */
        typedef option_of<
            bool,
            cocaine::unicorn::versioned_value_t
        >::tag upstream_type;

        typedef unicorn_final_tag dispatch_type;
    };

    struct get {
        typedef unicorn_tag tag;

        static const char* alias() {
            return "get";
        }

        /**
        * subscribe for updates on path. Will send last update which version is greater than specified.
        */
        typedef boost::mpl::list<
            cocaine::unicorn::path_t
        > argument_type;

        /**
        * current version in ZK
        */
        typedef option_of<
            cocaine::unicorn::versioned_value_t
        >::tag upstream_type;

        typedef unicorn_final_tag dispatch_type;
    };
};

template<>
struct protocol<unicorn_tag> {
    typedef boost::mpl::int_<
        1
    >::type version;

    typedef boost::mpl::list<
        void, // unicorn::subscribe,
        void, // unicorn::children_subscribe,
        unicorn::put,
        unicorn::get,
        void, // unicorn::create,
        void, // unicorn::del,

        // alias for del method
        void, // unicorn::remove,
        void, // unicorn::increment,
        void // unicorn::lock
    > messages;

    typedef unicorn scope;
};

}} // namespace cocaine::io
