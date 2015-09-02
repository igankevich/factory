#ifndef FACTORY_CFG_APP_HH
#define FACTORY_CFG_APP_HH

#include <factory/server/cpu_server.hh>
#include <factory/server/timer_server.hh>
#include <factory/server/app_server.hh>

namespace factory {

	inline namespace app_config {

		struct config {
			typedef components::Managed_object<components::Server<config>> server;
			typedef components::Principal<config> kernel;
			typedef components::CPU_server<config> local_server;
			typedef components::Principal_server<config> remote_server;
			typedef components::Timer_server<config> timer_server;
			typedef components::No_server<config> app_server;
			typedef components::No_server<config> principal_server;
			typedef components::No_server<config> external_server;
			typedef components::Basic_factory<config> factory;
		};

		typedef config::kernel Kernel;
		typedef config::server Server;
	}

}

#endif // FACTORY_CFG_APP_HH