#include <factory/factory.hh>
#include "test.hh"

using namespace factory;

template<class T, template<class X> class B>
void test_buffer() {

	std::default_random_engine generator;
	std::uniform_int_distribution<T> distribution(std::numeric_limits<T>::min(),std::numeric_limits<T>::max());
	auto dice = std::bind(distribution, generator);

	typedef typename B<T>::Size I;
	const I MAX_SIZE_POWER = 12;

	for (I k=1; k<=133; ++k) {
		const I chunk_size = k;
		for (I i=0; i<=MAX_SIZE_POWER; ++i) {
			I size = I(2) << i;
			std::vector<T> input(size);
			for (size_t j=0; j<size; ++j)
				input[j] = dice();
			B<T> buf(chunk_size);
			if (!buf.empty()) {
				std::stringstream msg;
				msg << "buffer is not empty before write: size=";
				msg << buf.size();
				throw Error(msg.str(), __FILE__, __LINE__, __func__);
			}
			buf.write(&input[0], size);
			if (buf.size() != input.size()) {
				std::stringstream msg;
				msg << "buffer size is not equal to input size: ";
				msg << buf.size() << " != " << input.size();
				throw Error(msg.str(), __FILE__, __LINE__, __func__);
			}
			std::vector<T> output(size);
			buf.read(&output[0], size);
			if (!buf.empty()) {
				std::stringstream msg;
				msg << "buffer is not empty after read: size=";
				msg << buf.size();
				throw Error(msg.str(), __FILE__, __LINE__, __func__);
			}
			for (size_t j=0; j<size; ++j) {
				if (input[j] != output[j]) {
					std::stringstream msg;
					msg << "input and output does not match:\n'"
						<< input[j] << "'\n!=\n'" << output[j] << "'";
					throw Error(msg.str(), __FILE__, __LINE__, __func__);
				}
			}
		}
	}
}

template<class T, class Fd=int>
void test_fdbuf() {
	std::string filename = "/tmp/"
		+ test::random_string<char>(16, 'a', 'z')
		+ ".factory";
	const size_t MAX_K = 1 << 20;
	for (size_t k=1; k<=MAX_K; k<<=1) {
		// fill file with random contents
		std::basic_string<T> expected_contents = test::random_string<T>(k, 'a', 'z');
		{
			std::clog << "Checking overflow()" << std::endl;
			File file(filename, O_WRONLY | O_CREAT | O_TRUNC,  S_IRUSR | S_IWUSR);
			factory::basic_ofdstream<T,Fd>(file.fd()) << expected_contents;
		}
		{
			std::clog << "Checking underflow()" << std::endl;
			File file(filename, O_RDONLY);
			factory::basic_ifdstream<T,Fd> in(file.fd());
			std::basic_stringstream<T> contents;
			contents << in.rdbuf();
			std::basic_string<T> result = contents.str();
			if (result != expected_contents) {
				std::stringstream msg;
				msg << "input and output does not match:\n'"
					<< expected_contents << "'\n!=\n'" << result << "'";
				throw Error(msg.str(), __FILE__, __LINE__, __func__);
			}
		}
//		{
//			std::clog << "Checking seekg()" << std::endl;
//			File file(nm, O_RDONLY);
//			factory::basic_ifdstream<T,Fd> in(file.fd());
//			test::equal(in.tellg(), 0);
//			in.seekg(k, std::ios_base::beg);
//			test::equal(in.tellg(), k);
//			in.seekg(0, std::ios_base::beg);
//			test::equal(in.tellg(), 0);
//		}
//		{
//			std::clog << "Checking seekp()" << std::endl;
//			File file(nm, O_WRONLY);
//			factory::basic_ofdstream<T,Fd> in(file.fd());
//			test::equal(in.tellp(), 0);
//			in.seekp(k, std::ios_base::beg);
//			test::equal(in.tellp(), k);
//			in.seekp(0, std::ios_base::beg);
//			test::equal(in.tellp(), 0);
//		}
		{
			std::clog << "Checking flush()" << std::endl;
			File file(filename, O_WRONLY);
			factory::basic_ofdstream<T,Fd> out(file.fd());
			test::equal(out.eof(), false);
			out.flush();
			test::equal(out.tellp(), 0);
		}
	}
	check("remove()", std::remove(filename.c_str()));
}

template<class T>
void test_filterbuf() {
	using namespace factory::components;
	const size_t MAX_K = 1 << 20;
	for (size_t k=1; k<=MAX_K; k<<=1) {
		std::basic_string<T> contents = test::random_string<T>(k, 0, 31);
		std::basic_stringstream<T> out;
		std::basic_streambuf<T>* orig = out.rdbuf();
		static_cast<std::basic_ostream<T>&>(out).rdbuf(new Filter_buf<T>(orig));
		out << contents;
		std::basic_string<T> result = out.str();
		std::size_t cnt = std::count_if(result.begin(), result.end(), [] (T ch) {
			return ch != '\r' && ch != '\n';
		});
		if (cnt > 0) {
			std::stringstream msg;
			msg << "output stream is not properly filtered: rdbuf='"
				<< out.rdbuf() << '\'';
			throw Error(msg.str(), __FILE__, __LINE__, __func__);
		}
	}
}

template<class T, class Fd=int>
void test_kernelbuf() {
	std::clog << "Checking kernelbuf" << std::endl;
	std::string filename = "/tmp/"
		+ test::random_string<char>(16, 'a', 'z')
		+ ".factory";
	typedef basic_ikernelbuf<basic_fdbuf<T,Fd>> ikernelbuf;
	typedef basic_okernelbuf<basic_fdbuf<T,Fd>> okernelbuf;
	const size_t MAX_K = 1 << 20;
	for (size_t k=1; k<=MAX_K; k<<=1) {
		std::basic_string<T> contents = test::random_string<T>(k, 'a', 'z');
		{
			File file(filename, O_WRONLY | O_CREAT | O_TRUNC,  S_IRUSR | S_IWUSR);
			okernelbuf buf;
			buf.setfd(file.fd());
			std::basic_ostream<T> out(&buf);
			out << contents << std::flush;
		}
		{
			File file(filename, O_RDONLY);
			ikernelbuf buf;
			buf.setfd(file.fd());
			std::basic_istream<T> in(&buf);
			std::basic_string<T> result(k, '_');
			in.read(&result[0], k);
			if (in.gcount() < k) {
				in.clear();
				in.read(&result[0], k);
			}
//			std::clog << "Result: "
//				<< "tellg=" << in.tellg()
//				<< ", size=" << result.size()
//				<< ", result='" << Binary<std::basic_string<T>>(result) << '\'' << std::endl;
			if (result != contents) {
				std::stringstream msg;
				msg << "input and output does not match:\n'"
					<< contents << "'\n!=\n'" << result << "'";
				throw Error(msg.str(), __FILE__, __LINE__, __func__);
			}
		}
	}
	check("remove()", std::remove(filename.c_str()));
}

template<class T>
void test_kernelbuf_with_stringstream() {
	typedef basic_ikernelbuf<std::basic_stringbuf<T>> ikernelbuf;
	typedef basic_okernelbuf<std::basic_stringbuf<T>> okernelbuf;
	typedef basic_kernelbuf<std::basic_stringbuf<T>> kernelbuf;
	const size_t MAX_K = 1 << 20;
	for (size_t k=1; k<=MAX_K; k<<=1) {
		std::basic_string<T> contents = test::random_string<T>(k, 'a', 'z');
		std::basic_stringstream<T> out;
		std::basic_streambuf<T>* orig = out.rdbuf();
		kernelbuf buf;
		static_cast<std::basic_ostream<T>&>(out).rdbuf(&buf);
		out.write(contents.data(), contents.size());
		out.flush();
		std::basic_string<T> result(contents.size(), '_');
		out.read(&result[0], result.size());
		if (result != contents) {
			std::stringstream msg;
			msg << "input and output does not match:\n'"
				<< contents << "'\n!=\n'" << result << "'";
			throw Error(msg.str(), __FILE__, __LINE__, __func__);
		}
		static_cast<std::basic_ostream<T>&>(out).rdbuf(orig);
	}
}

template<class T>
void test_kernelbuf_withvector() {

	std::default_random_engine generator;
	std::uniform_int_distribution<T> distribution(std::numeric_limits<T>::min(),std::numeric_limits<T>::max());
	auto dice = std::bind(distribution, generator);

	typedef std::size_t I;
	typedef basic_kernelbuf<std::basic_stringbuf<T>> kernelbuf;
	typedef basic_kstream<std::basic_stringbuf<T>> kstream;

	const I MAX_SIZE_POWER = 12;

	for (I k=1; k<=133; ++k) {
		I size = I(1) << 10;
		std::vector<T> input(size);
		for (size_t j=0; j<size; ++j)
			input[j] = dice();

		kstream str;
		if (str.tellp() != 0) {
			std::stringstream msg;
			msg << "buffer is not empty before write: tellp=";
			msg << str.tellp();
			throw Error(msg.str(), __FILE__, __LINE__, __func__);
		}
		str.write(&input[0], size);
		str.flush();
		std::vector<T> output(size);
		str.read(&output[0], size);
//		if (!buf.empty()) {
//			std::stringstream msg;
//			msg << "buffer is not empty after read: size=";
//			msg << buf.size();
//			throw Error(msg.str(), __FILE__, __LINE__, __func__);
//		}
		for (size_t j=0; j<size; ++j) {
			if (input[j] != output[j]) {
				std::stringstream msg;
				msg << "input and output does not match:\n'"
					<< input[j] << "'\n!=\n'" << output[j] << "'";
				throw Error(msg.str(), __FILE__, __LINE__, __func__);
			}
		}
	}
}

struct App {
	int run(int, char**) {
		try {
			test_buffer<char, Buffer>();
			test_buffer<unsigned char, Buffer>();
			test_buffer<char, LBuffer>();
			test_buffer<unsigned char, LBuffer>();
			test_fdbuf<char, int>();
			test_fdbuf<unsigned char, int>();
			test_fdbuf<char, Socket>();
			test_filterbuf<char>();
			test_filterbuf<unsigned char>();
			test_kernelbuf<char>();
			test_kernelbuf_with_stringstream<char>();
			test_kernelbuf_withvector<char>();
		} catch (std::exception& e) {
			std::cerr << e.what() << std::endl;
			return 1;
		}
		return 0;
	}
};

int main(int argc, char* argv[]) {
	App app;
	return app.run(argc, argv);
}
