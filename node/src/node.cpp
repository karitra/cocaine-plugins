/*
    Copyright (c) 2011-2014 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2014 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "cocaine/service/node.hpp"

#include <cocaine/api/storage.hpp>

#include <cocaine/context.hpp>
#include <cocaine/context/signal.hpp>
#include <cocaine/logging.hpp>

#include <cocaine/traits/dynamic.hpp>
#include <cocaine/traits/endpoint.hpp>
#include <cocaine/traits/graph.hpp>
#include <cocaine/traits/tuple.hpp>
#include <cocaine/traits/vector.hpp>

#include "cocaine/service/node/app.hpp"

#include <blackhole/logger.hpp>
#include <blackhole/scope/holder.hpp>

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/spirit/include/karma_char.hpp>
#include <boost/spirit/include/karma_generate.hpp>
#include <boost/spirit/include/karma_list.hpp>
#include <boost/spirit/include/karma_string.hpp>

#include "cocaine/service/node/overseer.hpp"

using namespace cocaine;
using namespace cocaine::service;

using cocaine::service::node::overseer_t;

namespace ph = std::placeholders;

namespace {

class control_slot_t:
    public io::basic_slot<io::node::control_app>
{
    struct controlling_slot_t:
        public io::basic_slot<io::node::control_app>::dispatch_type
    {
        typedef io::basic_slot<io::node::control_app>::dispatch_type super;

        typedef io::event_traits<io::node::control_app>::dispatch_type dispatch_type;
        typedef io::protocol<dispatch_type>::scope protocol;

        std::shared_ptr<overseer_t> overseer;

        controlling_slot_t(const std::string& name, control_slot_t* p):
            super(format("controlling/{}", name))
        {
            overseer = p->parent.overseer(name);
            if (overseer == nullptr) {
                throw cocaine::error_t("app '{}' is not available", name);
            }

            on<protocol::chunk>([&](int size) {
                overseer->control_population(size);
            });
        }

        virtual
        void
        discard(const std::error_code&) const {
            overseer->control_population(0);
        }
    };

    typedef std::vector<hpack::header_t> meta_type;
    typedef std::shared_ptr<const io::basic_slot<io::node::control_app>::dispatch_type> result_type;

    node_t& parent;

public:
    explicit control_slot_t(node_t& parent):
        parent(parent)
    {}

    boost::optional<result_type>
    operator()(tuple_type&& args, upstream_type&& upstream) {
        return operator()({}, std::move(args), std::move(upstream));
    }

    boost::optional<result_type>
    operator()(const meta_type&, tuple_type&& args, upstream_type&&) {
        const auto dispatch = tuple::invoke(std::move(args), [&](const std::string& name) -> result_type {
            return std::make_shared<controlling_slot_t>(name, this);
        });

        return boost::make_optional(dispatch);
    }
};

}  // namespace

node_t::node_t(context_t& context, asio::io_service& asio, const std::string& name, const dynamic_t& args):
    category_type(context, asio, name, args),
    dispatch<io::node_tag>(name),
    log(context.log(name)),
    context(context)
{
    on<io::node::start_app>(std::bind(&node_t::start_app, this, ph::_1, ph::_2));
    on<io::node::pause_app>(std::bind(&node_t::pause_app, this, ph::_1));
    on<io::node::list>     (std::bind(&node_t::list, this));
    on<io::node::info>     (std::bind(&node_t::info, this, ph::_1, ph::_2));
    on<io::node::control_app>(std::make_shared<control_slot_t>(*this));

    // Context signal/slot.
    signal = std::make_shared<dispatch<io::context_tag>>(name);
    signal->on<io::context::shutdown>(std::bind(&node_t::on_context_shutdown, this));

    const auto runname = args.as_object().at("runlist", "").as_string();

    if(runname.empty()) {
        context.signal_hub().listen(signal, asio);
        return;
    }

    COCAINE_LOG_INFO(log, "reading '{}' runlist", runname);

    typedef std::map<std::string, std::string> runlist_t;
    runlist_t runlist;

    const auto storage = api::storage(context, "core");

    try {
        // TODO: Perform request to a special service, like "storage->runlist(runname)".
        runlist = storage->get<runlist_t>("runlists", runname).get();
    } catch(const std::system_error& err) {
        COCAINE_LOG_WARNING(log, "unable to read '{}' runlist: {}", runname, err.what());
    }

    if(runlist.empty()) {
        context.signal_hub().listen(signal, asio);
        return;
    }

    COCAINE_LOG_INFO(log, "starting {} app(s)", runlist.size());

    std::vector<std::string> errored;

    for(auto it = runlist.begin(); it != runlist.end(); ++it) {
        const blackhole::scope::holder_t scoped(*log, {{ "app", it->first }});

        try {
            start_app(it->first, it->second);
        } catch(const std::exception& e) {
            COCAINE_LOG_WARNING(log, "unable to initialize app: {}", e.what());
            errored.push_back(it->first);
        }
    }

    if(!errored.empty()) {
        std::ostringstream stream;
        std::ostream_iterator<char> builder(stream);

        boost::spirit::karma::generate(builder, boost::spirit::karma::string % ", ", errored);

        COCAINE_LOG_WARNING(log, "couldn't start {} app(s): {}", errored.size(), stream.str());
    }

    context.signal_hub().listen(signal, asio);
}

node_t::~node_t() = default;

auto
node_t::prototype() const -> const io::basic_dispatch_t&{
    return *this;
}

void
node_t::on_context_shutdown() {
    // TODO: In fact this method may not be invoked during context shutdown - race - node service
    // can be terminated earlier than this completion handler be invoked.
    COCAINE_LOG_DEBUG(log, "shutting down apps");

    apps->clear();

    signal = nullptr;
}

deferred<void>
node_t::start_app(const std::string& name, const std::string& profile) {
    COCAINE_LOG_DEBUG(log, "processing `start_app` request, app: '{}'", name);

    struct deferred<void> deferred;

    apps.apply([&](std::map<std::string, std::shared_ptr<node::app_t>>& apps) {
        auto it = apps.find(name);

        if(it != apps.end()) {
            const auto info = it->second->info(io::node::info::brief).as_object();
            throw std::system_error(error::already_started,
                cocaine::format("app '{}' is {}", name, info["state"].as_string()));
        }

        apps.insert({name, std::make_shared<node::app_t>(context, name, profile, deferred)});
    });

    return deferred;
}

void
node_t::pause_app(const std::string& name) {
    COCAINE_LOG_DEBUG(log, "processing `pause_app` request, app: '{}'", name);

    apps.apply([&](std::map<std::string, std::shared_ptr<node::app_t>>& apps) {
        auto it = apps.find(name);

        if(it == apps.end()) {
            throw std::system_error(error::not_running,
                cocaine::format("app '{}' is not running", name));
        }

        apps.erase(it);
    });
}

auto
node_t::list() const -> dynamic_t {
    dynamic_t::array_t result;

    apps.apply([&](const std::map<std::string, std::shared_ptr<node::app_t>>& apps) {
        boost::copy(apps | boost::adaptors::map_keys, std::back_inserter(result));
    });

    return result;
}

dynamic_t
node_t::info(const std::string& name, io::node::info::flags_t flags) const {
    auto app = apps.apply([&](const std::map<std::string, std::shared_ptr<node::app_t>>& apps) -> std::shared_ptr<node::app_t> {
        auto it = apps.find(name);

        if(it != apps.end()) {
            return it->second;
        }

        return nullptr;
    });

    if (!app) {
        throw cocaine::error_t("app '{}' is not running", name);
    }

    return app->info(flags);
}

std::shared_ptr<node::overseer_t>
node_t::overseer(const std::string& name) const {
    auto app = apps.apply([&](const std::map<std::string, std::shared_ptr<node::app_t>>& apps) -> std::shared_ptr<node::app_t> {
        auto it = apps.find(name);

        if(it != apps.end()) {
            return it->second;
        }

        return nullptr;
    });

    if (!app) {
        throw cocaine::error_t("app '{}' is not running", name);
    }

    return app->overseer();
}
