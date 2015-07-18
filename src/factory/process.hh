#ifndef FACTORY_PROCESS
#define FACTORY_PROCESS
namespace factory {

	/// Fast mutex for scheduling strategies.
	struct Spin_mutex {

		void lock() noexcept { while (_flag.test_and_set(std::memory_order_acquire)); }
		void unlock() noexcept { _flag.clear(std::memory_order_release); }
		bool try_lock() noexcept { return !_flag.test_and_set(std::memory_order_acquire); }

		Spin_mutex() = default;

		// disallow copy & move operations
		Spin_mutex(const Spin_mutex&) = delete;
		Spin_mutex(Spin_mutex&&) = delete;
		Spin_mutex& operator=(const Spin_mutex&) = delete;
		Spin_mutex& operator=(Spin_mutex&&) = delete;

	private:
		std::atomic_flag _flag = ATOMIC_FLAG_INIT;
	};

	typedef ::pid_t Process_id;

	namespace this_process {

		Process_id id() { return ::getpid(); }
		Process_id parent_id() { return ::getppid(); }

		template<class T>
		T getenv(const char* key, T dflt) {
			const char* text = ::getenv(key);
			if (!text) { return dflt; }
			std::stringstream s;
			s << text;
			T val;
			return (s >> val) ? val : dflt;
		}

	}

}
#else
namespace factory {

	namespace components {
		struct To_string {

			template<class T>
			To_string(T rhs): _s(to_string(rhs)) {}

			const char* c_str() const { return _s.c_str(); }

		private:

			template<class T>
			static std::string to_string(T rhs) {
				std::stringstream s;
				s << rhs;
				return s.str();
			}

			std::string _s;
		};
	}

	typedef struct ::sigaction Basic_action;

	struct Action: public Basic_action {
		explicit Action(void (*func)(int)) noexcept {
			this->sa_handler = func;
		}
	};

	namespace this_process {

		// TODO: setenv() is known to cause memory leaks
		template<class T>
		void env(const char* key, T val) {
			check("setenv()", ::setenv(key, components::To_string(val).c_str(), 1));
		}

		template<class ... Args>
		int execute(const Args& ... args) {
			components::To_string tmp[] = { args... };
			const size_t argc = sizeof...(Args);
			char* argv[argc + 1];
			for (size_t i=0; i<argc; ++i) {
				argv[i] = const_cast<char*>(tmp[i].c_str());
			}
			argv[argc] = 0;
			return check("execve()", ::execv(argv[0], argv));
		}

		void bind_signal(int signum, const Action& action) {
			check("sigaction()", ::sigaction(signum, &action, 0));
		}

	}

	struct Process {

		constexpr Process() {}

		template<class F>
		explicit Process(F f) { run(f); }

		template<class F>
		void run(F f) {
			_child_pid = ::fork();
			if (_child_pid == 0) {
				int ret = f();
				Logger<Level::COMPONENT>() << ": exit(" << ret << ')' << std::endl;
				std::exit(ret);
			}
		}

		void stop() {
			if (_child_pid > 0) {
		    	check("kill()", ::kill(_child_pid, SIGHUP));
			}
		}

		int wait() {
			int ret = 0, sig = 0;
			if (_child_pid > 0) {
				int status = 0;
//				check("waitpid()", ::waitpid(_child_pid, &status, 0));
				::waitpid(_child_pid, &status, 0);
				ret = WEXITSTATUS(status);
				if (WIFSIGNALED(status)) {
					sig = WTERMSIG(status);
				}
				Logger<Level::COMPONENT>()
					<< _child_pid << ": waitpid(), ret=" << ret
					<< ", sig=" << sig << std::endl;
			}
			return ret | sig;
		}

		friend std::ostream& operator<<(std::ostream& out, const Process& rhs) {
			return out << rhs.id();
		}

		constexpr Process_id id() const { return _child_pid; }

	private:
		Process_id _child_pid = 0;
	};

	struct Process_group {

		template<class F>
		void add(F f) {
			_procs.push_back(Process(f));
		}

		int wait() {
			int ret = 0;
			for (Process& p : _procs) ret += std::abs(p.wait());
			return ret;
		}

		void stop() {
			for (Process& p : _procs)
				p.stop();
		}

		Process& operator[](size_t i) { return _procs[i]; }
		const Process& operator[](size_t i) const { return _procs[i]; }

		friend std::ostream& operator<<(std::ostream& out, const Process_group& rhs) {
			for (const Process& p : rhs._procs) {
				out << p << ' ';
			}
			return out;
		}

	private:
		std::vector<Process> _procs;
	};

	struct Command_line {

		Command_line(int argc, char* argv[]): cmdline() {
			std::ostream_iterator<char*> it(cmdline, " ");
			std::copy(argv + 1, argv + argc, it);
		}

		template<class F>
		void parse(F handle) {
			std::string arg;
			while (cmdline >> std::ws >> arg) {
				handle(arg, cmdline);
			}
		}

	private:
		std::stringstream cmdline;
	};

	template<class T>
	struct shared_mem {
	
		typedef uint8_t proj_id_type;
		typedef ::key_t key_type;
		typedef size_t size_type;
		typedef unsigned short perms_type;
		typedef int id_type;
		typedef int shmid_type;
		typedef void* addr_type;
		typedef T value_type;
	
		shared_mem(id_type id, size_type sz, proj_id_type num, perms_type perms = DEFAULT_SHMEM_PERMS):
			_id(id), _size(sz), _key(this->genkey(num)),
			_shmid(this->createshmem(perms)),
			_addr(this->attach()), _owner(true)
		{
			this->fillshmem();
			Logger<Level::SHMEM>()
				<< "shared_mem server: "
				<< "shmem=" << *this
				<< std::endl;
		}
	
		explicit shared_mem(id_type id, proj_id_type num):
			_id(id), _size(0), _key(this->genkey(num)),
			_shmid(this->getshmem()),
			_addr(this->attach())
		{
			this->_size = this->load_size();
			Logger<Level::SHMEM>()
				<< "shared_mem client: "
				<< "shmem=" << *this
				<< std::endl;
		}
	
		~shared_mem() {
			this->detach();
			if (this->is_owner()) {
				this->rmshmem();
				this->rmfile();
			}
		}
	
		shared_mem(const shared_mem&) = delete;
		shared_mem& operator=(const shared_mem&) = delete;
	
		addr_type ptr() { return this->_addr; }
		const addr_type ptr() const { return this->_addr; }
		size_type size() const { return this->_size; }
		bool is_owner() const { return this->_owner; }

		value_type* begin() { return static_cast<value_type*>(this->_addr); }
		value_type* end() { return static_cast<value_type*>(this->_addr) + this->_size; }
	
		const value_type* begin() const { return static_cast<value_type*>(this->_addr); }
		const value_type* end() const { return static_cast<value_type*>(this->_addr) + this->_size; }

		void resize(size_type new_size) {
			if (new_size > this->_size) {
				this->detach();
				if (this->is_owner()) {
					this->rmshmem();
				}
				this->_size = new_size;
				this->_shmid = this->createshmem(this->getperms());
				this->attach();
			}
		}

		void sync() {
			shmid_type newid = this->getshmem();
			if (newid != this->_shmid) {
				Logger<Level::SHMEM>()
					<< "detach/attach sync"
					<< std::endl;
				this->detach();
				if (this->is_owner()) {
					this->rmshmem();
				}
				this->_shmid = newid;
				this->attach();
			}
			this->_size = this->load_size();
		}

	private:

		void fillshmem() {
			std::fill_n(this->begin(),
				this->size(), value_type());
		}

		key_type genkey(proj_id_type num) const {
			std::string path = filename(this->_id);
			{ std::filebuf out; out.open(path, std::ios_base::out); }
			return check(::ftok(path.c_str(), num),
				__FILE__, __LINE__, __func__);
		}

		shmid_type createshmem(perms_type perms) const {
			return check(::shmget(this->_key,
				this->size(), perms | IPC_CREAT),
				__FILE__, __LINE__, __func__);
		}

		shmid_type getshmem() const {
			return check(::shmget(this->_key, 0, 0),
				__FILE__, __LINE__, __func__);
		}

		addr_type attach() const {
			return check(::shmat(this->_shmid, 0, 0),
				__FILE__, __LINE__, __func__);
		}

		void detach() {
			if (this->_addr) {
				check(::shmdt(this->_addr),
					__FILE__, __LINE__, __func__);
			}
		}

		void rmshmem() {
			check(::shmctl(this->_shmid, IPC_RMID, 0),
				__FILE__, __LINE__, __func__);
		}

		void rmfile() {
			std::string path = this->filename(this->_id);
			Logger<Level::SHMEM>()
				<< "rmfile path=" << path
				<< ",shmem=" << *this
				<< std::endl;
			check(::remove(path.c_str()),
				__FILE__, __LINE__, __func__);
		}

		size_type load_size() {
			::shmid_ds stat;
			check(::shmctl(_shmid, IPC_STAT, &stat),
				__FILE__, __LINE__, __func__);
			return stat.shm_segsz;
		}

		perms_type getperms() {
			::shmid_ds stat;
			check(::shmctl(_shmid, IPC_STAT, &stat),
				__FILE__, __LINE__, __func__);
			return stat.shm_perm.mode;
		}
		
		static
		std::string filename(id_type id) {
			std::ostringstream path;
			path << "/var/tmp";
			path << "/factory.";
			path << id;
			path << ".shmem";
			return path.str();
		}
	
		friend std::ostream& operator<<(std::ostream& out, const shared_mem& rhs) {
			return out << "{addr=" << rhs.ptr()
				<< ",size=" << rhs.size()
				<< ",owner=" << rhs.is_owner()
				<< ",key=" << rhs._key
				<< ",shmid=" << rhs._shmid
				<< '}';
		}
	
		id_type _id = 0;
		size_type _size = 0;
		key_type _key = 0;
		shmid_type _shmid = 0;
		addr_type _addr = nullptr;
		bool _owner = false;
	
		static const perms_type DEFAULT_SHMEM_PERMS = 0666;
	};

	struct Semaphore {

		explicit Semaphore(const std::string& name): Semaphore(name.c_str()) {}
		explicit Semaphore(const char* name) {
			_sem = check("sem_open()", ::sem_open(name, O_CREAT, 0666, 0), SEM_FAILED);
		}
		~Semaphore() { check("sem_close()", ::sem_close(_sem)); }

		void wait() {
			std::cout << "sem = " << _sem << std::endl;
			check("sem_wait()", ::sem_wait(_sem));
		}
		void notify_one() { check("sem_post()", ::sem_post(_sem)); }
		void lock() { this->wait(); }
		void unlock() { this->notify_one(); }
	
		Semaphore& operator=(const Semaphore&) = delete;
		Semaphore(const Semaphore&) = delete;
	
	private:
		::sem_t* _sem;
	};

}
#endif
