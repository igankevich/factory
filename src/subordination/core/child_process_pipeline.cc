#include <unistdx/io/pipe>

#include <subordination/core/application.hh>
#include <subordination/core/child_process_pipeline.hh>

void sbn::child_process_pipeline::send(kernel* k) {
    #if defined(SBN_DEBUG)
    this->log("send _", *k);
    #endif
    lock_type lock(this->_mutex);
    if (!this->_parent) {
        lock.unlock();
        native_pipeline()->send(k);
    } else {
        this->_kernels.emplace(k);
        this->poller().notify_one();
    }
}

void sbn::child_process_pipeline::add_connection() {
    using f = connection_flags;
    sys::fd_type in = this_application::get_input_fd();
    sys::fd_type out = this_application::get_output_fd();
    if (in != -1 && out != -1) {
        this->_parent = std::make_shared<process_handler>(sys::pipe(in, out));
        this->_parent->parent(this);
        this->_parent->types(types());
        this->_parent->setf(f::save_upstream_kernels);
        this->_parent->state(connection_state::starting);
        this->_parent->name(name());
        this->_parent->add(this->_parent);
    }
}

void sbn::child_process_pipeline::process_kernels() {
    while (!this->_kernels.empty()) {
        auto* k = this->_kernels.front();
        this->_kernels.pop();
        if (this->_parent && (this->_parent->state() == connection_state::started ||
                              this->_parent->state() == connection_state::starting)) {
            this->_parent->send(k);
        } else {
            native_pipeline()->send(k);
        }
    }
}
