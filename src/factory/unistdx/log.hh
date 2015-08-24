#include <syslog.h>

namespace factory {

	namespace unix {

		template<class Ch, class Tr=std::char_traits<Ch>>
		struct basic_logbuf: public std::basic_streambuf<Ch,Tr> {

			using typename std::basic_streambuf<Ch,Tr>::int_type;
			using typename std::basic_streambuf<Ch,Tr>::traits_type;
			using typename std::basic_streambuf<Ch,Tr>::char_type;
			using typename std::basic_streambuf<Ch,Tr>::pos_type;
			using typename std::basic_streambuf<Ch,Tr>::off_type;
			typedef stdx::spin_mutex mutex_type;
			typedef std::lock_guard<mutex_type> lock_type;
			typedef std::basic_string<Ch,Tr> buf_type;
			typedef std::basic_ostream<Ch,Tr>& stream_type;

			explicit
			basic_logbuf(std::basic_ostream<Ch,Tr>& s):
			_stream(s), _bufs() {}

			int_type
			overflow(int_type c = traits_type::eof()) override {
				lock_type lock(_mutex);
				this_thread_buf().push_back(c);
				return c;
			}

			std::streamsize
			xsputn(const char_type* s, std::streamsize n) override {
				lock_type lock(_mutex);
				this_thread_buf().append(s, n);
				return n;
			}

			int
			sync() override {
				lock_type lock(_mutex);
				write_log();
				return 0;
			}

		private:

			void
			write_log() {
				buf_type& buf = this_thread_buf();
				if (!buf.empty()) {
					write_syslog(buf.c_str());
					buf.clear();
				}
			}

			void
			write_syslog(const char_type* msg) {
				::syslog(LOG_INFO, "%s", msg);
			}

			buf_type&
			this_thread_buf() {
				return _bufs[std::this_thread::get_id()];
			}

			mutex_type _mutex;
			stream_type& _stream;
			std::unordered_map<std::thread::id, buf_type> _bufs;
		};

		struct Install_syslog {

			explicit
			Install_syslog(std::ostream& s):
			str(s), newbuf(s), oldbuf(str.rdbuf(&newbuf))
			{}

			~Install_syslog() {
				str.rdbuf(oldbuf);
			}

		private:
			std::ostream& str;
			basic_logbuf<char> newbuf;
			std::streambuf* oldbuf = nullptr;
		};

		Install_syslog __syslog1(std::clog);

	}

}