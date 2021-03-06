#ifndef SUBORDINATION_DAEMON_UNIX_SOCKET_PIPELINE_HH
#define SUBORDINATION_DAEMON_UNIX_SOCKET_PIPELINE_HH

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

#include <unistdx/net/socket>
#include <unistdx/net/socket_address>

#include <subordination/core/basic_socket_pipeline.hh>
#include <subordination/core/types.hh>

namespace sbnd {

    class unix_socket_client: public sbn::connection {

    private:
        sys::socket _socket;

    public:

        explicit unix_socket_client(sys::socket&& socket);

        void handle(const sys::epoll_event& event) override;
        void flush() override;

        inline sys::fd_type fd() const noexcept { return this->_socket.fd(); }
        inline const sys::socket& socket() const noexcept { return this->_socket; }

    protected:
        void receive_kernel(sbn::kernel_ptr&& k) override;

    };

    class unix_socket_pipeline: public sbn::basic_socket_pipeline {

    public:
        struct properties: public sbn::basic_socket_pipeline::properties {
            inline properties():
            properties{sys::this_process::cpus(), sys::page_size()} {}

            inline explicit
            properties(const sys::cpu_set& cpus, size_t page_size, size_t multiple=16):
            sbn::basic_socket_pipeline::properties{cpus, page_size, multiple} {}
        };

    public:
        using client_type = unix_socket_client;
        using client_ptr = std::shared_ptr<client_type>;
        using client_table = std::unordered_map<sys::socket_address,client_ptr>;

    private:
        client_table _clients;

    public:
        explicit unix_socket_pipeline(const properties& p);
        unix_socket_pipeline() = default;
        ~unix_socket_pipeline() = default;
        unix_socket_pipeline(const unix_socket_pipeline& rhs) = delete;
        unix_socket_pipeline(unix_socket_pipeline&& rhs) = delete;

        void add_server(const sys::unix_socket_address& rhs);
        void add_client(const sys::unix_socket_address& addr);
        void forward(sbn::kernel_ptr&& hdr) override;

    private:

        void add_client(const sys::unix_socket_address& addr, sys::socket&& sock);

        void process_kernels() override;
        void process_kernel(sbn::kernel_ptr&& k);

        friend class unix_socket_server;
        friend class unix_socket_client;

    };

}

#endif // vim:filetype=cpp
