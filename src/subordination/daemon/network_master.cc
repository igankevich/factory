#include <algorithm>
#include <iterator>

#include <unistdx/base/log_message>
#include <unistdx/it/field_iterator>
#include <unistdx/it/intersperse_iterator>
#include <unistdx/net/interface_addresses>

#include <subordination/daemon/factory.hh>
#include <subordination/daemon/job_status_kernel.hh>
#include <subordination/daemon/network_master.hh>
#include <subordination/daemon/pipeline_status_kernel.hh>
#include <subordination/daemon/status_kernel.hh>
#include <subordination/daemon/terminate_kernel.hh>

namespace {

    template <class T, class X>
    std::unordered_set<T>
    set_difference_copy(
        const std::unordered_map<T,X>& lhs,
        const std::unordered_set<T>& rhs
    ) {
        typedef typename std::unordered_map<T,X>::value_type value_type;
        std::unordered_set<T> result;
        for (const value_type& value : lhs) {
            if (rhs.find(value.first) == rhs.end()) {
                result.emplace(value.first);
            }
        }
        return result;
    }

    template <class T, class X>
    std::unordered_set<T>
    set_difference_copy(
        const std::unordered_set<T>& lhs,
        const std::unordered_map<T,X>& rhs
    ) {
        std::unordered_set<T> result;
        for (const T& value : lhs) {
            if (rhs.find(value) == rhs.end()) {
                result.emplace(value);
            }
        }
        return result;
    }

}

void sbnd::network_master::mark_as_deleted(sbn::kernel_sack& result) {
    sbn::kernel::mark_as_deleted(result);
    for (auto& pair : this->_discoverers) {
        pair.second->mark_as_deleted(result);
    }
}

void sbnd::network_master::send_timer(bool first_time) {
    using namespace std::chrono;
    auto k = sbn::make_pointer<network_timer>();
    k->after(first_time ? duration::zero() : this->_interval);
    k->point_to_point(this);
    factory.local().send(std::move(k));
}

void sbnd::network_master::act() {
    this->send_timer(true);
}

auto
sbnd::network_master::enumerate_ifaddrs() -> interface_address_set {
    interface_address_set new_ifaddrs;
    sys::interface_addresses addrs;
    for (const sys::interface_addresses::value_type& ifa : addrs) {
        if (ifa.ifa_addr && ifa.ifa_addr->sa_family == traits_type::family) {
            ifaddr_type a(*ifa.ifa_addr, *ifa.ifa_netmask);
            if (!a.is_loopback() && !a.is_widearea()) {
                new_ifaddrs.emplace(a);
            }
        }
    }
    return new_ifaddrs;
}

void
sbnd::network_master::update_ifaddrs() {
    interface_address_set new_ifaddrs = this->enumerate_ifaddrs();
    interface_address_set ifaddrs_to_add =
        set_difference_copy(
            new_ifaddrs,
            this->_discoverers
        );
    interface_address_set ifaddrs_to_rm =
        set_difference_copy(
            this->_discoverers,
            new_ifaddrs
        );
    // filter allowed interface addresses
    if (!this->_allowedifaddrs.empty()) {
        auto first = ifaddrs_to_add.begin();
        while (first != ifaddrs_to_add.end()) {
            if (!this->is_allowed(*first)) {
                first = ifaddrs_to_add.erase(first);
            } else {
                ++first;
            }
        }
    }
    // update servers in socket pipeline
    for (const ifaddr_type& interface_address : ifaddrs_to_rm) {
        this->remove_ifaddr(interface_address);
    }
    for (const ifaddr_type& interface_address : ifaddrs_to_add) {
        this->add_ifaddr(interface_address);
    }
    this->send_timer();
}

void sbnd::network_master::react(sbn::kernel_ptr&& child) {
    if (typeid(*child) == typeid(network_timer)) {
        update_ifaddrs();
    } else if (typeid(*child) == typeid(probe)) {
        forward_probe(sbn::pointer_dynamic_cast<probe>(std::move(child)));
    } else if (typeid(*child) == typeid(Hierarchy_kernel)) {
        forward_hierarchy_kernel(sbn::pointer_dynamic_cast<Hierarchy_kernel>(std::move(child)));
    } else if (typeid(*child) == typeid(socket_pipeline_kernel)) {
        on_event(sbn::pointer_dynamic_cast<socket_pipeline_kernel>(std::move(child)));
    } else if (typeid(*child) == typeid(Status_kernel)) {
        report_status(sbn::pointer_dynamic_cast<Status_kernel>(std::move(child)));
    } else if (typeid(*child) == typeid(Job_status_kernel)) {
        report_job_status(sbn::pointer_dynamic_cast<Job_status_kernel>(std::move(child)));
    } else if (typeid(*child) == typeid(Pipeline_status_kernel)) {
        report_pipeline_status(sbn::pointer_dynamic_cast<Pipeline_status_kernel>(std::move(child)));
    } else if (typeid(*child) == typeid(process_pipeline_kernel)) {
        on_event(sbn::pointer_dynamic_cast<process_pipeline_kernel>(std::move(child)));
    }
}

void sbnd::network_master::report_status(pointer<Status_kernel> status) {
    Status_kernel::hierarchy_array hierarchies;
    hierarchies.reserve(this->_discoverers.size());
    for (const auto& pair : this->_discoverers) {
        hierarchies.emplace_back(pair.second->hierarchy());
    }
    status->hierarchies(std::move(hierarchies));
    status->return_to_parent(sbn::exit_code::success);
    #if !defined(SUBORDINATION_PROFILE_NODE_DISCOVERY)
    factory.unix().send(std::move(status));
    #endif
}

void sbnd::network_master::report_job_status(pointer<Job_status_kernel> k) {
    Job_status_kernel::application_array jobs;
    {
        auto g = factory.process().guard();
        for (auto id : k->job_ids()) { factory.process().remove(id); }
        jobs.reserve(factory.process().jobs().size());
        for (const auto& pair : factory.process().jobs()) {
            jobs.emplace_back(pair.second->application());
        }
    }
    auto tk = sbn::make_pointer<Terminate_kernel>(k->job_ids());
    tk->phase(sbn::kernel::phases::broadcast);
    factory.remote().send(std::move(tk));
    k->jobs(std::move(jobs));
    k->return_to_parent(sbn::exit_code::success);
    #if !defined(SUBORDINATION_PROFILE_NODE_DISCOVERY)
    factory.unix().send(std::move(k));
    #endif
}

void sbnd::network_master::report_pipeline_status(pointer<Pipeline_status_kernel> k) {
    Pipeline_status_kernel::pipeline_array pipelines;
    const sbn::basic_socket_pipeline* pipelines_b[] =
        { &factory.remote(), &factory.process(), &factory.unix() };
    for (const auto* ppl_b : pipelines_b) {
        pipelines.emplace_back();
        auto& ppl = pipelines.back();
        auto g = ppl_b->guard();
        ppl.name = ppl_b->name();
        for (const auto& conn : ppl_b->connections()) {
            if (!conn) { continue; }
            ppl.connections.emplace_back();
            auto& c = ppl.connections.back();
            c.address = conn->socket_address();
            for (const auto& b : conn->upstream()) {
                c.kernels.emplace_back();
                auto& a = c.kernels.back();
                a.id = b->id();
                if (b->is_foreign()) {
                    a.type_id = b->type_id();
                } else {
                    auto g = factory.types().guard();
                    auto result = factory.types().find(typeid(*b));
                    if (result != factory.types().end()) {
                        a.type_id = result->id();
                    }
                }
                a.source_application_id = b->source_application_id();
                a.target_application_id = b->target_application_id();
                a.source = b->source();
                a.destination = b->destination();
            }
        }
    }
    k->pipelines(std::move(pipelines));
    k->return_to_parent(sbn::exit_code::success);
    #if !defined(SUBORDINATION_PROFILE_NODE_DISCOVERY)
    factory.unix().send(std::move(k));
    #endif
}

void sbnd::network_master::add_ifaddr(const ifaddr_type& ifa) {
    sys::log_message("net", "add interface address _", ifa);
    factory.remote().add_server(ifa);
    if (this->_discoverers.find(ifa) == this->_discoverers.end()) {
        const auto port = factory.remote().port();
        auto d = sbn::make_pointer<master_discoverer>(ifa, port, this->_fanout);
        d->interval(network_scan_interval());
        this->_discoverers.emplace(ifa, d.get());
        d->parent(this);
        factory.local().send(std::move(d));
    }
}

void
sbnd::network_master::remove_ifaddr(const ifaddr_type& ifa) {
    sys::log_message("net", "remove interface address _", ifa);
    factory.remote().remove_server(ifa);
    auto result = this->_discoverers.find(ifa);
    if (result != this->_discoverers.end()) {
        // the kernel is deleted automatically
        result->second->stop();
        this->_discoverers.erase(result);
    }
}

void
sbnd::network_master::forward_probe(pointer<probe> p) {
    map_iterator result = this->find_discoverer(p->interface_address().address());
    if (result == this->_discoverers.end()) {
        sys::log_message("net", "bad probe _", p->interface_address());
    } else {
        auto* discoverer = result->second;
        p->principal(discoverer);
        factory.local().send(std::move(p));
    }
}

void
sbnd::network_master::forward_hierarchy_kernel(pointer<Hierarchy_kernel> p) {
    map_iterator result = this->find_discoverer(p->interface_address().address());
    if (result == this->_discoverers.end()) {
        sys::log_message("net", "bad hierarchy kernel _", p->interface_address());
    } else {
        auto* discoverer = result->second;
        p->principal(discoverer);
        factory.local().send(std::move(p));
    }
}

auto
sbnd::network_master::find_discoverer(const addr_type& a) -> map_iterator {
    typedef typename discoverer_table::value_type value_type;
    return std::find_if(
        this->_discoverers.begin(),
        this->_discoverers.end(),
        [&a] (const value_type& pair) {
            return pair.first.contains(a);
        }
    );
}

void sbnd::network_master::on_event(pointer<socket_pipeline_kernel> ev) {
    if (ev->event() == socket_pipeline_event::remove_client ||
        ev->event() == socket_pipeline_event::add_client) {
        const addr_type& a = traits_type::address(ev->socket_address());
        map_iterator result = this->find_discoverer(a);
        if (result == this->_discoverers.end()) {
            sys::log_message("net", "bad event socket_address _", a);
        } else {
            auto* discoverer = result->second;
            ev->principal(discoverer);
            factory.local().send(std::move(ev));
        }
    }
}

void sbnd::network_master::on_event(pointer<process_pipeline_kernel> k) {
    if (k->event() == process_pipeline_event::child_process_terminated) {
        sys::log_message("net", "job _ terminated with status _",
                         k->application_id(), k->status());
        if (k->status().exited()) {
            auto tk = sbn::make_pointer<Terminate_kernel>(
                Terminate_kernel::application_id_array{k->application_id()});
            tk->phase(sbn::kernel::phases::broadcast);
            factory.remote().send(std::move(tk));
        }
    }
}
