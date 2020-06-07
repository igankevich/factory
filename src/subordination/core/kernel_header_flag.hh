#ifndef SUBORDINATION_CORE_KERNEL_HEADER_FLAG_HH
#define SUBORDINATION_CORE_KERNEL_HEADER_FLAG_HH

#include <unistdx/base/types>

#include <subordination/core/macros.hh>

namespace sbn {

    enum class kernel_field: sys::u8 {
        source = 1<<0,
        destination = 1<<1,
        application = 1<<2,
    };

    SBN_FLAGS(kernel_field)

}

#endif // vim:filetype=cpp
