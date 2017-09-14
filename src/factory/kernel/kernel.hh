#ifndef FACTORY_KERNEL_KERNEL_HH
#define FACTORY_KERNEL_KERNEL_HH

#include <unistdx/net/pstream>
#include <memory>

#include "mobile_kernel.hh"

namespace factory {

	struct Kernel: public mobile_kernel {

		typedef mobile_kernel base_kernel;
		using mobile_kernel::id_type;

		inline const Kernel*
		principal() const {
			return this->isset(kernel_flag::principal_is_id)
				? nullptr : this->_principal;
		}

		inline Kernel*
		principal() {
			return this->_principal;
		}

		inline id_type
		principal_id() const {
			return this->_principal_id;
		}

		inline void
		set_principal_id(id_type id) {
			this->_principal_id = id;
			this->setf(kernel_flag::principal_is_id);
		}

		inline void
		principal(Kernel* rhs) {
			this->_principal = rhs;
			this->unsetf(kernel_flag::principal_is_id);
		}

		inline const Kernel*
		parent() const {
			return this->_parent;
		}

		inline Kernel*
		parent() {
			return this->_parent;
		}

		inline id_type
		parent_id() const {
			return this->_parent_id;
		}

		inline void
		parent(Kernel* p) {
			this->_parent = p;
			this->unsetf(kernel_flag::parent_is_id);
		}

		inline size_t
		hash() const {
			return this->_principal && this->_principal->has_id()
				? this->_principal->id()
				: size_t(this->_principal) / alignof(size_t);
		}

		inline bool
		moves_upstream() const noexcept {
			return this->result() == exit_code::undefined &&
				!this->_principal &&
				this->_parent;
		}

		inline bool
		moves_downstream() const noexcept {
			return this->result() != exit_code::undefined &&
				this->_principal &&
				this->_parent;
		}

		inline bool
		moves_somewhere() const noexcept {
			return this->result() == exit_code::undefined &&
				this->_principal &&
				this->_parent;
		}

		inline bool
		moves_everywhere() const noexcept {
			return !this->_principal && !this->_parent;
		}

		void
		read(sys::pstream& in) override;

		void
		write(sys::pstream& out) override;

		virtual void
		act();

		virtual void
		react(Kernel* child);

		virtual void
		error(Kernel* rhs);

		friend std::ostream&
		operator<<(std::ostream& out, const Kernel& rhs);

	public:

		/// New API

		inline Kernel*
		call(Kernel* rhs) noexcept {
			rhs->parent(this);
			return rhs;
		}

		inline Kernel*
		carry_parent(Kernel* rhs) noexcept {
			rhs->parent(this);
			rhs->setf(kernel_flag::carries_parent);
			return rhs;
		}

		inline void
		return_to_parent(exit_code ret = exit_code::success) noexcept {
			return_to(_parent, ret);
		}

		inline void
		return_to(Kernel* rhs, exit_code ret = exit_code::success) noexcept {
			this->principal(rhs);
			this->result(ret);
		}

		inline void
		recurse() noexcept {
			this->principal(this);
		}

		template<class It>
		void
		mark_as_deleted(It result) noexcept {
			if (!this->isset(kernel_flag::DELETED)) {
				this->setf(kernel_flag::DELETED);
				if (this->_parent) {
					this->_parent->mark_as_deleted(result);
				}
				*result = std::unique_ptr<Kernel>(this);
				++result;
			}
		}

	private:

		union {
			Kernel* _parent = nullptr;
			id_type _parent_id;
		};
		union {
			Kernel* _principal = nullptr;
			id_type _principal_id;
		};

	};

	std::ostream&
	operator<<(std::ostream& out, const Kernel& rhs);

}

#ifdef SPRINGY
#include <springy/springy.hh>
#include <test/demangle.hh>
namespace std {

	template<>
	struct hash<factory::Kernel> {

		typedef size_t result_type;
		typedef factory::Kernel argument_type;

		size_t
		operator()(const factory::Kernel& rhs) const noexcept {
			return rhs.has_id() ? rhs.id() : size_t(std::addressof(rhs));
		}

	};

}
namespace springy {

	template<>
	struct Node<factory::Kernel> {

		explicit
		Node(const factory::Kernel& rhs) noexcept:
		_kernel(rhs)
		{}

		std::string
		label() const noexcept {
			return test::short_name(test::demangle_name(typeid(_kernel).name()));
		}

		float
		mass() const noexcept {
			return _kernel._mass;
		}

		friend std::ostream&
		operator<<(std::ostream& out, const Node& rhs) {
			std::hash<factory::Kernel> hsh;
			return out << 'n' << hsh(rhs._kernel);
		}

	private:

		const factory::Kernel& _kernel;

	};

}
#endif

#endif // vim:filetype=cpp
