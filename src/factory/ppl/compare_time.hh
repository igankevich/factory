#ifndef FACTORY_PPL_COMPARE_TIME_HH
#define FACTORY_PPL_COMPARE_TIME_HH

namespace asc {

	template<class T>
	struct Compare_time {
		inline bool
		operator()(const T* lhs, const T* rhs) const noexcept {
			return lhs->at() > rhs->at();
		}
	};

}


#endif // vim:filetype=cpp
