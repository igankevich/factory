#ifndef SERVER_NIC_SERVER_HH
#define SERVER_NIC_SERVER_HH

#include "intro.hh"

namespace factory {

	namespace components {

		template<class T, class Socket, class Kernels=std::deque<T*>>
		struct Remote_Rserver: public Managed_object<Server<T>> {

			typedef char Ch;
			typedef basic_kernelbuf<basic_fdbuf<Ch,Socket>> Kernelbuf;
			typedef Packing_stream<Ch> stream_type;
			typedef Server<T> server_type;
			typedef Socket socket_type;
			typedef T kernel_type;
			typedef typename T::app_type app_type;
			typedef Kernels pool_type;
			typedef Managed_object<Server<T>> base_server;
			typedef stdx::log<Remote_Rserver> this_log;

			Remote_Rserver(socket_type&& sock, unix::endpoint vaddr):
				_vaddr(vaddr),
				_kernelbuf(),
				_stream(&this->_kernelbuf),
				_buffer(),
				_link(nullptr),
				_dirty(false)
			{
				this->_kernelbuf.setfd(std::move(sock));
			}

			Remote_Rserver(const Remote_Rserver&) = delete;
			Remote_Rserver& operator=(const Remote_Rserver&) = delete;

			Remote_Rserver(Remote_Rserver&& rhs):
				base_server(std::move(rhs)),
				_vaddr(rhs._vaddr),
				_kernelbuf(std::move(rhs._kernelbuf)),
				_stream(&_kernelbuf),
				_buffer(std::move(rhs._buffer)) ,
				_link(rhs._link),
				_dirty(rhs._dirty)
			{
				this_log() << "fd after move ctr " << _kernelbuf.fd() << std::endl;
				this_log() << "root after move ctr " << this->root()
					<< ",this=" << this << std::endl;
			}

			virtual
			~Remote_Rserver() {
//				this_log() << "deleting server " << *this << std::endl;
				while (!_buffer.empty()) {
					delete _buffer.front();
					_buffer.pop_front();
				}
			}

			Category
			category() const noexcept override {
				return Category{
					"nic_rserver",
					[] () { return nullptr; }
				};
			}

			void recover_kernels() {

				this->read_kernels();

				this_log()
					<< "Kernels left: "
					<< _buffer.size()
					<< std::endl;
				
				// recover kernels written to output buffer
				while (!this->_buffer.empty()) {
					this->recover_kernel(this->_buffer.front(), this->root());
					this->_buffer.pop_front();
				}
			}

			void send(kernel_type* kernel) {
				this_log() << "Remote_Rserver::send()" << std::endl;
				if (kernel->result() == Result::NO_PRINCIPAL_FOUND) {
					this_log() << "poll send error: tellp=" << _stream.tellp() << std::endl;
				}
				bool erase_kernel = true;
				if (!kernel->identifiable() && !kernel->moves_everywhere()) {
					kernel->id(factory_generate_id());
					erase_kernel = false;
					this_log() << "Kernel generate id = " << kernel->id() << std::endl;
				}
				if ((kernel->moves_upstream() || kernel->moves_somewhere()) && kernel->identifiable()) {
					_buffer.push_back(kernel);
					erase_kernel = false;
					this_log() << "Buffer size = " << _buffer.size() << std::endl;
				}
				this->write_kernel(*kernel);
				this->_dirty = true;
				if (erase_kernel && !kernel->moves_everywhere()) {
					this_log() << "Delete kernel " << *kernel << std::endl;
					delete kernel;
				}
			}

			bool valid() const {
//				TODO: It is probably too slow to check error on every event.
				return this->socket().error() == 0;
			}

			void operator()(unix::poll_event& event) {
				if (this->valid()) {
					this->handle_event(event);
				} else {
					event.setrev(unix::poll_event::Hup);
				}
			}

			void simulate(unix::poll_event event) {
				this->handle_event(event);
			}

			void handle_event(unix::poll_event& event) {
				bool overflow = false;
				if (event.in()) {
					this_log() << "recv rdstate="
						<< bits::debug_stream(_stream) << ",event=" << event << std::endl;
					while (this->_stream) {
						try {
							this->read_and_send_kernel();
						} catch (No_principal_found<kernel_type>& err) {
							this->return_kernel(err.kernel());
							overflow = true;
						}
					}
					this->_stream.clear();
				}
				if (event.out() && !event.hup()) {
					this_log() << "Send rdstate=" << bits::debug_stream(this->_stream) << std::endl;
					this->_stream.flush();
					this->socket().flush();
					if (this->_stream) {
						this_log() << "Flushed." << std::endl;
					}
					if (!this->_stream || !this->socket().empty()) {
						overflow = true;
						this->_stream.clear();
					}
				}
				if (overflow) {
					event.setev(unix::poll_event::Out);
				} else {
					event.unsetev(unix::poll_event::Out);
					this->_dirty = false;
				}
			}

			bool dirty() const { return this->_dirty; }
			int fd() const { return this->socket().fd(); }
			const socket_type& socket() const { return this->_kernelbuf.fd(); }
			socket_type& socket() { return this->_kernelbuf.fd(); }
			void socket(unix::socket&& rhs) {
				this->_stream >> underflow;
				this->_stream.clear();
				this->_kernelbuf.setfd(socket_type(std::move(rhs)));
			}
			unix::endpoint bind_addr() const { return this->socket().bind_addr(); }
			const unix::endpoint& vaddr() const { return this->_vaddr; }
			void setvaddr(const unix::endpoint& rhs) { this->_vaddr = rhs; }

			bool empty() const { return this->_buffer.empty(); }

			Remote_Rserver* link() const { return this->_link; }
			void link(Remote_Rserver* rhs) { this->_link = rhs; }

			friend std::ostream&
			operator<<(std::ostream& out, const Remote_Rserver& rhs) {
				return out << "{vaddr="
					<< rhs.vaddr() << ",sock="
					<< rhs.socket() << ",kernels="
					<< rhs._buffer.size() << ",str="
					<< bits::debug_stream(rhs._stream) << '}';
			}

		private:

			void read_and_send_kernel() {
				app_type app;
				this->_stream >> app;
				if (!this->_stream) return;
				if (app != Application::ROOT) {
					factory::components::forward_to_app(app, this->_vaddr, this->_kernelbuf);
				} else {
					Type<kernel_type>::read_object(this->_stream,
						[this,app] (kernel_type* k) {
							send_kernel(k, app);
						}
					);
				}
			}

			void
			send_kernel(kernel_type* k, app_type app) {
				k->from(_vaddr);
				k->setapp(app);
				this_log()
					<< "recv kernel=" << *k
					<< ",rdstate=" << bits::debug_stream(this->_stream)
					<< std::endl;
				if (k->moves_downstream()) {
					this->clear_kernel_buffer(k);
				}
				if (k->principal()) {
					kernel_type* p = Type<kernel_type>::instances().lookup(k->principal()->id());
					if (p == nullptr) {
						k->result(Result::NO_PRINCIPAL_FOUND);
						throw No_principal_found<kernel_type>(k);
					}
					k->principal(p);
				}
				this->root()->send(k);
			}

			void return_kernel(kernel_type* k) {
				this_log()
					<< "No principal found for "
					<< *k << std::endl;
				k->principal(k->parent());
				// TODO : not safe with bare pointer
//				if (this->link()) {
//					this->link()->send(k);
//				} else {
					this->send(k);
//				}
			}

			void write_kernel(kernel_type& kernel) {
				typedef packstream::pos_type pos_type;
				pos_type old_pos = this->_stream.tellp();
				this->_stream << kernel.app();
				Type<kernel_type>::write_object(kernel, this->_stream);
				pos_type new_pos = this->_stream.tellp();
				this->_stream << end_packet;
				this_log() << "send bytes="
					<< new_pos-old_pos
					<< ",stream="
					<< bits::debug_stream(this->_stream)
					<< ",krnl=" << kernel
					<< std::endl;
			}

			void recover_kernel(kernel_type* k, server_type* root) {
				k->from(k->to());
				k->result(Result::ENDPOINT_NOT_CONNECTED);
				k->principal(k->parent());
				root->send(k);
			}

			void clear_kernel_buffer(kernel_type* k) {
				auto pos = std::find_if(_buffer.begin(), _buffer.end(), [k] (kernel_type* rhs) {
					return *rhs == *k;
				});
				if (pos != _buffer.end()) {
					this_log() << "Kernel erased " << k->id() << std::endl;
					delete *pos;
					this->_buffer.erase(pos);
					this_log() << "Buffer size = " << _buffer.size() << std::endl;
				} else {
					this_log() << "Kernel not found " << k->id() << std::endl;
				}
			}
			
			void read_kernels() {
				// Here failed kernels are written to buffer,
				// from which they must be recovered with recover_kernels().
				this->simulate(unix::poll_event{this->fd(), unix::poll_event::In});
			}

			unix::endpoint _vaddr;
			Kernelbuf _kernelbuf;
			stream_type _stream;
			pool_type _buffer;

			Remote_Rserver* _link;
			volatile bool _dirty = false;
		};

		template<class T, class Socket, class Kernels=std::queue<T*>, class Threads=std::vector<std::thread>>
		using NIC_server_base = Server_with_pool<T, Kernels, Threads,
			stdx::spin_mutex, stdx::simple_lock<stdx::spin_mutex>,
			unix::event_poller<Remote_Rserver<T, Socket>*>>;

		template<class T, class Socket>
		struct NIC_server: public NIC_server_base<T, Socket> {

			typedef Remote_Rserver<T, Socket> server_type;
			typedef std::map<unix::endpoint, server_type> upstream_type;
			typedef Socket socket_type;

			typedef bits::handler_wrapper<unix::pointer_tag> wrapper;

			typedef NIC_server_base<T, Socket> base_server;
			using typename base_server::kernel_type;
			using typename base_server::mutex_type;
			using typename base_server::lock_type;
			using typename base_server::sem_type;
			using typename base_server::kernel_pool;

			typedef server_type* handler_type;
			typedef unix::event_poller<handler_type> poller_type;
			typedef stdx::log<NIC_server> this_log;

			NIC_server(NIC_server&& rhs) noexcept:
			base_server(std::move(rhs))
			{}

			NIC_server():
			base_server(1u)
			{}

			~NIC_server() = default;
			NIC_server(const NIC_server&) = delete;
			NIC_server& operator=(const NIC_server&) = delete;

			void
			do_run() override {
				// start processing as early as possible
				this->process_kernels();
//				while (!this->stopped() && _stop_iterations < MAX_STOP_ITERATIONS) {
				while (!this->stopped()) {
					cleanup_and_check_if_dirty();
					lock_type lock(this->_mutex);
					this->_semaphore.wait(lock,
						[this] () { return this->stopped(); });
					lock.unlock();
					check_and_process_kernels();
					check_and_accept_connections();
					read_and_write_kernels();
				}
				// prevent double free or corruption
				poller().clear();
			}

			void cleanup_and_check_if_dirty() {
				poller().for_each_ordinary_fd(
					[this] (unix::poll_event& ev, handler_type& h) {
						if (!ev) {
							this->remove_server(h);
						} else if (wrapper::dirty(h)) {
							ev.setev(unix::poll_event::Out);
						}
					}
				);
			}

			void
			check_and_process_kernels() {
				if (this->stopped()) {
					this->try_to_stop_gracefully();
				} else {
					poller().for_each_pipe_fd([this] (unix::poll_event& ev) {
						if (ev.in()) {
							this->process_kernels();
						}
					});
				}
			}

			void
			check_and_accept_connections() {
				poller().for_each_special_fd([this] (unix::poll_event& ev) {
					if (ev.in()) {
						this->accept_connection();
					}
				});
			}

			void
			read_and_write_kernels() {
				poller().for_each_ordinary_fd(
					[this] (unix::poll_event& ev, handler_type& h) {
						if (wrapper::dirty(h)) {
							ev.setrev(unix::poll_event::Out);
						}
						wrapper::handle(h, ev);
					}
				);
			}

			void remove_server(server_type* ptr) {
				// remove from the mapping if it is not linked
				// with other subordinate server
				// TODO: occasional ``Bad file descriptor''
//				this_log() << "Removing server " << *ptr << std::endl;
				if (!ptr->link()) {
					this->_upstream.erase(ptr->vaddr());
				}
				ptr->recover_kernels();
			}

			void accept_connection() {
				unix::endpoint addr;
				socket_type sock;
				this->_socket.accept(sock, addr);
				unix::endpoint vaddr = virtual_addr(addr);
				this_log()
					<< "after accept: socket="
					<< sock << std::endl;
				auto res = this->_upstream.find(vaddr);
				if (res == this->_upstream.end()) {
					this->add_connected_server(std::move(sock), vaddr, unix::poll_event::In);
					this_log()
						<< "connected peer "
						<< vaddr << std::endl;
				} else {
					server_type& s = res->second;
					this_log()
						<< "ports: "
						<< addr.port() << ' '
						<< s.bind_addr().port()
						<< std::endl;
					if (addr.port() < s.bind_addr().port()) {
						this_log()
							<< "not replacing peer " << s
							<< std::endl;
						// create temporary subordinate server
						// to read kernels until the socket
						// is closed from the other end
						server_type* new_s = new server_type(std::move(sock), addr);
						this->link_server(&s, new_s, unix::poll_event{new_s->fd(), unix::poll_event::In});
						debug("not replacing upstream");
					} else {
						this_log log;
						log << "replacing peer " << s;
						poller().disable(s.fd());
						server_type new_s(std::move(s));
						new_s.setparent(this);
						new_s.socket(std::move(sock));
						_upstream.erase(res);
						_upstream.emplace(vaddr, std::move(new_s));
//						this->_upstream.emplace(vaddr, std::move(*new_s));
						poller().emplace(
							unix::poll_event{res->second.fd(), unix::poll_event::Inout, unix::poll_event::Inout},
							handler_type(&res->second));
						log << " with " << res->second << std::endl;
						debug("replacing upstream");
					}
				}
				this_log()
					<< "after add: socket="
					<< sock << std::endl;
			}

			void try_to_stop_gracefully() {
				this->process_kernels();
				this->flush_kernels();
				++this->_stop_iterations;
				if (this->empty() || this->_stop_iterations == MAX_STOP_ITERATIONS) {
					debug("stopping");
				} else {
					debug("not stopping");
				}
			}

			bool empty() const {
				return std::all_of(stdx::field_iter<1>(_upstream.begin()),
					stdx::field_iter<1>(_upstream.end()),
					std::mem_fn(&server_type::empty));
//				typedef typename upstream_type::value_type pair_type;
//				return std::all_of(_upstream.cbegin(), _upstream.cend(),
//					[] (const pair_type& rhs)
//				{
//					return rhs.second.empty();
//				});
			}

			void peer(unix::endpoint addr) {
//				if (!this->stopped()) {
//					throw Error("Can not add upstream server when socket server is running.",
//						__FILE__, __LINE__, __func__);
//				}
				this->connect_to_server(addr, unix::poll_event::In);
			}

			void
			socket(const unix::endpoint& addr) {
				this->_socket.bind(addr);
				this->_socket.listen();
				poller().insert_special(unix::poll_event{this->_socket.fd(),
					unix::poll_event::In});
				if (!this->stopped()) {
					this->_semaphore.notify_one();
				}
			}

			inline unix::endpoint
			server_addr() const {
				return this->_socket.bind_addr();
			}

			Category
			category() const noexcept override {
				return Category{
					"nic_server",
					[] () { return new NIC_server; },
					{"endpoint"},
					[] (const void* obj, Category::key_type key) {
						const NIC_server* lhs = static_cast<const NIC_server*>(obj);
						if (key == "endpoint") {
							std::stringstream tmp;
							tmp << lhs->server_addr();
							return tmp.str();
						}
						return Category::value_type();
					}
				};
			}
		
		private:

			inline unix::endpoint
			virtual_addr(unix::endpoint addr) const {
				return unix::endpoint(addr, server_addr().port());
			}

			void
			flush_kernels() {
				typedef typename upstream_type::value_type pair_type;
				std::for_each(this->_upstream.begin(),
					this->_upstream.end(), [] (pair_type& rhs)
					{ rhs.second.simulate(unix::poll_event{rhs.second.fd(), unix::poll_event::Out}); });
			}

			void
			process_kernels() {
				this_log() << "NIC_server::process_kernels()" << std::endl;
				stdx::front_pop_iterator<kernel_pool> it_end;
				lock_type lock(this->_mutex);
				stdx::for_each_thread_safe(lock,
					stdx::front_popper(this->_kernels), it_end,
					[this] (kernel_type* rhs) { process_kernel(rhs); }
				);
			}

			void
			process_kernel(kernel_type* k) {
				if (this->server_addr() && k->to() == this->server_addr()) {
					std::ostringstream msg;
					msg << "Kernel is sent to local node. From="
						<< this->server_addr() << ", to=" << k->to();
					throw Error(msg.str(), __FILE__, __LINE__, __func__);
				}

				if (k->moves_everywhere()) {
					this_log() << "broadcast kernel" << std::endl;
					for (auto& pair : _upstream) {
						pair.second.send(k);
					}
					// delete broadcast kernel
					delete k;
				} else if (k->moves_upstream() && k->to() == unix::endpoint()) {
					if (_upstream.empty()) {
						throw Error("No upstream servers found.", __FILE__, __LINE__, __func__);
					}
					// TODO: round robin
					auto result = _upstream.begin();
					result->second.send(k);
				} else {
					// create endpoint if necessary, and send kernel
					if (!k->to()) {
						k->to(k->from());
					}
					this->find_or_create_peer(k->to(), unix::poll_event::Inout)->send(k);
				}
			}

			server_type* find_or_create_peer(const unix::endpoint& addr, unix::poll_event::legacy_event ev) {
				server_type* ret;
				auto result = _upstream.find(addr);
				if (result == _upstream.end()) {
					ret = this->connect_to_server(addr, ev);
				} else {
					ret = &result->second;
				}
				return ret;
			}

			struct print_values {
				explicit print_values(const upstream_type& rhs): map(rhs) {}
				friend std::ostream& operator<<(std::ostream& out, const print_values& rhs) {
					out << '{';
					std::copy(stdx::field_iter<1>(rhs.map.begin()),
						stdx::field_iter<1>(rhs.map.end()),
						stdx::intersperse_iterator<server_type>(out, ","));
					out << '}';
					return out;
				}
			private:
				const upstream_type& map;
			};

			void debug(const char* msg = "") {
				this_log()
					<< msg << " upstream " << print_values(this->_upstream) << std::endl
					<< msg << " events " << poller() << std::endl;
			}

			server_type* connect_to_server(unix::endpoint addr, unix::poll_event::legacy_event events) {
				// bind to server address with ephemeral port
				unix::endpoint srv_addr(this->server_addr(), 0);
				return this->add_connected_server(socket_type(srv_addr, addr), addr, events);
			}

			server_type* add_connected_server(socket_type&& sock, unix::endpoint vaddr,
				unix::poll_event::legacy_event events,
				unix::poll_event::legacy_event revents=0)
			{
				unix::fd_type fd = sock.fd();
				server_type s(std::move(sock), vaddr);
				s.setparent(this);
				auto result = this->_upstream.emplace(vaddr, std::move(s));
				poller().emplace(
					unix::poll_event{fd, events, revents},
					handler_type(&result.first->second));
				this_log() << "added server " << result.first->second << std::endl;
				return &result.first->second;
			}

			void link_server(server_type* par, server_type* sub, unix::poll_event ev) {
				sub->setparent(this);
				sub->setvaddr(par->vaddr());
				sub->link(par);
				poller().emplace(ev, handler_type(sub));
			}

			inline sem_type&
			poller() {
				return this->_semaphore;
			}

			inline const sem_type&
			poller() const {
				return this->_semaphore;
			}

			socket_type _socket;
			upstream_type _upstream;

			int _stop_iterations = 0;

			static const int MAX_STOP_ITERATIONS = 13;
		};

	}

	namespace stdx {

		template<class T, class Socket>
		struct type_traits<components::NIC_server<T,Socket>> {
			static constexpr const char*
			short_name() { return "nic_server"; }
			typedef components::server_category category;
		};

		template<class T, class Socket, class Kernels>
		struct type_traits<components::Remote_Rserver<T, Socket, Kernels>> {
			static constexpr const char*
			short_name() { return "nic_rserver"; }
			typedef components::server_category category;
		};

//		template<>
//		struct disable_log_category<server_category>:
//		public std::integral_constant<bool, true> {};
	
	}

}
#endif // SERVER_NIC_SERVER_HH