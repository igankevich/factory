#include <algorithm>
#include <cassert>
#include <sstream>
#include <stdexcept>

#include <unistdx/base/make_object>
#include <unistdx/net/socket>

#include <subordination/core/basic_factory.hh>
#include <subordination/core/kernel_instance_registry.hh>
#include <subordination/daemon/socket_pipeline.hh>

namespace {

    template <class kernel, class Server>
    void
    set_kernel_id(kernel* k, Server& srv) {
        if (!k->has_id()) {
            k->id(srv.generate_id());
        }
    }

    template <class kernel, class Id>
    void
    set_kernel_id_2(kernel* k, Id& counter) {
        if (!k->has_id()) {
            k->id(++counter);
        }
    }


}

namespace sbnd {

    class local_server: public sbn::connection {

    public:
        using ip_address = sys::ipv4_address;
        using interface_address_type = sys::interface_address<ip_address>;
        using id_type = typename interface_address_type::rep_type;
        using counter_type = id_type;

    private:
        interface_address_type _ifaddr;
        id_type _pos0 = 0;
        id_type _pos1 = 0;
        counter_type _counter {0};
        sys::socket _socket;

    public:

        inline explicit
        local_server(const interface_address_type& ifaddr, sys::port_type port):
        _ifaddr(ifaddr),
        _socket(sys::socket_address(ifaddr.address(), port)) {
            socket_address(sys::socket_address(ifaddr.address(), port));
            this->init();
        }

        local_server() = delete;
        local_server(const local_server&) = delete;
        local_server& operator=(const local_server&) = delete;
        local_server& operator=(local_server&&) = default;

        inline const interface_address_type
        interface_address() const noexcept {
            return this->_ifaddr;
        }

        inline sys::fd_type fd() const noexcept { return this->_socket.fd(); }
        inline const sys::socket& socket() const noexcept { return this->_socket; }
        inline sys::socket& socket() noexcept { return this->_socket; }

        inline id_type generate_id() noexcept {
            id_type id;
            if (this->_counter == this->_pos1) {
                id = this->_pos0;
                this->_counter = id+1;
            } else {
                id = this->_counter++;
            }
            return id;
        }

        void handle(const sys::epoll_event& ev) override;

        void add(const connection_ptr& self) override {
            connection::parent()->emplace_handler(
                sys::epoll_event{socket().fd(), sys::event::in}, self);
        }

        void remove(const connection_ptr& self) override {
            connection::parent()->erase(socket().fd());
            parent()->remove_server(this->_ifaddr);
        }

        using connection::parent;
        inline socket_pipeline* parent() const noexcept {
            return reinterpret_cast<socket_pipeline*>(this->connection::parent());
        }

    private:

        inline void
        init() noexcept {
            determine_id_range(this->_ifaddr, this->_pos0, this->_pos1);
            this->_counter = this->_pos0;
        }

    };

    class remote_client: public sbn::connection {

    public:
        using weight_type = socket_pipeline::weight_type;

    private:
        sys::socket _socket;
        sys::socket_address _old_bind_address;
        weight_type _max_weight = 1;
        weight_type _weight = 0;

    public:

        inline explicit remote_client(sys::socket&& socket): _socket(std::move(socket)) {}
        virtual ~remote_client() { recover(); }

        remote_client() = default;
        remote_client& operator=(const remote_client&) = delete;
        remote_client& operator=(remote_client&&) = delete;
        remote_client(const remote_client&) = delete;
        remote_client(remote_client&& rhs) = delete;

        inline sbn::foreign_kernel_ptr forward(sbn::foreign_kernel_ptr k) {
            return do_forward(std::move(k));
        }

        void recover() {
            // Here failed kernels are written to buffer,
            // from which they must be recovered with recover_kernels().
            try {
                sys::epoll_event ev {socket().fd(), sys::event::in};
                this->handle(ev);
            } catch (const std::exception& err) {
                log("error during recovery: _", err.what());
            }
            // recover kernels from upstream and downstream buffer
            recover_kernels(true);
        }

        void handle(const sys::epoll_event& event) override {
            if (state() == sbn::connection_state::starting && !event.err()) {
                state(sbn::connection_state::started);
            }
            if (event.in()) {
                fill(socket());
                receive_kernels();
            }
        }

        inline void flush() override { connection::flush(this->_socket); }

        inline const sys::socket& socket() const noexcept { return this->_socket; }
        inline sys::socket& socket() noexcept { return this->_socket; }

        void add(const connection_ptr& self) override {
            connection::parent()->emplace_handler(
                sys::epoll_event{socket().fd(), sys::event::inout}, self);
        }

        void remove(const connection_ptr& self) override {
            connection::parent()->erase(socket().fd());
            parent()->remove_client(socket_address());
        }

        inline void deactivate(const connection_ptr& self) override {
            connection::deactivate(self);
            connection::parent()->erase(socket().fd());
            this->_old_bind_address = this->_socket.name();
            this->_socket = sys::socket();
            state(sbn::connection_state::inactive);
        }

        inline void activate(const connection_ptr& self) override {
            this->_socket.bind(this->_old_bind_address);
            this->_socket.connect(socket_address());
            add(self);
            state(sbn::connection_state::starting);
        }

        using connection::parent;
        inline socket_pipeline* parent() const noexcept {
            return reinterpret_cast<socket_pipeline*>(this->connection::parent());
        }

        void receive_foreign_kernel(sbn::foreign_kernel_ptr&& k) override {
            using p = sbn::kernel::phases;
            if (k->phase() == p::upstream) {
                // Route upstream kernels using routing algorithm, i.e.
                // redistribute them across all neighbours except
                // the one from which the kernel was received.
                { auto g = parent()->unguard(); parent()->forward(std::move(k)); }
            } else {
                connection::receive_foreign_kernel(std::move(k));
            }
        }

        /// The number of nodes "behind" this one in the hierarchy.
        inline weight_type max_weight() const noexcept { return this->_max_weight; }
        inline void max_weight(weight_type rhs) noexcept { this->_max_weight = rhs; }
        inline weight_type weight() const noexcept { return this->_weight; }
        inline void weight(const weight_type& rhs) noexcept { this->_weight = rhs; }
        inline bool full() const noexcept { return this->_max_weight <= this->_weight; }

    };

}

void sbnd::local_server::handle(const sys::epoll_event& ev) {
    sys::socket_address addr;
    sys::socket sock;
    while (this->_socket.accept(sock, addr)) {
        sys::socket_address vaddr = parent()->virtual_addr(addr);
        auto res = parent()->_clients.find(vaddr);
        if (res == parent()->_clients.end()) {
            #if defined(SBN_DEBUG)
            auto ptr =
            #endif
            parent()->do_add_client(std::move(sock), vaddr);
            #if defined(SBN_DEBUG)
            this->log("accept _", ptr->socket_address());
            #endif
        }
    }
}

void sbnd::socket_pipeline::remove_server(const interface_address& interface_address) {
    lock_type lock(this->_mutex);
    server_iterator result = this->find_server(interface_address);
    if (result != this->_servers.end()) {
        this->remove_server(result);
    }
}

void sbnd::socket_pipeline::remove_server(server_iterator result) {
    // copy interface_address
    interface_address interface_address = (*result)->interface_address();
    this->_servers.erase(result);
    fire_event_kernels(socket_pipeline_event::remove_server, interface_address);
}

void sbnd::socket_pipeline::remove_client(const sys::socket_address& vaddr) {
    this->log("remove client _", vaddr);
    client_iterator result = this->_clients.find(vaddr);
    if (result != this->_clients.end()) {
        this->remove_client(result);
    }
}

void sbnd::socket_pipeline::remove_client(client_iterator result) {
    // copy socket_address
    sys::socket_address socket_address = result->first;
    #if defined(SBN_DEBUG)
    const char* reason =
        result->second->state() == sbn::connection_state::starting
        ? "timed out" : "connection closed";
    this->log("remove client _ (_)", socket_address, reason);
    #endif
    result->second->state(sbn::connection_state::stopped);
    this->_clients.erase(result);
    fire_event_kernels(socket_pipeline_event::remove_client, socket_address);
}

auto sbnd::socket_pipeline::advance_neighbour_iterator(const sbn::kernel* k)
-> client_iterator {
    using value_type = client_table::value_type;
    if (this->_clients.empty()) {
        log("neighbour local");
        return this->_clients.end();
    }
    bool all_full = true, any_started = false;
    for (const auto& pair : this->_clients) {
        const auto& client = *pair.second;
        if (client.state() != sbn::connection_state::started) { continue; }
        any_started = true;
        if (!client.full()) { all_full = false; }
    }
    // if there are no started clients
    if (!any_started) {
        log("neighbour local");
        return this->_clients.end();
    }
    // reset weights when all nodes are full
    if (all_full) {
        for (auto& pair : this->_clients) { pair.second->weight(0); }
        // do not send kernels carrying the parent to localhost
        if (use_localhost() && !k->carries_parent()) {
            log("neighbour local");
            return this->_clients.end();
        }
    }
    auto result = std::find_if(
        this->_clients.begin(), this->_clients.end(),
        [k] (value_type& pair) {
            const auto& address = pair.first;
            auto& client = *pair.second;
            // do not send the kernel back
            if (address == k->source()) { return false; }
            // skip stopped clients
            if (client.state() != sbn::connection_state::started) { return false; }
            // do not choose the client that is already received max. no. of kernels
            if (client.full()) { return false; }
            // increase weight
            client.weight(client.weight()+1);
            return true;
        });
    if (result != this->_clients.end()) {
        const auto& client = result->second;
        log("neighbour _ weight _ max-weight _",
            client->socket_address(), client->weight(), client->max_weight());
    }
    return result;
}

void sbnd::socket_pipeline::add_server(const sys::socket_address& rhs, ip_address netmask) {
    using traits_type = sys::ipaddr_traits<ip_address>;
    lock_type lock(this->_mutex);
    interface_address interface_address(traits_type::address(rhs), netmask);
    if (this->find_server(interface_address) == this->_servers.end()) {
        auto ptr = std::make_shared<local_server>(interface_address, traits_type::port(rhs));
        ptr->parent(this);
        this->_servers.emplace_back(ptr);
        #if defined(SBN_DEBUG)
        this->log("add server _", rhs);
        #endif
        ptr->socket().set_user_timeout(this->_socket_timeout);
        ptr->add(ptr);
        fire_event_kernels(socket_pipeline_event::add_server, interface_address);
    }
}

void sbnd::socket_pipeline::forward(sbn::foreign_kernel_ptr&& k) {
    lock_type lock(this->_mutex);
    log("forward _", *k);
    client_iterator client;
    switch (k->phase()) {
        case sbn::kernel::phases::upstream:
            client = advance_neighbour_iterator(k.get());
            if (client == this->_clients.end()) {
                if (k->carries_parent()) {
                    log("warning, sending a kernel carrying parent to local pipeline _", *k);
                }
                forward_foreign(std::move(k));
            } else {
                //ensure_identity(k.get(), address);
                //#if defined(SBN_DEBUG)
                std::stringstream tmp;
                for (const auto& pair : this->_clients) { tmp << pair.first << ' '; }
                log("fwd _ to _ clients (_)", *k, client->first, tmp.str());
                //#endif
                client->second->forward(std::move(k));
                this->_semaphore.notify_one();
            }
            break;
        case sbn::kernel::phases::downstream:
        case sbn::kernel::phases::point_to_point:
            if (!k->destination()) {
                throw std::invalid_argument("kernel without destination");
            }
            #if defined(SBN_DEBUG)
            log("fwd _ to _", *k, k->destination());
            #endif
            {
                auto ptr = find_or_create_client(k->destination());
                ptr->forward(std::move(k));
            }
            this->_semaphore.notify_one();
            break;
        case sbn::kernel::phases::broadcast:
            for (auto& pair : this->_clients) {
                auto& conn = pair.second;
                if (k->source() == conn->socket_address()) { continue; }
                k = conn->forward(std::move(k));
            }
            this->_semaphore.notify_one();
            break;
    }
}

auto sbnd::socket_pipeline::find_server(const interface_address& interface_address)
-> server_iterator {
    typedef typename server_array::value_type value_type;
    return std::find_if(
        this->_servers.begin(),
        this->_servers.end(),
        [&interface_address] (const value_type& rhs) {
            return rhs->interface_address() == interface_address;
        }
    );
}

auto sbnd::socket_pipeline::find_server(sys::fd_type fd) -> server_iterator {
    typedef typename server_array::value_type value_type;
    return std::find_if(
        this->_servers.begin(),
        this->_servers.end(),
        [fd] (const value_type& rhs) {
            return rhs->socket().fd() == fd;
        }
    );
}

auto sbnd::socket_pipeline::find_server(const sys::socket_address& dest) -> server_iterator {
    typedef typename server_array::value_type value_type;
    return std::find_if(
        this->_servers.begin(),
        this->_servers.end(),
        [&dest] (const value_type& rhs) {
            return rhs->interface_address().contains(dest.addr4());
        }
    );
}

void sbnd::socket_pipeline::ensure_identity(sbn::kernel* k, const sys::socket_address& dest) {
    if (dest.family() == sys::family_type::unix) {
        set_kernel_id_2(k, this->_counter);
        if (auto* p = k->parent()) { set_kernel_id_2(p, this->_counter); }
    } else {
        if (this->_servers.empty()) {
            k->return_to_parent(sbn::exit_code::no_upstream_servers_available);
        } else {
            server_iterator result = this->find_server(dest);
            if (result == this->_servers.end()) {
                result = this->_servers.begin();
            }
            set_kernel_id(k, **result);
            if (auto* p = k->parent()) { set_kernel_id(p, **result); }
        }
    }
}

void
sbnd::socket_pipeline::emplace_client(const sys::socket_address& vaddr, const client_ptr& s) {
    this->_clients.emplace(vaddr, s);
}

void sbnd::socket_pipeline::process_kernels() {
    while (!this->_kernels.empty()) {
        auto k = std::move(this->_kernels.front());
        this->_kernels.pop();
        try {
            this->process_kernel(k);
        } catch (const std::exception& err) {
            this->log_error(err);
            if (k) {
                k->source(k->destination());
                k->return_to_parent(sbn::exit_code::no_upstream_servers_available);
                send_native(std::move(k));
            }
        }
    }
}

void sbnd::socket_pipeline::process_kernel(sbn::kernel_ptr& k) {
    // short circuit local server
    /*
       if (k->destination()) {
        server_iterator result = this->find_server(k->destination());
        if (result != this->_servers.end()) {
            k->source(k->destination());
            k->return_to_parent(exit_code::no_upstream_servers_available);
            send_native(std::move(k));
            return;
        }
       }
     */
    if (k->phase() == sbn::kernel::phases::broadcast) {
        for (auto& pair : _clients) {
            auto& conn = pair.second;
            if (k->source() == conn->socket_address()) { continue; }
            conn->send(k);
        }
    } else if (k->phase() == sbn::kernel::phases::upstream &&
               k->destination() == sys::socket_address()) {
        auto client = advance_neighbour_iterator(k.get());
        if (client == this->_clients.end()) {
            if (k->carries_parent()) {
                log("warning, sending a kernel carrying parent to local pipeline _", *k);
            }
            send_native(std::move(k));
        } else {
            ensure_identity(k.get(), client->second->socket_address());
            client->second->send(k);
        }
    } else if (k->phase() == sbn::kernel::phases::downstream and not k->source()) {
        // kernel @k was sent to local node
        // because no upstream servers had
        // been available
        send_native(std::move(k));
    } else {
        // create socket_address if necessary, and send kernel
        if (not k->destination()) {
            k->destination(k->source());
        }
        if (k->phase() == sbn::kernel::phases::point_to_point) {
            ensure_identity(k.get(), k->destination());
        }
        this->find_or_create_client(k->destination())->send(k);
    }
}

auto
sbnd::socket_pipeline::find_or_create_client(const sys::socket_address& addr) -> client_ptr {
    client_ptr ret;
    auto result = this->_clients.find(addr);
    if (result == this->_clients.end()) {
        ret = do_add_client(addr);
    } else {
        ret = result->second;
    }
    return ret;
}

auto
sbnd::socket_pipeline::do_add_client(const sys::socket_address& addr) -> client_ptr {
    if (addr.family() == sys::family_type::unix) {
        sys::socket s(sys::family_type::unix);
        s.setopt(sys::socket::pass_credentials);
        try {
            s.connect(addr);
        } catch (const sys::bad_call& err) {
            if (err.errc() != std::errc::connection_refused) {
                throw;
            }
        }
        return this->do_add_client(std::move(s), addr);
    } else {
        server_iterator result = this->find_server(addr);
        if (result == this->_servers.end()) {
            throw std::invalid_argument("no matching server found");
        }
        // bind to server address with ephemeral port
        sys::socket_address srv_addr((*result)->socket_address(), 0);
        sys::socket s(addr.family());
        s.bind(srv_addr);
        try {
            s.connect(addr);
        } catch (const sys::bad_call& err) {
            if (err.errc() != std::errc::connection_refused) {
                throw;
            }
        }
        return this->do_add_client(std::move(s), addr);
    }
}

auto
sbnd::socket_pipeline::do_add_client(sys::socket&& sock, sys::socket_address vaddr) -> client_ptr {
    if (vaddr.family() != sys::family_type::unix) {
        sock.set_user_timeout(this->_socket_timeout);
    }
    auto s = std::make_shared<remote_client>(std::move(sock));
    s->socket_address(vaddr);
    s->parent(this);
    s->types(types());
    s->state(sbn::connection_state::starting);
    s->name(this->_name);
    using f = sbn::connection_flags;
    s->setf(f::save_upstream_kernels | f::save_downstream_kernels | f::write_transaction_log);
    emplace_client(vaddr, s);
    s->add(s);
    fire_event_kernels(socket_pipeline_event::add_client, vaddr);
    return s;
}

void
sbnd::socket_pipeline::stop_client(const sys::socket_address& addr) {
    lock_type lock(this->_mutex);
    auto result = this->_clients.find(addr);
    if (result != this->_clients.end()) {
        result->second->state(sbn::connection_state::stopped);
    }
}

void
sbnd::socket_pipeline::set_client_weight(const sys::socket_address& addr,
                                         weight_type new_weight) {
    lock_type lock(this->_mutex);
    auto result = this->_clients.find(addr);
    if (result != this->_clients.end()) { result->second->max_weight(new_weight); }
}