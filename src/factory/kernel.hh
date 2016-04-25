#ifndef FACTORY_KERNEL_HH
#define FACTORY_KERNEL_HH

#include <bitset>

#include <stdx/log.hh>

#include <factory/server/basic_server.hh>
#include <sys/proc/process.hh>
#include <sys/packetstream.hh>
#include "result.hh"

namespace factory {

	namespace components {

		struct kernel_category {};

		struct Basic_kernel {

			typedef std::chrono::steady_clock Clock;
			typedef Clock::time_point Time_point;
			typedef Clock::duration Duration;
			typedef std::bitset<3> Flags;
			typedef stdx::log<Basic_kernel> this_log;

			enum struct Flag {
				DELETED = 0,
				carries_parent = 1,
				priority_service = 2
			};

			virtual
			~Basic_kernel() = default;

			Result
			result() const noexcept {
				return _result;
			}

			void
			result(Result rhs) noexcept {
				_result = rhs;
			}

			// timed kernels
			Time_point
			at() const noexcept {
				return _at;
			}

			void
			at(Time_point t) noexcept {
				_at = t;
			}

			void
			after(Duration delay) noexcept {
				_at = Clock::now() + delay;
			}

			bool
			timed() const noexcept {
				return _at != Time_point(Duration::zero());
			}

			// flags
			Flags
			flags() const noexcept {
				return _flags;
			}

			void
			setf(Flag f) noexcept {
				_flags.set(static_cast<size_t>(f));
			}

			void
			unsetf(Flag f) noexcept {
				_flags.reset(static_cast<size_t>(f));
			}

			bool
			isset(Flag f) const noexcept {
				return _flags.test(static_cast<size_t>(f));
			}

			bool
			carries_parent() const noexcept {
				return this->isset(Flag::carries_parent);
			}

		protected:

			Result _result = Result::undefined;
			Time_point _at = Time_point(Duration::zero());
			Flags _flags = 0;

		};

		struct Kernel_header {

			typedef uint16_t app_type;

			Kernel_header() = default;
			Kernel_header(const Kernel_header&) = default;
			Kernel_header& operator=(const Kernel_header&) = default;
			~Kernel_header() = default;

			sys::endpoint
			from() const noexcept {
				return _src;
			}

			void
			from(sys::endpoint rhs) noexcept {
				_src = rhs;
			}

			sys::endpoint
			to() const noexcept {
				return _dst;
			}

			void
			to(sys::endpoint rhs) noexcept {
				_dst = rhs;
			}

			app_type
			app() const noexcept {
				return this->_app;
			}

			void
			setapp(app_type rhs) noexcept {
				this->_app = rhs;
			}

			bool
			is_foreign() const noexcept {
				return static_cast<bool>(_src);
			}

			friend std::ostream&
			operator<<(std::ostream& out, const Kernel_header& rhs) {
				return out << stdx::make_fields(
					"src", rhs._src,
					"dst", rhs._dst,
					"app", rhs._app
				);
			}

		private:
			sys::endpoint _src{};
			sys::endpoint _dst{};
			app_type _app = 0;
		};

		struct Mobile_kernel: public Basic_kernel, public Kernel_header {

			typedef uint64_t id_type;
			static constexpr const id_type no_id = 0;

			virtual void
			read(sys::packetstream& in) {
				in >> _result >> _id;
			}

			virtual void
			write(sys::packetstream& out) {
				out << _result << _id;
			}

			id_type
			id() const noexcept {
				return _id;
			}

			void
			id(id_type rhs) noexcept {
				_id = rhs;
			}

			bool
			identifiable() const noexcept {
				return _id != no_id;
			}

			void
			set_id(id_type rhs) noexcept {
				_id = rhs;
			}

			bool
			operator==(const Mobile_kernel& rhs) const noexcept {
				return this == &rhs or (
					id() == rhs.id()
					and identifiable()
					and rhs.identifiable()
				);
			}

		private:

			id_type _id = no_id;

		};

		template<class Config>
		struct Principal: public Mobile_kernel {

			typedef Mobile_kernel base_kernel;
			typedef Basic_kernel::Flag Flag;
			typedef stdx::log<Principal> this_log;
			typedef typename Config::server server_type;
			typedef typename Config::factory factory_type;
			using Mobile_kernel::id_type;

			const Principal*
			principal() const {
				return _principal;
			}

			Principal*
			principal() {
				return _principal;
			}

			id_type
			principal_id() const {
				return _principal_id;
			}

			void
			set_principal_id(id_type id) {
				_principal_id = id;
			}

			void
			principal(Principal* rhs) {
				_principal = rhs;
			}

			const Principal*
			parent() const {
				return _parent;
			}

			Principal*
			parent() {
				return _parent;
			}

			id_type
			parent_id() const {
				return _parent_id;
			}

			void
			parent(Principal* p) {
				_parent = p;
			}

			size_t
			hash() const {
				return _principal && _principal->identifiable()
					? _principal->id()
					: size_t(_principal) / alignof(size_t);
			}

			bool
			moves_upstream() const noexcept {
				return this->result() == Result::undefined && !_principal && _parent;
			}

			bool
			moves_downstream() const noexcept {
				return this->result() != Result::undefined && _principal && _parent;
			}

			bool
			moves_somewhere() const noexcept {
				return this->result() == Result::undefined && _principal && _parent;
			}

			bool
			moves_everywhere() const noexcept {
				return !_principal && !_parent;
			}

			void
			read(sys::packetstream& in) override {
				base_kernel::read(in);
				bool b = false;
				in >> b;
				if (b) {
					this->setf(Flag::carries_parent);
				}
				assert(not _parent and "Parent is not null while reading from the data stream.");
				in >> _parent_id;
				assert(not _principal and "Principal kernel is not null while reading from the data stream.");
				in >> _principal_id;
			}

			void
			write(sys::packetstream& out) override {
				base_kernel::write(out);
				out << carries_parent();
				if (moves_downstream() or moves_somewhere()) {
					out << _parent_id << _principal_id;
				} else {
					out << (not _parent ? Mobile_kernel::no_id : _parent->id());
					out << (not _principal ? Mobile_kernel::no_id : _principal->id());
				}
			}

			virtual void
			act(server_type& this_server) {
				act();
			}

			virtual void
			act() {}

			virtual void
			react(server_type&, Principal* child) {
				react(child);
			}

			virtual void
			react(Principal*) {
				std::stringstream msg;
				msg << "Empty react in ";
				const Type<Principal> tp = type();
				if (tp) {
					msg << tp;
				} else {
					msg << "unknown type";
					msg << " typeid=" << typeid(*this).name();
				}
				throw Error(msg.str(), __FILE__, __LINE__, __func__);
			}

			virtual void
			error(server_type& this_server, Principal* rhs) {
				react(this_server, rhs);
			}

			virtual const Type<Principal>
			type() const noexcept {
				return Type<Principal>{0};
			}

			void
			run_act(server_type& this_server) {
				_server = &this_server;
				if (_principal) {
					_principal->_server = &this_server;
				}
				switch (this->result()) {
					case Result::undefined:
						if (_principal) {
							_principal->react(this_server, this);
						} else {
							this->act(this_server);
//							if (this->moves_everywhere()) {
//								delete this;
//							}
						}
						break;
					case Result::success:
					case Result::endpoint_not_connected:
					case Result::no_principal_found:
					default:
						this_log() << "Result is defined" << std::endl;
						if (!_principal) {
							this_log() << "Principal is null" << std::endl;
							if (!_parent) {
								delete this;
								this_log() << "SHUTDOWN" << std::endl;
								this_server.factory()->shutdown();
							}
						} else {
							this_log() << "Principal is not null" << std::endl;
							bool del = *_principal == *_parent;
							if (this->result() == Result::success) {
								_principal->react(this_server, this);
								this_log() << "Principal end react" << std::endl;
							} else {
								this_log() << "Principal error" << std::endl;
								_principal->error(this_server, this);
							}
							if (del) {
								this_log() << "delete " << *this << std::endl;
								delete this;
							}
						}
				}
			}

			friend std::ostream&
			operator<<(std::ostream& out, const Principal& rhs) {
				return out << '{'
					<< (rhs.moves_upstream()   ? 'u' : '-')
					<< (rhs.moves_downstream() ? 'd' : '-')
					<< (rhs.moves_somewhere()  ? 's' : '-')
					<< (rhs.moves_everywhere() ? 'b' : '-')
					<< ",tp=" << rhs.type()
					<< ",id=" << rhs.id()
					<< ",src=" << rhs.from()
					<< ",dst=" << rhs.to()
					<< ",rslt=" << rhs.result()
					<< ",app=" << rhs.app()
					<< ",parent=" << rhs._parent
					<< ",principal=" << rhs._principal
					<< '}';
			}

		public:

			factory_type*
			factory() noexcept {
				assert(_server != nullptr);
				return _server->factory();
			}

			/// @deprecated
			server_type*
			local_server() noexcept {
				assert(_server != nullptr);
				return _server->local_server();
			}

			/// @deprecated
			server_type*
			io_server() noexcept {
				assert(_server != nullptr);
				return _server->io_server();
			}

			/// @deprecated
			server_type*
			remote_server() noexcept {
				assert(_server != nullptr);
				return _server->remote_server();
			}

			/// @deprecated
			template<class S>
			void
			upstream(S* this_server, Principal* a) {
				a->parent(this);
				this_server->send(a);
			}

			/// @deprecated
			template<class S>
			void
			upstream_carry(S* this_server, Principal* a) {
				a->parent(this);
				a->setf(Flag::carries_parent);
				this_server->send(a);
			}

			/// @deprecated
			template<class S>
			void
			commit(S* srv, Result res = Result::success) {
				this->principal(_parent);
				this->result(res);
				srv->send(this);
			}

			/// New API

			inline Principal*
			call(Principal* rhs) noexcept {
				rhs->parent(this);
				return rhs;
			}

			inline Principal*
			carry_parent(Principal* rhs) noexcept {
				rhs->parent(this);
				rhs->setf(Flag::carries_parent);
				return rhs;
			}

			inline void
			return_to_parent(Result ret = Result::success) noexcept {
				return_to(_parent, ret);
			}

			inline void
			return_to(Principal* rhs, Result ret = Result::success) noexcept {
				this->principal(rhs);
				this->result(ret);
			}

			inline void
			recurse() noexcept {
				this->principal(this);
			}

			/// Server API

			inline void compute(Principal* rhs) { factory()->compute(rhs); }
			inline void spill(Principal* rhs) { factory()->spill(rhs); }
			inline void input(Principal* rhs) { factory()->input(rhs); }
			inline void output(Principal* rhs) { factory()->output(rhs); }
			inline void schedule(Principal* rhs) { factory()->schedule(rhs); }
			inline void yield() { compute(this); }
			inline void collect() { spill(this); }

			template<class It>
			void
			mark_as_deleted(It result) noexcept {
				if (!this->isset(Flag::DELETED)) {
					this_log() << "marked for death " << *this << std::endl;
					this->setf(Flag::DELETED);
					if (this->_parent) {
						this->_parent->mark_as_deleted(result);
					}
					*result = std::unique_ptr<Principal>(this);
					++result;
				}
			}

		private:

			union {
				Principal* _parent = nullptr;
				id_type _parent_id;
			};
			union {
				Principal* _principal = nullptr;
				id_type _principal_id;
			};
			server_type* _server = nullptr;

		};

		struct Application {

			typedef std::string path_type;

			static const Kernel_header::app_type ROOT = 0;

			Application() = default;

			explicit
			Application(const path_type& exec):
				_execpath(exec) {}

			int
			execute() const {
				return sys::this_process::execute(this->_execpath);
			}

			friend std::ostream&
			operator<<(std::ostream& out, const Application& rhs) {
				return stdx::format_fields(out, "exec", rhs._execpath);
			}

		private:

			path_type _execpath;

		};

	}

	template<class Base>
	struct Priority_kernel: public Base {
		Priority_kernel() {
			this->setf(components::Basic_kernel::Flag::priority_service);
		}
	};

	using components::Result;

}

namespace stdx {

	template<class T>
	struct type_traits<factory::components::Principal<T>> {
		static constexpr const char*
		short_name() { return "kernel"; }
		typedef factory::components::kernel_category category;
	};

}

#endif // FACTORY_KERNEL_HH
