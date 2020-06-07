#include <subordination/core/basic_factory.hh>

#include <unistdx/util/system>

sbn::Factory::Factory():
#if defined(SUBORDINATION_PROFILE_NODE_DISCOVERY)
_local(1) {
#else
_local(sys::thread_concurrency()) {
#endif
    this->_local.name("upstrm");
    this->_local.error_pipeline(&this->_remote);
    this->_remote.name("chld");
    this->_remote.native_pipeline(&this->_local);
    this->_remote.foreign_pipeline(&this->_remote);
    this->_remote.remote_pipeline(&this->_remote);
    this->_remote.types(&this->_types);
    this->_remote.instances(&this->_instances);
    this->_remote.add_connection();
}

void sbn::Factory::start() {
    this->_local.start();
    this->_remote.start();
}

void sbn::Factory::stop() {
    this->_local.stop();
    this->_remote.stop();
}

void sbn::Factory::wait() {
    this->_local.wait();
    this->_remote.wait();
}

void sbn::Factory::clear() {
    this->_local.clear();
    this->_remote.clear();
}

sbn::Factory sbn::factory;
