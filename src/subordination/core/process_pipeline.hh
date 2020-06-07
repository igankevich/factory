#ifndef SUBORDINATION_CORE_PROCESS_PIPELINE_HH
#define SUBORDINATION_CORE_PROCESS_PIPELINE_HH

#include <memory>
#include <unordered_map>

#include <unistdx/ipc/process>
#include <unistdx/ipc/process_group>

#include <subordination/core/application.hh>
#include <subordination/core/basic_socket_pipeline.hh>
#include <subordination/core/kernel_header.hh>
#include <subordination/core/process_handler.hh>

namespace sbn {

    class process_pipeline: public basic_socket_pipeline {

    private:
        using event_handler_type = process_handler;
        using event_handler_ptr = std::shared_ptr<event_handler_type>;
        using application_id_type = application::id_type;
        using application_table = std::unordered_map<application_id_type,event_handler_ptr>;
        using app_iterator = typename application_table::iterator;

    private:
        application_table _apps;
        sys::process_group _procs;
        /// Allow process execution as superuser/supergroup.
        bool _allowroot = true;

    public:

        process_pipeline() = default;
        ~process_pipeline() = default;
        process_pipeline(const process_pipeline&) = delete;
        process_pipeline& operator=(const process_pipeline&) = delete;
        process_pipeline(process_pipeline&& rhs) = default;
        process_pipeline& operator=(process_pipeline&) = default;

        inline void
        add(const application& app) {
            lock_type lock(this->_mutex);
            this->do_add(app);
        }

        void loop() override;
        void forward(foreign_kernel* hdr) override;

        inline void
        allow_root(bool rhs) noexcept {
            this->_allowroot = rhs;
        }

    protected:

        void
        process_kernels() override;

    private:

        app_iterator
        do_add(const application& app);

        void
        process_kernel(kernel* k);

        void wait_loop();

        inline app_iterator
        find_by_app_id(application_id_type id) {
            return this->_apps.find(id);
        }

        app_iterator
        find_by_process_id(sys::pid_type pid);

        friend class process_handler;

    };

}

#endif // vim:filetype=cpp
