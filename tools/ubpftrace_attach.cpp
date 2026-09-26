#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <algorithm>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>

namespace fs = std::filesystem;

// ANSI Colors for high-fidelity HPC CLI output
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[1;31m"
#define ANSI_GREEN   "\033[1;32m"
#define ANSI_YELLOW  "\033[1;33m"
#define ANSI_CYAN    "\033[1;36m"
#define ANSI_BLUE    "\033[1;34m"

static void print_banner() {
    std::cout << ANSI_CYAN << ANSI_BOLD
              << "╔════════════════════════════════════════════════════════════════════╗\n"
              << "║       ubt-attach: Dynamic Multi-Node Runtime Injector              ║\n"
              << "╚════════════════════════════════════════════════════════════════════╝\n"
              << ANSI_RESET;
}

static void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  -p, --pid <PID>         Target single process ID on the local node\n"
              << "  -c, --comm <NAME>       Target all local processes matching executable name\n"
              << "  -s, --script <PATH>     Compile and attach .bt tracing script into targets\n"
              << "  -j, --job <JOBID>       Fan-out attach across all nodes in Slurm job\n"
              << "  -d, --detach            Send detach signal to unhook probes cleanly\n"
              << "  -a, --agent-so <PATH>   Path to libbpftime-agent.so (auto-discovered if omitted)\n"
              << "  -h, --help              Show this help message\n\n"
              << "Examples:\n"
              << "  # Attach to single PID with .bt script:\n"
              << "  " << prog << " -p 12849 -s /path/to/script.bt\n\n"
              << "  # Cluster-wide attach with .bt script across Slurm job:\n"
              << "  " << prog << " --job 58774223 --comm hpc_app -s /path/to/script.bt\n\n"
              << "  # Safely detach probes from all ranks:\n"
              << "  " << prog << " --job 58774223 --comm hpc_app --detach\n\n";
}

// Find path to libbpftime-agent.so
static std::string discover_agent_so(const std::string &override_path) {
    if (!override_path.empty() && fs::exists(override_path)) {
        return fs::canonical(override_path).string();
    }
    if (const char *env_so = std::getenv("UBPFTRACE_AGENT_SO")) {
        if (fs::exists(env_so)) return fs::canonical(env_so).string();
    }
    // Search relative to self executable
    char self_buf[1024] = {0};
    if (readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1) > 0) {
        fs::path self_dir = fs::path(self_buf).parent_path();
        std::vector<fs::path> candidates = {
            self_dir / "libbpftime-agent.so",
            self_dir / ".." / "bin" / "libbpftime-agent.so",
            self_dir / ".." / "build" / "bpftime" / "runtime" / "agent" / "libbpftime-agent.so",
            self_dir / ".." / ".." / "build" / "bpftime" / "runtime" / "agent" / "libbpftime-agent.so"
        };
        for (const auto &c : candidates) {
            if (fs::exists(c)) return fs::canonical(c).string();
        }
    }
    return "";
}

// Find path to libbpftime-syscall-server.so
static std::string discover_syscall_server_so() {
    if (const char *env_srv = std::getenv("UBPFTRACE_SYSCALL_SERVER_SO")) {
        if (fs::exists(env_srv)) return fs::canonical(env_srv).string();
    }
    char self_buf[1024] = {0};
    if (readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1) > 0) {
        fs::path self_dir = fs::path(self_buf).parent_path();
        std::vector<fs::path> candidates = {
            self_dir / "libbpftime-syscall-server.so",
            self_dir / ".." / "bin" / "libbpftime-syscall-server.so",
            self_dir / ".." / "build" / "bpftime" / "runtime" / "syscall-server" / "libbpftime-syscall-server.so",
            self_dir / ".." / ".." / "build" / "bpftime" / "runtime" / "syscall-server" / "libbpftime-syscall-server.so"
        };
        for (const auto &c : candidates) {
            if (fs::exists(c)) return fs::canonical(c).string();
        }
    }
    return "";
}

// Find path to bpftime injector CLI
static std::string discover_bpftime_cli() {
    if (const char *env_cli = std::getenv("BPFTIME_CLI")) {
        if (fs::exists(env_cli)) return fs::canonical(env_cli).string();
    }
    char self_buf[1024] = {0};
    if (readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1) > 0) {
        fs::path self_dir = fs::path(self_buf).parent_path();
        std::vector<fs::path> candidates = {
            self_dir / "bpftime",
            self_dir / ".." / "bin" / "bpftime",
            self_dir / ".." / "build" / "bpftime" / "tools" / "cli" / "bpftime",
            self_dir / ".." / ".." / "build" / "bpftime" / "tools" / "cli" / "bpftime"
        };
        for (const auto &c : candidates) {
            if (fs::exists(c)) return fs::canonical(c).string();
        }
    }
    return "";
}

// Find path to ubpftrace compiler CLI
static std::string discover_ubpftrace_cli() {
    if (const char *env_cli = std::getenv("UBPFTRACE_CLI")) {
        if (fs::exists(env_cli)) return fs::canonical(env_cli).string();
    }
    char self_buf[1024] = {0};
    if (readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1) > 0) {
        fs::path self_dir = fs::path(self_buf).parent_path();
        std::vector<fs::path> candidates = {
            self_dir / "ubpftrace",
            self_dir / ".." / "bin" / "ubpftrace",
            self_dir / ".." / "build" / "src" / "ubpftrace",
            self_dir / ".." / ".." / "build" / "src" / "ubpftrace"
        };
        for (const auto &c : candidates) {
            if (fs::exists(c)) return fs::canonical(c).string();
        }
    }
    return "";
}

// Find path to bpftimetool manager CLI
static std::string discover_bpftimetool_cli() {
    if (const char *env_cli = std::getenv("BPFTIMETOOL_CLI")) {
        if (fs::exists(env_cli)) return fs::canonical(env_cli).string();
    }
    char self_buf[1024] = {0};
    if (readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1) > 0) {
        fs::path self_dir = fs::path(self_buf).parent_path();
        std::vector<fs::path> candidates = {
            self_dir / "bpftimetool",
            self_dir / ".." / "bin" / "bpftimetool",
            self_dir / ".." / "build" / "bpftime" / "tools" / "bpftimetool" / "bpftimetool",
            self_dir / ".." / ".." / "build" / "bpftime" / "tools" / "bpftimetool" / "bpftimetool"
        };
        for (const auto &c : candidates) {
            if (fs::exists(c)) return fs::canonical(c).string();
        }
    }
    return "";
}

// Discover all local PIDs matching command name (excluding self)
static std::vector<pid_t> find_local_pids_by_comm(const std::string &comm) {
    std::vector<pid_t> pids;
    pid_t self_pid = getpid();
    DIR *dir = opendir("/proc");
    if (!dir) return pids;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        char *endptr = nullptr;
        long pid = strtol(entry->d_name, &endptr, 10);
        if (*endptr != '\0' || pid <= 0 || pid == self_pid) continue;
        if (kill((pid_t)pid, 0) != 0) continue;

        // 1. Check exe link
        char exe_buf[1024] = {0};
        std::string exe_link = "/proc/" + std::to_string(pid) + "/exe";
        ssize_t len = readlink(exe_link.c_str(), exe_buf, sizeof(exe_buf) - 1);
        if (len > 0) {
            fs::path p(exe_buf);
            if (p.filename() == comm) {
                pids.push_back((pid_t)pid);
                continue;
            }
        }

        // 2. Check comm file
        std::string comm_path = "/proc/" + std::to_string(pid) + "/comm";
        std::ifstream comm_file(comm_path);
        if (comm_file.is_open()) {
            std::string line;
            if (std::getline(comm_file, line)) {
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
                    line.pop_back();
                }
                if (line == comm) {
                    pids.push_back((pid_t)pid);
                    continue;
                }
            }
        }

        // 3. Check cmdline file
        std::string cmd_path = "/proc/" + std::to_string(pid) + "/cmdline";
        std::ifstream cmd_file(cmd_path);
        if (cmd_file.is_open()) {
            std::string cmd;
            if (std::getline(cmd_file, cmd, '\0')) {
                fs::path p(cmd);
                if (p.filename() == comm) {
                    pids.push_back((pid_t)pid);
                }
            }
        }
    }
    closedir(dir);
    std::sort(pids.begin(), pids.end());
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    return pids;
}

// Send IPC command to agent Unix Domain Socket
static bool send_agent_ipc_command(pid_t pid, const std::string &command, std::string &response) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;

    sockaddr_un addr {};
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::string name = "bpftime-agent-" + std::to_string(pid);
    addr.sun_path[0] = '\0'; // Linux abstract namespace
    memcpy(addr.sun_path + 1, name.data(), name.size());
    socklen_t len = (socklen_t)(offsetof(sockaddr_un, sun_path) + 1 + name.size());

    if (::connect(fd, (sockaddr *)&addr, len) != 0) {
        ::close(fd);
        return false;
    }

    // Send full request
    const char *buf = command.data();
    size_t left = command.size();
    while (left > 0) {
        ssize_t n = ::send(fd, buf, left, MSG_NOSIGNAL);
        if (n < 0) {
            ::close(fd);
            return false;
        }
        buf += (size_t)n;
        left -= (size_t)n;
    }
    (void)::shutdown(fd, SHUT_WR);

    char rbuf[4096];
    for (;;) {
        ssize_t n = ::recv(fd, rbuf, sizeof(rbuf), 0);
        if (n <= 0) break;
        response.append(rbuf, rbuf + n);
    }
    ::close(fd);
    return true;
}

static bool attach_single_pid(pid_t pid, const std::string &bpftime_cli, const std::string &agent_so) {
    (void)agent_so;
    // 1. Try sending refresh to already injected agent IPC socket
    std::string resp;
    if (send_agent_ipc_command(pid, "refresh ", resp) && resp.find("ok") != std::string::npos) {
        std::cout << "  [" << ANSI_CYAN << "REFRESH" << ANSI_RESET << "] Re-attached & refreshed probes in PID " 
                  << ANSI_YELLOW << pid << ANSI_RESET << "... " << ANSI_GREEN << "SUCCESS" << ANSI_RESET << "\n";
        return true;
    }

    // 2. Perform Frida injection if agent is not yet resident
    std::cout << "  [" << ANSI_CYAN << "INJECT" << ANSI_RESET << "] Injecting "
              << ANSI_BOLD << "libbpftime-agent.so" << ANSI_RESET << " into PID " 
              << ANSI_YELLOW << pid << ANSI_RESET << "... " << std::flush;

    std::string cmd = bpftime_cli + " attach " + std::to_string(pid);
    int ret = std::system(cmd.c_str());
    int exit_code = WIFEXITED(ret) ? WEXITSTATUS(ret) : ret;
    if (exit_code == 0) {
        std::cout << ANSI_GREEN << "SUCCESS" << ANSI_RESET << "\n";
        return true;
    } else {
        std::cout << ANSI_RED << "FAILED (exit code " << exit_code << ")" << ANSI_RESET << "\n";
        return false;
    }
}

// Perform detach from a single PID
static bool detach_single_pid(pid_t pid) {
    std::cout << "  [" << ANSI_YELLOW << "DETACH" << ANSI_RESET << "] Detaching probes from PID " 
              << ANSI_YELLOW << pid << ANSI_RESET << "... " << std::flush;

    std::string resp;
    if (send_agent_ipc_command(pid, "detach", resp)) {
        std::cout << ANSI_GREEN << "SUCCESS" << ANSI_RESET << " (" << resp << ")\n";
        return true;
    } else {
        std::cout << ANSI_RED << "FAILED (Process not responding or already detached)" << ANSI_RESET << "\n";
        return false;
    }
}

int main(int argc, char **argv) {
    pid_t target_pid = 0;
    std::string comm_name;
    std::string script_path;
    std::string job_id;
    std::string target_node;
    std::string agent_so_override;
    bool do_detach = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_banner();
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-w" || arg == "--node" || arg == "--nodelist") {
            if (i + 1 < argc) target_node = argv[++i];
        } else if (arg == "-p" || arg == "--pid") {
            if (i + 1 < argc) target_pid = (pid_t)std::strtol(argv[++i], nullptr, 10);
        } else if (arg == "-c" || arg == "--comm" || arg == "--pidof") {
            if (i + 1 < argc) comm_name = argv[++i];
        } else if (arg == "-s" || arg == "--script") {
            if (i + 1 < argc) script_path = argv[++i];
        } else if (arg == "-j" || arg == "--job" || arg == "--jobid") {
            if (i + 1 < argc) job_id = argv[++i];
        } else if (arg == "-d" || arg == "--detach") {
            do_detach = true;
        } else if (arg == "-a" || arg == "--agent-so") {
            if (i + 1 < argc) agent_so_override = argv[++i];
        }
    }

    if (target_pid == 0 && comm_name.empty()) {
        print_banner();
        std::cerr << ANSI_RED << "Error: Must specify either -p <PID> or --comm <NAME>" << ANSI_RESET << "\n\n";
        print_usage(argv[0]);
        return 1;
    }

    if (!script_path.empty()) {
        if (!fs::exists(script_path)) {
            print_banner();
            std::cerr << ANSI_RED << "Error: Script file does not exist: " << script_path << ANSI_RESET << "\n\n";
            return 1;
        }
        script_path = fs::canonical(script_path).string();
    }

    print_banner();

    // 1. Remote Node / Cluster-wide Fan-out from Login Node
    if (!target_node.empty() || !job_id.empty()) {
        char self_buf[1024] = {0};
        readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1);
        std::string self_bin = self_buf;

        std::stringstream remote_cmd;
        if (!job_id.empty()) {
            std::cout << ANSI_BOLD << ">>> Initiating Remote Orchestration for Slurm Job: " 
                      << ANSI_GREEN << job_id << ANSI_RESET;
            if (!target_node.empty()) {
                std::cout << " (Target Node: " << ANSI_YELLOW << target_node << ANSI_RESET << ")\n";
            } else {
                std::cout << " (All Compute Nodes)\n";
            }

            int node_count = 0;
            if (target_node.empty()) {
                std::string sq_cmd = "squeue -j " + job_id + " -h -o %D 2>/dev/null";
                FILE *fp = popen(sq_cmd.c_str(), "r");
                if (fp) {
                    char buf[64] = {0};
                    if (fgets(buf, sizeof(buf) - 1, fp)) node_count = atoi(buf);
                    pclose(fp);
                }
            }

            remote_cmd << "srun --jobid=" << job_id << " --overlap ";
            if (!target_node.empty()) {
                remote_cmd << "-w " << target_node << " ";
            } else if (node_count > 0) {
                remote_cmd << "-N " << node_count << " ";
            }
            remote_cmd << "--ntasks-per-node=1 " << self_bin;
            if (target_pid > 0) remote_cmd << " -p " << target_pid;
            if (!comm_name.empty()) remote_cmd << " --comm " << comm_name;
            if (!script_path.empty()) remote_cmd << " -s " << script_path;
            if (do_detach) remote_cmd << " --detach";
            if (!agent_so_override.empty()) remote_cmd << " --agent-so " << agent_so_override;
        } else {
            // Direct remote SSH dispatch to specific node
            std::cout << ANSI_BOLD << ">>> Initiating Remote Dispatch to Compute Node: " 
                      << ANSI_YELLOW << target_node << ANSI_RESET << "\n";
            remote_cmd << "ssh " << target_node << " \"" << self_bin;
            if (target_pid > 0) remote_cmd << " -p " << target_pid;
            if (!comm_name.empty()) remote_cmd << " --comm " << comm_name;
            if (!script_path.empty()) remote_cmd << " -s " << script_path;
            if (do_detach) remote_cmd << " --detach";
            if (!agent_so_override.empty()) remote_cmd << " --agent-so " << agent_so_override;
            remote_cmd << "\"";
        }

        std::cout << "  [" << ANSI_BLUE << "REMOTE" << ANSI_RESET << "] Executing: " 
                  << ANSI_CYAN << remote_cmd.str() << ANSI_RESET << "\n\n";

        int ret = std::system(remote_cmd.str().c_str());
        int exit_code = WIFEXITED(ret) ? WEXITSTATUS(ret) : ret;
        if (exit_code == 0) {
            std::cout << "\n" << ANSI_GREEN << ANSI_BOLD
                      << "✔ Remote " << (do_detach ? "detachment" : "injection")
                      << " completed successfully!" << ANSI_RESET << "\n";
            return 0;
        } else {
            std::cerr << "\n" << ANSI_RED << "✖ Remote orchestration encountered errors (exit code " 
                      << exit_code << ")" << ANSI_RESET << "\n";
            return exit_code;
        }
    }

    // 2. Node-Local Execution
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    std::cout << ANSI_BOLD << "Node Context: " << ANSI_CYAN << hostname << ANSI_RESET << "\n";

    std::string bpftime_cli = discover_bpftime_cli();
    std::string ubpftrace_cli = discover_ubpftrace_cli();
    std::string agent_so = discover_agent_so(agent_so_override);

    if (!do_detach) {
        if (bpftime_cli.empty()) {
            std::cerr << ANSI_RED << "Error: Unable to locate 'bpftime' injector CLI binary." << ANSI_RESET << "\n";
            return 1;
        }
        if (!script_path.empty() && ubpftrace_cli.empty()) {
            std::cerr << ANSI_RED << "Error: Unable to locate 'ubpftrace' compiler CLI binary." << ANSI_RESET << "\n";
            return 1;
        }
        if (agent_so.empty()) {
            std::cerr << ANSI_RED << "Error: Unable to locate 'libbpftime-agent.so'." << ANSI_RESET << "\n";
            return 1;
        }
    }

    if (!do_detach && !script_path.empty()) {
        std::string manifest_file = "/dev/shm/ubpf_manifest_" + std::to_string(getpid()) + ".json";
        std::system("rm -f /dev/shm/bpftime_maps_shm* /dev/shm/bpftime_sem_bpftime_maps_shm* /dev/shm/ubpf_manifest_* 2>/dev/null || true");
        usleep(100000); // 100ms

        std::cout << "  [" << ANSI_CYAN << "COMPILE-PROBES" << ANSI_RESET << "] Compiling " 
                  << ANSI_BOLD << fs::path(script_path).filename().string() << ANSI_RESET 
                  << " to eBPF manifest... " << std::flush;

        std::string srv_so = discover_syscall_server_so();
        std::string env_opts = "BPFTIME_GLOBAL_SHM_NAME=bpftime_maps_shm UBPFTRACE_DISABLE_AGENT=1 ";
        if (!srv_so.empty()) {
            env_opts += "LD_PRELOAD=" + srv_so + " UBPFTRACE_SYSCALL_SERVER_SO=" + srv_so + " ";
        }
        if (const char *out = std::getenv("UBPFTRACE_OUTPUT_DIR")) {
            env_opts += "UBPFTRACE_OUTPUT_DIR=" + std::string(out) + " ";
        } else if (fs::exists("/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces")) {
            env_opts += "UBPFTRACE_OUTPUT_DIR=/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces ";
        }
        if (const char *live = std::getenv("UBPFTRACE_LIVE_DIR")) {
            env_opts += "UBPFTRACE_LIVE_DIR=" + std::string(live) + " ";
        }
        env_opts += "UBPFTRACE_EXPORT_MANIFEST=" + manifest_file + " ";

        std::string compile_cmd = env_opts + ubpftrace_cli + " --no-warnings " + script_path + " >/dev/null 2>&1";
        int c_ret = std::system(compile_cmd.c_str());
        if (c_ret == 0 && fs::exists(manifest_file)) {
            std::cout << ANSI_GREEN << "SUCCESS" << ANSI_RESET << "\n";
            std::cout << "  [" << ANSI_CYAN << "ACTIVATE-SHM" << ANSI_RESET << "] Activating eBPF runtime into persistent SHM... " << std::flush;

            std::system("rm -f /dev/shm/bpftime_maps_shm* /dev/shm/bpftime_sem_bpftime_maps_shm* 2>/dev/null || true");
            usleep(50000);

            std::string tool_cli = discover_bpftimetool_cli();
            if (!tool_cli.empty()) {
                std::string import_cmd = tool_cli + " import " + manifest_file + " >/dev/null 2>&1";
                int imp_ret = std::system(import_cmd.c_str());
                if (imp_ret == 0) {
                    std::cout << ANSI_GREEN << "SUCCESS" << ANSI_RESET << "\n";
                } else {
                    std::cout << ANSI_YELLOW << "WARNING (import code " << imp_ret << ")" << ANSI_RESET << "\n";
                }
            } else {
                std::cout << ANSI_RED << "FAILED (bpftimetool not found)" << ANSI_RESET << "\n";
            }
            try { fs::remove(manifest_file); } catch (...) {}
        } else {
            std::cout << ANSI_RED << "FAILED (compilation error " << c_ret << ")" << ANSI_RESET << "\n";
            return 1;
        }
    } else if (do_detach) {
        std::system("pkill -9 -x ubpftrace >/dev/null 2>&1 || true");
        std::system("rm -f /dev/shm/bpftime_maps_shm* /dev/shm/bpftime_sem_bpftime_maps_shm* /dev/shm/ubpf_manifest_* 2>/dev/null || true");
    }

    std::vector<pid_t> pids_to_process;
    if (target_pid > 0) {
        pids_to_process.push_back(target_pid);
    } else {
        pids_to_process = find_local_pids_by_comm(comm_name);
        if (pids_to_process.empty()) {
            std::cout << "  [" << ANSI_YELLOW << "WARN" << ANSI_RESET << "] No local processes matching '" 
                      << comm_name << "' found on " << hostname << ".\n";
            return 0;
        }
        std::cout << "  [" << ANSI_GREEN << "DISCOVER" << ANSI_RESET << "] Found " 
                  << pids_to_process.size() << " local processes matching '" << comm_name << "': [";
        for (size_t i = 0; i < pids_to_process.size(); i++) {
            std::cout << pids_to_process[i] << (i + 1 < pids_to_process.size() ? ", " : "");
        }
        std::cout << "]\n";
    }

    int success_count = 0;
    for (size_t i = 0; i < pids_to_process.size(); i++) {
        pid_t pid = pids_to_process[i];
        if (i > 0) {
            // Allow previous agent SHM initialization to complete
            usleep(200000); // 200ms
        }
        bool ok = do_detach ? detach_single_pid(pid)
                            : attach_single_pid(pid, bpftime_cli, agent_so);
        if (ok) {
            success_count++;
        } else if (!do_detach) {
            // Retry once after 300ms
            usleep(300000);
            if (attach_single_pid(pid, bpftime_cli, agent_so)) {
                success_count++;
            }
        }
    }

    std::cout << "\n" << ANSI_BOLD << "Summary: " 
              << (do_detach ? "Detached from " : "Injected into ")
              << success_count << "/" << pids_to_process.size() << " processes successfully on "
              << hostname << "." << ANSI_RESET << "\n";

    return (success_count == (int)pids_to_process.size()) ? 0 : 1;
}
