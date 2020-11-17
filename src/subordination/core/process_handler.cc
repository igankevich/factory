#include <ostream>

#include <unistdx/base/make_object>

#include <subordination/core/basic_factory.hh>
#include <subordination/core/process_handler.hh>

/// Called from parent process.
sbn::process_handler::process_handler(sys::pid_type&& child,
                                      sys::two_way_pipe&& pipe,
                                      const ::sbn::application& app):
_child_process_id(child),
_file_descriptors(std::move(pipe)),
_application(app),
_role(role_type::parent) {
    this->_file_descriptors.in().validate();
    this->_file_descriptors.out().validate();
}

/// Called from child process.
sbn::process_handler::process_handler(sys::pipe&& pipe):
_child_process_id(sys::this_process::id()),
_file_descriptors(std::move(pipe)),
_role(role_type::child) {}

void sbn::process_handler::handle(const sys::epoll_event& event) {
    if (state() == connection_state::starting) {
        state(connection_state::started);
    }
    if (event.in()) {
        fill(this->_file_descriptors.in());
        receive_kernels(this->_role == role_type::parent ?  &this->_application : nullptr,
                        [this] (kernel_ptr&) { ++this->_kernel_count; });
    }
}

void sbn::process_handler::receive_foreign_kernel(foreign_kernel_ptr&& fk) {
    if (fk->type_id() == 1) {
        log("RECV _", *fk);
        //fk->return_to_parent();
        //fk->principal_id(0);
        //fk->parent_id(0);
        //connection::forward(std::move(fk));
        fk->target_application_id(1);
        parent()->forward_to(this->_unix, std::move(fk));
        //parent()->remove(application().id());
        sys::process_view(child_process_id()).send(sys::signal::terminate);
    } else {
        connection::receive_foreign_kernel(std::move(fk));
    }
}

void sbn::process_handler::add(const connection_ptr& self) {
    connection::parent()->emplace_handler(sys::epoll_event(in(), sys::event::in), self);
    connection::parent()->emplace_handler(sys::epoll_event(out(), sys::event::out), self);
}

void sbn::process_handler::remove(const connection_ptr&) {
    try {
        connection::parent()->erase(in());
    } catch (const sys::bad_call& err) {
        if (err.errc() != std::errc::no_such_file_or_directory) { throw; }
    }
    try {
        connection::parent()->erase(out());
    } catch (const sys::bad_call& err) {
        if (err.errc() != std::errc::no_such_file_or_directory) { throw; }
    }
    state(connection_state::stopped);
}

void sbn::process_handler::flush() {
    connection::flush(this->_file_descriptors.out());
}

void sbn::process_handler::stop() {
}
