#include "hierarchy_kernel.hh"

void
sbn::hierarchy_kernel::write(sys::pstream& out) const {
    sbn::kernel::write(out);
    out << this->_ifaddr << this->_weight;
}

void
sbn::hierarchy_kernel::read(sys::pstream& in) {
    sbn::kernel::read(in);
    in >> this->_ifaddr >> this->_weight;
}

