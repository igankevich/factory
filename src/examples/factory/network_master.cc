#include "network_master.hh"

#include <algorithm>
#include <iterator>

#include <unistdx/base/log_message>
#include <unistdx/it/field_iterator>
#include <unistdx/it/intersperse_iterator>
#include <unistdx/net/ifaddrs>

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

void
asc::network_master::send_timer() {
	using namespace std::chrono;
	this->_timer = new network_timer;
	this->_timer->after(seconds(1));
	asc::send<>(this->_timer, this);
}

void
asc::network_master::act() {
	this->send_timer();
}

asc::network_master::set_type
asc::network_master::enumerate_ifaddrs() {
	set_type new_ifaddrs;
	sys::ifaddrs addrs;
	for (const sys::ifaddrs::value_type& ifa : addrs) {
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
asc::network_master::update_ifaddrs() {
	set_type new_ifaddrs = this->enumerate_ifaddrs();
	set_type ifaddrs_to_add = set_difference_copy(
		new_ifaddrs,
		this->_ifaddrs
	                          );
	set_type ifaddrs_to_rm = set_difference_copy(
		this->_ifaddrs,
		new_ifaddrs
	                         );
	// update servers in socket pipeline
	for (const ifaddr_type& ifaddr : ifaddrs_to_rm) {
		this->remove_ifaddr(ifaddr);
	}
	for (const ifaddr_type& ifaddr : ifaddrs_to_add) {
		this->add_ifaddr(ifaddr);
	}
	this->send_timer();
}

void
asc::network_master::react(asc::Kernel* child) {
	if (child == this->_timer) {
		this->update_ifaddrs();
		this->_timer = nullptr;
	} else if (typeid(*child) == typeid(probe)) {
		this->forward_probe(dynamic_cast<probe*>(child));
	} else if (typeid(*child) == typeid(hierarchy_kernel)) {
		this->forward_hierarchy_kernel(dynamic_cast<hierarchy_kernel*>(child));
	} else if (typeid(*child) == typeid(socket_pipeline_kernel)) {
		this->on_event(dynamic_cast<socket_pipeline_kernel*>(child));
	}
}

void
asc::network_master::add_ifaddr(const ifaddr_type& ifa) {
	sys::log_message("net", "add ifaddr _", ifa);
	asc::factory.nic().add_server(ifa);
	if (this->_ifaddrs.find(ifa) == this->_ifaddrs.end()) {
		const sys::port_type port = ::asc::factory.nic().port();
		master_discoverer* d = new master_discoverer(ifa, port, this->_fanout);
		this->_ifaddrs.emplace(ifa, d);
		asc::upstream(this, d);
	}
}

void
asc::network_master::remove_ifaddr(const ifaddr_type& ifa) {
	sys::log_message("net", "remove ifaddr _", ifa);
	asc::factory.nic().remove_server(ifa);
	auto result = this->_ifaddrs.find(ifa);
	if (result != this->_ifaddrs.end()) {
		// the kernel is deleted automatically
		result->second->stop();
		this->_ifaddrs.erase(result);
	}
}

void
asc::network_master::forward_probe(probe* p) {
	map_iterator result = this->find_discoverer(p->ifaddr().address());
	if (result == this->_ifaddrs.end()) {
		sys::log_message("net", "bad probe _", p->ifaddr());
	} else {
		p->setf(kernel_flag::do_not_delete);
		asc::send(p, result->second);
	}
}

void
asc::network_master::forward_hierarchy_kernel(hierarchy_kernel* p) {
	map_iterator result = this->find_discoverer(p->ifaddr().address());
	if (result == this->_ifaddrs.end()) {
		sys::log_message("net", "bad hierarchy kernel _", p->ifaddr());
	} else {
		p->setf(kernel_flag::do_not_delete);
		asc::send(p, result->second);
	}
}

asc::network_master::map_iterator
asc::network_master::find_discoverer(const addr_type& a) {
	typedef typename map_type::value_type value_type;
	return std::find_if(
		this->_ifaddrs.begin(),
		this->_ifaddrs.end(),
		[&a] (const value_type& pair) {
		    return pair.first.contains(a);
		}
	);
}

void
asc::network_master::on_event(socket_pipeline_kernel* ev) {
	if (ev->event() == socket_pipeline_event::remove_client ||
	    ev->event() == socket_pipeline_event::add_client) {
		const addr_type& a = traits_type::address(ev->endpoint());
		map_iterator result = this->find_discoverer(a);
		if (result == this->_ifaddrs.end()) {
			sys::log_message("net", "bad event endpoint _", a);
		} else {
			ev->setf(kernel_flag::do_not_delete);
			asc::send(ev, result->second);
		}
	}
}