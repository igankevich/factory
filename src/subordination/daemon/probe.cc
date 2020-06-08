#include <subordination/core/kernel_buffer.hh>
#include <subordination/daemon/probe.hh>

void
sbnd::probe::write(sbn::kernel_buffer& out) const {
    sbn::kernel::write(out);
    out << this->_ifaddr << this->_oldprinc << this->_newprinc;
}

void
sbnd::probe::read(sbn::kernel_buffer& in) {
    sbn::kernel::read(in);
    in >> this->_ifaddr >> this->_oldprinc >> this->_newprinc;
}
