#ifndef EXAMPLES_FACTORY_HIERARCHY_KERNEL_HH
#define EXAMPLES_FACTORY_HIERARCHY_KERNEL_HH

#include <unistdx/net/ifaddr>
#include <unistdx/net/ipv4_addr>

#include <factory/api.hh>

namespace asc {

	class hierarchy_kernel: public asc::Kernel {

	public:
		typedef sys::ipv4_addr addr_type;
		typedef sys::ifaddr<addr_type> ifaddr_type;

	private:
		ifaddr_type _ifaddr;
		uint32_t _weight = 0;

	public:

		hierarchy_kernel() = default;

		inline
		hierarchy_kernel(const ifaddr_type& ifaddr, uint32_t weight):
		_ifaddr(ifaddr),
		_weight(weight)
		{}

		inline const ifaddr_type&
		ifaddr() const noexcept {
			return this->_ifaddr;
		}

		inline void
		weight(uint32_t rhs) noexcept {
			this->_weight = rhs;
		}

		inline uint32_t
		weight() const noexcept {
			return this->_weight;
		}

		void
		write(sys::pstream& out) override;

		void
		read(sys::pstream& in) override;

	};

}

#endif // vim:filetype=cpp