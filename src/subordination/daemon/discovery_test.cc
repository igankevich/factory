#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <regex>
#include <string>
#include <thread>

#include <unistdx/base/byte_buffer>
#include <unistdx/base/log_message>
#include <unistdx/fs/path>
#include <unistdx/io/pipe>
#include <unistdx/io/poller>
#include <unistdx/ipc/argstream>
#include <unistdx/ipc/execute>
#include <unistdx/ipc/process>
#include <unistdx/net/veth_interface>

#include <gtest/gtest.h>

#include <subordination/daemon/discovery_test.hh>
#include <subordination/daemon/test_application.hh>

#include <dtest/application.hh>

template <class ... Args>
inline void
log(const Args& ... args) {
    sys::log_message("tst", args...);
}

std::string expected_failure;

void
expect_event_sequence(
    const std::vector<std::string>& lines,
    const std::vector<std::string>& regex_strings
) {
    std::vector<std::regex> expressions;
    for (const auto& s : regex_strings) { expressions.emplace_back(s); }
    auto first = expressions.begin();
    auto last = expressions.end();
    auto first2 = lines.begin();
    auto last2 = lines.end();
    while (first != last && first2 != last2) {
        if (std::regex_match(*first2, *first)) { ++first; }
        ++first2;
    }
    if (first != last) {
        std::stringstream msg;
        msg << "unmatched expressions: \n";
        size_t offset = first - expressions.begin();
        std::copy(
            regex_strings.begin() + offset,
            regex_strings.end(),
            std::ostream_iterator<std::string>(msg, "\n")
        );
        throw std::runtime_error(msg.str());
    }
}

void
expect_event(const std::vector<std::string>& lines, const std::string& regex_string) {
    expect_event_sequence(lines, {regex_string});
}

void
expect_event_count(
    const std::vector<std::string>& lines,
    const std::string& regex_string,
    size_t expected_count
) {
    std::regex expr(regex_string);
    size_t count = 0;
    for (const auto& line : lines) {
        if (std::regex_match(line, expr)) { ++count; }
    }
    if (count != expected_count) {
        std::stringstream msg;
        msg << "bad event count: expected=" << expected_count << ",actual=" << count;
        throw std::runtime_error(msg.str());
    }
}

void test_superior(const std::vector<std::string>& lines, int fanout, int n) {
    if (fanout >= n || n == 2) {
        if (n == 2) {
            expect_event_sequence(lines, {
                R"(^x1.*add interface address 10\.0\.0\.1.*$)",
                R"(^x1.*add subordinate 10\.0\.0\.2.*$)",
            });
            expect_event(lines, R"(^x1.*set 10\.0\.0\.2.*weight to 1$)");
        } else {
            expect_event(lines, R"(^x1.*add interface address 10\.0\.0\.1.*$)");
            for (int i=2; i<=n; ++i) {
                std::stringstream re;
                re << R"(^x1.*add subordinate 10\.0\.0\.)" << i << ".*$";
                expect_event(lines, re.str());
            }
            for (int i=2; i<=n; ++i) {
                std::stringstream re;
                re << R"(^x1.*set 10\.0\.0\.)" << i << ".*weight to 1$";
                expect_event(lines, re.str());
            }
        }
    }
}

void test_subordinates(const std::vector<std::string>& lines, int fanout, int n) {
    if (fanout >= n || n == 2) {
        if (n == 2) {
            expect_event_sequence(lines, {
                R"(^x2.*add interface address 10\.0\.0\.2.*$)",
                R"(^x2.*set principal to 10\.0\.0\.1.*$)"
            });
            expect_event(lines, R"(^x2.*set 10\.0\.0\.1.*weight to 1$)");
        } else {
            for (int i=2; i<=n; ++i) {
                std::stringstream re;
                re << "^x" << i << R"(.*add interface address 10\.0\.0\.)" << i << ".*$";
                expect_event(lines, re.str());
                re.str("");
                re << "^x" << i << R"(.*set principal to 10\.0\.0\.1.*$)";
                expect_event(lines,	re.str());
                re.str("");
                re << "^x" << i << R"(.*set 10\.0\.0\.1.*weight to )" << n-1 << "$";
                expect_event(lines, re.str());
            }
        }
    }
}

void test_hierarchy(const std::vector<std::string>& lines, int fanout, int n) {
    if (fanout != 2 || n == 2) { return; }
    expect_event(lines, R"(^x1.*add interface address 10\.0\.0\.1.*$)");
    if (n == 4) {
        expect_event(lines, R"(^x1.*add subordinate 10\.0\.0\.2.*$)");
        expect_event(lines, R"(^x1.*add subordinate 10\.0\.0\.3.*$)");
        expect_event(lines, R"(^x2.*add subordinate 10\.0\.0\.4.*$)");
    } else if (n == 8) {
        expect_event(lines, R"(^x1.*add subordinate 10\.0\.0\.2.*$)");
        expect_event(lines, R"(^x1.*add subordinate 10\.0\.0\.3.*$)");
        expect_event(lines, R"(^x2.*add subordinate 10\.0\.0\.4.*$)");
        expect_event(lines, R"(^x2.*add subordinate 10\.0\.0\.5.*$)");
        expect_event(lines, R"(^x3.*add subordinate 10\.0\.0\.6.*$)");
        expect_event(lines, R"(^x3.*add subordinate 10\.0\.0\.7.*$)");
        expect_event(lines, R"(^x4.*add subordinate 10\.0\.0\.8.*$)");
    } else {
        std::stringstream msg;
        msg << "bad number of nodes: " << n;
        throw std::invalid_argument(msg.str());
    }
}

void test_weights(const std::vector<std::string>& lines, int fanout, int n) {
    if (fanout != 2 || n == 2) { return; }
    expect_event(lines, R"(^x1.*add interface address 10\.0\.0\.1.*$)");
    if (n == 4) {
        // upstream
        expect_event(lines, R"(^x1.*set 10\.0\.0\.2.*weight to 2$)");
        expect_event(lines, R"(^x1.*set 10\.0\.0\.3.*weight to 1$)");
        expect_event(lines, R"(^x2.*set 10\.0\.0\.4.*weight to 1$)");
        // downstream
        expect_event(lines, R"(^x2.*set 10\.0\.0\.1.*weight to 2$)");
        expect_event(lines, R"(^x3.*set 10\.0\.0\.1.*weight to 3$)");
        expect_event(lines, R"(^x4.*set 10\.0\.0\.2.*weight to 3$)");
    } else if (n == 8) {
        // upstream
        expect_event(lines, R"(^x1.*set 10\.0\.0\.2.*weight to 4$)");
        expect_event(lines, R"(^x1.*set 10\.0\.0\.3.*weight to 3$)");
        expect_event(lines, R"(^x2.*set 10\.0\.0\.4.*weight to 2$)");
        expect_event(lines, R"(^x2.*set 10\.0\.0\.5.*weight to 1$)");
        expect_event(lines, R"(^x3.*set 10\.0\.0\.6.*weight to 1$)");
        expect_event(lines, R"(^x3.*set 10\.0\.0\.7.*weight to 1$)");
        expect_event(lines, R"(^x4.*set 10\.0\.0\.8.*weight to 1$)");
        // downstream
        expect_event(lines, R"(^x2.*set 10\.0\.0\.1.*weight to 4$)");
        expect_event(lines, R"(^x3.*set 10\.0\.0\.1.*weight to 5$)");
        expect_event(lines, R"(^x4.*set 10\.0\.0\.2.*weight to 6$)");
        expect_event(lines, R"(^x5.*set 10\.0\.0\.2.*weight to 7$)");
        expect_event(lines, R"(^x6.*set 10\.0\.0\.3.*weight to 7$)");
        expect_event(lines, R"(^x7.*set 10\.0\.0\.3.*weight to 7$)");
        expect_event(lines, R"(^x8.*set 10\.0\.0\.4.*weight to 7$)");
    } else {
        std::stringstream msg;
        msg << "bad number of nodes: " << n;
        throw std::invalid_argument(msg.str());
    }
}

void test_application(const std::vector<std::string>& lines, int fanout, int n) {
    int node_no = 0;
    if (expected_failure == "no-failure") {
        node_no = 1;
    } else if (expected_failure == "superior-failure" && n == 2) {
        node_no = 2;
    }
    if (node_no == 0) {
        //expect_event(lines, R"(^x.*app exited:.*status=exited,exit_code=0.*$)");
        expect_event(lines, R"(^x.*job .* terminated with status .*status=exited,exit_code=0$)");
    } else {
        node_no = (expected_failure == "superior-failure") ? 2 : 1;
        std::stringstream re;
        //re << "^x" << node_no << R"(.*app exited:.*status=exited,exit_code=0.*$)";
        re << "^x" << node_no << R"(.*job .* terminated with status .*status=exited,exit_code=0$)";
        expect_event(lines, re.str());
    }
}

sys::argstream sbnd_args() {
    sys::argstream args;
    args.append(SBND_PATH);
    args.append("fanout=2");
    args.append("allow-root=1");
    // tolerate asynchronous start of daemons
    args.append("connection-timeout=1s");
    args.append("max-connection-attempts=10");
    args.append("network-scan-interval=5s");
    // never update NICs
    args.append("network-interface-update-interval=1h");
    return args;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::invalid_argument("usage: discovery-test <failure>");
    }
    auto sbn_failure = argv[1];
    expected_failure = sbn_failure;
    dts::application app;
    dts::cluster cluster;
    cluster.name("x");
    cluster.network({{10,1,0,1},16});
    cluster.peer_network({{10,0,0,1},16});
    cluster.generate_nodes(2);
    app.exit_code(dts::exit_code::all);
    //app.execution_delay(std::chrono::seconds(1));
    app.cluster(std::move(cluster));
    {
        // run sbnd on each cluster node
        const auto num_nodes = app.cluster().size();
        for (size_t i=0; i<num_nodes; ++i) {
            auto args = sbnd_args();
            std::stringstream tmp;
            tmp << app.cluster().name() << (i+1);
            const auto& transactions_directory = tmp.str();
            std::remove(sys::path(transactions_directory, "transactions").data());
            args << "transactions-directory=" << transactions_directory << '\0';
            app.add_process(i, std::move(args));
        }
    }
    app.emplace_test(
        "Wait for superior node to find subordinate nodes.",
        [] (dts::application& app, const dts::application::line_array& lines) {
            test_superior(lines, 2, 2);
        });
    app.emplace_test(
        "Wait for subordinate nodes to find superior node.",
        [] (dts::application& app, const dts::application::line_array& lines) {
            test_subordinates(lines, 2, 2);
        });
    app.emplace_test(
        "Check node hierarchy.",
        [] (dts::application& app, const dts::application::line_array& lines) {
            test_hierarchy(lines, 2, 2);
        });
    app.emplace_test(
        "Check node weights.",
        [&] (dts::application& app, const dts::application::line_array& lines) {
            test_weights(lines, 2, 2);
        });
    app.emplace_test(
        "Run test application.",
        [&] (dts::application& app, const dts::application::line_array& lines) {
            sys::argstream args;
            args.append(SBNC_PATH);
            args.append(APP_PATH);
            args.append(sbn_failure);
            // submit test application from the first node
            app.run_process(dts::cluster_node_bitmap(app.cluster().size(), {0}),
                            std::move(args));
        });
    app.emplace_test(
        "Check that at least one subordinate kernel is executed on each node.",
        [] (dts::application& app, const dts::application::line_array& lines) {
            expect_event(lines, R"(^x1.*app.*subordinate act.*$)");
            expect_event(lines, R"(^x2.*app.*subordinate act.*$)");
        });
    if (expected_failure != "no-failure") {
        app.emplace_test(
            "Simulate failure",
            [] (dts::application& app, const dts::application::line_array& lines) {
                if (expected_failure == "superior-failure") {
                    app.kill_process(0, sys::signal::kill);
                }
                if (expected_failure == "subordinate-failure") {
                    app.kill_process(1, sys::signal::kill);
                }
                if (expected_failure == "power-failure") {
                    app.kill_process(0, sys::signal::kill);
                    app.kill_process(1, sys::signal::kill);
                }
            });
    }
    if (expected_failure == "power-failure") {
        app.emplace_test(
            "Restart sbnd.",
            [] (dts::application& app, const dts::application::line_array& lines) {
                const auto num_nodes = app.cluster().size();
                for (size_t i=0; i<num_nodes; ++i) {
                    auto args = sbnd_args();
                    args << "transactions-directory=" << app.cluster().name() << (i+1) << '\0';
                    app.run_process(i, std::move(args));
                }
            });
    }
    app.emplace_test(
        "Check that test application finished successfully.",
        [] (dts::application& app, const dts::application::line_array& lines) {
            test_application(lines, 2, 2);
        });
    return dts::run(app);
}
