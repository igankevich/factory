#ifndef SUBORDINATION_DAEMON_HIERARCHY_HH
#define SUBORDINATION_DAEMON_HIERARCHY_HH

#include <iosfwd>
#include <unordered_set>

#include <unistdx/net/interface_address>
#include <unistdx/net/socket_address>

#include <subordination/core/types.hh>
#include <subordination/daemon/hierarchy_node.hh>

namespace sbnd {

    template<class Addr>
    class Hierarchy {

    public:
        typedef Addr addr_type;
        typedef sys::interface_address<addr_type> ifaddr_type;
        typedef std::unordered_set<hierarchy_node> container_type;
        typedef typename container_type::const_iterator const_iterator;
        typedef typename container_type::size_type size_type;
        typedef hierarchy_node node_type;
        typedef node_type::weight_type weight_type;

    protected:
        ifaddr_type _ifaddr;
        sys::socket_address _socket_address;
        hierarchy_node _superior;
        container_type _subordinates;

    public:

        inline explicit
        Hierarchy(const ifaddr_type& interface_address, const sys::port_type port):
        _ifaddr(interface_address),
        _socket_address(sys::ipv4_socket_address{interface_address.address(), port}),
        _superior(),
        _subordinates()
        {}

        Hierarchy() = default;
        ~Hierarchy() = default;
        Hierarchy(const Hierarchy&) = default;
        Hierarchy& operator=(const Hierarchy&) = default;
        Hierarchy(Hierarchy&&) = default;
        Hierarchy& operator=(Hierarchy&&) = default;

        inline const ifaddr_type&
        interface_address() const noexcept {
            return this->_ifaddr;
        }

        inline const sys::socket_address&
        socket_address() const noexcept {
            return this->_socket_address;
        }

        inline const node_type&
        superior() const noexcept {
            return this->_superior;
        }

        inline bool
        remove_superior() noexcept {
            bool old_state = bool(this->_superior.socket_address());
            this->_superior.reset();
            return old_state != bool(this->_superior.socket_address());
        }

        inline bool
        add_superior(const hierarchy_node& node) {
            if (node.socket_address() == this->_superior.socket_address() &&
                node.weight() == this->_superior.weight()) { return false; }
            this->_superior = node;
            this->_subordinates.erase(this->_superior);
            return true;
        }

        inline void
        superior(const sys::socket_address& new_princ) {
            this->_superior.socket_address(new_princ);
            this->_subordinates.erase(this->_superior);
        }

        inline bool
        add_subordinate(const sys::socket_address& addr) {
            auto result = this->_subordinates.insert(hierarchy_node(addr));
            return result.second;
        }

        inline bool
        remove_subordinate(const sys::socket_address& addr) {
            return this->_subordinates.erase(node_type(addr)) > 0;
        }

        inline size_type
        num_subordinates() const noexcept {
            return this->_subordinates.size();
        }

        inline bool
        has_superior() const noexcept {
            return static_cast<bool>(this->_superior.socket_address());
        }

        inline bool
        has_superior(const sys::socket_address& rhs) const noexcept {
            return this->_superior.socket_address() == rhs;
        }

        inline bool
        has_subordinate(const sys::socket_address& rhs) const noexcept {
            return this->_subordinates.find(node_type(rhs)) !=
                   this->_subordinates.end();
        }

        inline size_type
        num_neighbours() const noexcept {
            return this->num_subordinates() + (this->has_superior() ? 1 : 0);
        }

        inline sys::port_type
        port() const noexcept {
            return sys::ipaddr_traits<addr_type>::port(this->_socket_address);
        }

        inline const_iterator
        begin() const noexcept {
            return this->_subordinates.begin();
        }

        inline const_iterator
        end() const noexcept {
            return this->_subordinates.end();
        }

        inline const container_type& subordinates() const noexcept {
            return this->_subordinates;
        }

        inline bool
        set_superior_weight(weight_type w) noexcept {
            weight_type old = this->_superior.weight();
            bool changed = old != w;
            if (changed) {
                this->_superior.weight(w);
            }
            return changed;
        }

        /// @return total weight of all subordinate and principal nodes
        weight_type
        total_weight() const noexcept;

        /// @return total weight of all subordinate nodes
        weight_type
        total_subordinate_weight() const noexcept;

        inline weight_type
        principal_weight() const noexcept {
            return this->has_superior() ? this->_superior.weight() : 0;
        }

        bool
        set_subordinate_weight(const sys::socket_address& endp, weight_type w);

        template <class X>
        friend std::ostream&
        operator<<(std::ostream& out, const Hierarchy<X>& rhs);

        void write(sbn::kernel_buffer& out) const;
        void read(sbn::kernel_buffer& in);

    };

    template <class X>
    std::ostream&
    operator<<(std::ostream& out, const Hierarchy<X>& rhs);

    template <class T> inline sbn::kernel_buffer&
    operator<<(sbn::kernel_buffer& out, const Hierarchy<T>& rhs) {
        rhs.write(out); return out;
    }

    template <class T> inline sbn::kernel_buffer&
    operator>>(sbn::kernel_buffer& in, Hierarchy<T>& rhs) {
        rhs.read(in); return in;
    }

}


#endif // vim:filetype=cpp
