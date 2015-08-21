#include "libfactory.cc"

using namespace factory;

#include "datum.hh"

unix::endpoint server_endpoint("127.0.0.1", 10000);
unix::endpoint client_endpoint({127,0,0,1}, 20000);

const uint32_t NUM_SIZES = 1;
const uint32_t NUM_KERNELS = 2;
const uint32_t TOTAL_NUM_KERNELS = NUM_KERNELS * NUM_SIZES;

std::atomic<int> kernel_count(0);

struct Test_socket: public Mobile<Test_socket> {

	typedef stdx::log<Test_socket> this_log;

	Test_socket(): _data() {
	}

	explicit Test_socket(std::vector<Datum> x): _data(x) {
		++kernel_count;
	}

	virtual ~Test_socket() {
		--kernel_count;
	}

	void act() {
		commit(remote_server());
	}

	void write_impl(packstream& out) {
		out << uint32_t(_data.size());
		for (size_t i=0; i<_data.size(); ++i)
			out << _data[i];
	}

	void read_impl(packstream& in) {
		uint32_t sz;
		in >> sz;
		_data.resize(sz);
		for (size_t i=0; i<_data.size(); ++i)
			in >> _data[i];
	}

	std::vector<Datum> data() const { 
		this_log()
			<< "parent.id = " << (parent() ? parent()->id() : 12345)
			<< ", principal.id = " << (principal() ? principal()->id() : 12345)
			<< std::endl;
		return _data;
	}

	static void init_type(Type* t) {
		t->id(1);
		t->name("Test_socket");
	}
	
private:
	std::vector<Datum> _data;
};

struct Sender: public Identifiable<Kernel> {
	typedef stdx::log<Sender> this_log;

	Sender(uint32_t n, uint32_t s):
		_vector_size(n),
		_input(_vector_size),
		_sleep(s) {}

	void act() {
		this_log() << "Sender "
			<< "id = " << id()
			<< ", parent.id = " << (parent() ? parent()->id() : 12345)
			<< ", principal.id = " << (principal() ? principal()->id() : 12345)
			<< std::endl;
		for (uint32_t i=0; i<NUM_KERNELS; ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(_sleep));
			upstream(remote_server(), new Test_socket(_input));
//			this_log() << " Sender id = " << this->id() << std::endl;
		}
	}

	void react(Kernel* child) {

		Test_socket* test_kernel = dynamic_cast<Test_socket*>(child);
		std::vector<Datum> output = test_kernel->data();

		this_log() << "kernel = " << *test_kernel << std::endl;

		if (_input.size() != output.size())
			throw std::runtime_error("test_socket. Input and output size does not match.");

		for (size_t i=0; i<_input.size(); ++i) {
			if (_input[i] != output[i]) {
				std::stringstream msg;
				msg << "test_socket. Input and output does not match: ";
				msg << _input[i] << " != " << output[i];
				throw std::runtime_error(msg.str());
			}
		}

		this_log() << "Sender::kernel count = " << _num_returned+1 << std::endl;
		if (++_num_returned == NUM_KERNELS) {
			commit(the_server());
		}
	}

private:

	uint32_t _num_returned = 0;
	uint32_t _vector_size;

	std::vector<Datum> _input;
	uint32_t _sleep = 0;
};

struct Main: public Kernel {

	typedef stdx::log<Main> this_log;

	Main(uint32_t s): _sleep(s) {}

	void act() {
		for (uint32_t i=1; i<=NUM_SIZES; ++i)
			upstream(the_server(), new Sender(i, _sleep));
	}

	void react(Kernel*) {
		this_log() << "Main::kernel count = " << _num_returned+1 << std::endl;
		this_log() << "global kernel count = " << kernel_count << std::endl;
		if (++_num_returned == NUM_SIZES) {
			commit(the_server());
		}
	}

private:
	uint32_t _num_returned = 0;
	uint32_t _sleep = 0;
};

uint32_t sleep_time() {
	return unix::this_process::getenv("SLEEP_TIME", UINT32_C(0));
}

struct App {
	typedef stdx::log<App> this_log;
	int run(int argc, char* argv[]) {
		int retval = 0;
		if (argc <= 1) {
			this_log()
				<< "server=" << server_endpoint
				<< ",client=" << client_endpoint
				<< std::endl;
			uint32_t sleep = sleep_time();
			unix::procgroup procs;
			procs.add([&argv, sleep] () {
				unix::this_process::env("START_ID", 1000);
				return unix::this_process::execute(argv[0], 'x', sleep);
			});
			// wait for master to start
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			procs.add([&argv, sleep] () {
				unix::this_process::env("START_ID", 2000);
				return unix::this_process::execute(argv[0], 'y', sleep);
			});
			this_log() << "unix::proc group = " << procs << std::endl;
			procs.wait([] (const unix::proc& proc, unix::proc_info stat) {
				this_log() << "proc exited proc=" << proc
					<< ",status=" << stat.exit_code() << std::endl;
			});
			this_log() << "unix::log test " << std::endl;
//			retval = procs.wait();
		} else {
			try {
				if (argc != 3)
					throw std::runtime_error("Wrong number of arguments.");
				uint32_t sleep = 0;
				{
					std::stringstream s;
					s << argv[2];
					s >> sleep;
				}
				the_server()->add_cpu(0);
				if (argv[1][0] == 'x') {
					remote_server()->socket(server_endpoint);
					__factory.start();
					__factory.wait();
				}
				if (argv[1][0] == 'y') {
					remote_server()->socket(client_endpoint);
					remote_server()->peer(server_endpoint);
					__factory.start();
					the_server()->send(new Main(sleep));
					__factory.wait();
				}
			} catch (std::exception& e) {
				std::cerr << e.what() << std::endl;
				retval = 1;
			}
		}
		return retval;
	}
};

int main(int argc, char* argv[]) {
	App app;
	return app.run(argc, argv);
}
