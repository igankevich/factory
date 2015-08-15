namespace factory {
	
	namespace bits {

		template<class T>
		std::string
		to_string(T rhs) {
			std::stringstream s;
			s << rhs;
			return s.str();
		}

		struct To_string {

			template<class T>
			inline
			To_string(T rhs):
				_s(to_string(rhs)) {}

			const char*
			c_str() const noexcept {
				return _s.c_str();
			}

		private:
			std::string _s;
		};

		typedef struct ::sigaction sigaction_type;

		struct Action: public sigaction_type {
			inline
			Action(void (*func)(int)) noexcept {
				this->sa_handler = func;
			}
		};
	
	}

}