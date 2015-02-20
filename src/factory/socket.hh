namespace factory {

	// TODO: replace gethostbyname by getnameinfo
	void inet_address(const char* hostname, struct sockaddr_in* addr) {
		// gethostbyname needs locking beacuse it returns pointer
		// to the static data structure
		static std::mutex _mutex;
		std::unique_lock<std::mutex> lock(_mutex);
		struct hostent *host = ::gethostbyname(hostname);
		if (host == NULL) {
			std::stringstream msg;
			msg << "Can not find IP address of '" << hostname << "'. " << ::hstrerror(h_errno);
			throw Network_error(msg.str(), __FILE__, __LINE__, __func__);
		}
		std::memcpy((char*)&addr->sin_addr.s_addr, (char*)host->h_addr, host->h_length);
	}

	enum Socket_type {
		RELIABLE_SOCKET = SOCK_STREAM,
		UNRELIABLE_SOCKET = SOCK_DGRAM
	};

	struct Socket {

		typedef int Flag;
		typedef int Option;

		Socket(): _socket(0) {}
		Socket(int socket): _socket(socket) { flags(O_NONBLOCK); }
		Socket(const Socket& rhs): _socket(rhs._socket) {}

		void listen(Endpoint e, Socket_type type = RELIABLE_SOCKET) {
			struct sockaddr_in addr;
			init_socket_address(&addr, e.host().c_str(), e.port());
			check("socket()", _socket = ::socket(AF_INET, type | SOCK_NONBLOCK, 0));
			options(SO_REUSEADDR);
			check("bind()", ::bind(_socket, (struct sockaddr*)&addr, sizeof(addr)));
			std::clog << "Binding to " << e << std::endl;
			if (type == RELIABLE_SOCKET) {
				check("listen()", ::listen(_socket, SOMAXCONN));
				std::clog << "Listening on " << e << std::endl;
			}
		}

		void connect(Endpoint e, Socket_type type = RELIABLE_SOCKET) {
			try {
				struct sockaddr_in addr;
				init_socket_address(&addr, e.host().c_str(), e.port());
				check("socket()", _socket = ::socket(AF_INET, type | SOCK_NONBLOCK, 0));
				check_connect("connect123()", ::connect(_socket, (struct sockaddr*)&addr, sizeof(addr)));
				std::clog << "Connecting to " << e << std::endl;
			} catch (std::system_error& err) {
				std::clog << "Rethrowing connection error." << std::endl;
				throw Connection_error(err.what(), __FILE__, __LINE__, __func__);
			}
		}

		// Does not store client's address.
		std::pair<Socket, Endpoint> accept() {
			struct sockaddr_in addr;
			std::memset(&addr, 0, sizeof(addr));
			socklen_t acc_len = sizeof(addr);
			Socket socket = check("accept()", ::accept(_socket, (struct sockaddr*)&addr, &acc_len));
			char str_address[40];
			::inet_ntop(AF_INET, &addr.sin_addr.s_addr, str_address, 40);
//			std::clog << "Accepted connection from " << str_address
//				<< ':' << ntohs(addr.sin_port) << std::endl;
			Endpoint endpoint(str_address, ntohs(addr.sin_port));
			std::clog << "Accepted connection from " << endpoint << std::endl;
			return std::make_pair(socket, endpoint);
		}

		void close() {
			if (_socket > 0) {
				std::clog << "Closing socket " << _socket << std::endl;
				::shutdown(_socket, SHUT_RDWR);
				::close(_socket);
			}
		}

		void flags(Flag f) { ::fcntl(_socket, F_SETFL, flags() | f); }
		Flag flags() const { return ::fcntl(_socket, F_GETFL); }

		void options(Option option) {
			int one = 1;
			check("setsockopt()", ::setsockopt(_socket, SOL_SOCKET, option, &one, sizeof(one)));
		}

		Socket_type type() const {
			int tp = SOCK_STREAM;
			socklen_t tp_size = sizeof(tp);
			check("getsockopt()", ::getsockopt(_socket, SOL_SOCKET, SO_TYPE, &tp, &tp_size));
			return Socket_type(tp);
		}

		ssize_t read(char* buf, size_t size) {
			return ::read(_socket, buf, size);
		}

		ssize_t write(const char* buf, size_t size) {
			return ::write(_socket, buf, size);
		}

		Endpoint from() const {
			char dummy;
			struct ::sockaddr_in addr;
			std::memset(&addr, 0, sizeof(addr));
			socklen_t addr_len = sizeof(addr);
			check("recvfrom()", ::recvfrom(_socket, &dummy, 1, MSG_PEEK, (struct ::sockaddr*)&addr, &addr_len));
			char str_address[40];
			::inet_ntop(AF_INET, &addr.sin_addr.s_addr, str_address, 40);
			return Endpoint(str_address, ntohs(addr.sin_port));
		}

//		void send(Foreign_stream& packet) {
//			packet.insert_size();
//			std::stringstream msg;
//			msg << "write(" << _socket<< ')';
//			check(msg.str().c_str(), ::write(_socket, packet.buffer(), packet.size()));
//		}

		operator int() const { return _socket; }
		bool operator==(const Socket& rhs) const { return _socket == rhs._socket; }
		Socket& operator=(const Socket& rhs) { _socket = rhs._socket; return *this; }

		friend std::ostream& operator<<(std::ostream& out, const Socket& rhs) {
			return out << rhs._socket;
		}

		bool is_valid() const { return _socket > 0; }

	private:

		// TODO: connection status (success or failure) should be checked asynchronously in Socket_server
		static int check_connect(const char* func, int ret) {
			return (errno == EINPROGRESS) ? ret : check(func, ret);
		}

		static void init_socket_address(struct sockaddr_in* addr, const char* hostname, Port port) {
			std::memset(addr, 0, sizeof(sockaddr_in));
			addr->sin_family = AF_INET;
			addr->sin_port = htons(port);
			if (check("inet_pton()", ::inet_pton(AF_INET, hostname, &addr->sin_addr.s_addr)) == 0) {
				inet_address(hostname, addr);
			}
		}

		int _socket;
	};

}
