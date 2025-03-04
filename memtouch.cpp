#include <argparse.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <assert.h>
#include <signal.h>
#include <sys/mman.h>

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.0.0"
#endif

static constexpr uint64_t PAGE_SIZE (4096);
static constexpr int      PATTERN   (0xff);

static constexpr int DEFAULT_STAT_IVAL (1000);

using namespace std;

struct Statistics
{
    Statistics() = default;

    Statistics(Statistics&& o) {
        write_rate.store(o.write_rate.load());
        read_rate.store(o.read_rate.load());
    }

    std::atomic<float> read_rate  {0};
    std::atomic<float> write_rate {0};
};

class WorkerThread
{
public:
    WorkerThread(unsigned id_, bool run_once_, unsigned mem_size_mib_,
                 unsigned rw_ratio_, uint64_t page_log_ival_)
        : id(id_)
        , run_once(run_once_)
        , mem_size_mib(mem_size_mib_)
        , rw_ratio(rw_ratio_)
        , page_log_ival(page_log_ival_)
        , stats()
    {
    }

    bool pre_run()
    {
        if (not allocate_memory()) {
            printf("Worker %d: Unable to allocate memory\n", id);
            return false;
        }

        return true;
    }

    void run()
    {
        assert(mem_base != nullptr);

        uint64_t num_pages {(uint64_t(mem_size_mib) * 1024 * 1024) / PAGE_SIZE};

        // Warmup, write every page
        for (uint64_t page {0}; page < num_pages; ++page) {
            if (terminate) {
                break;
            }
            write_page(page);
        }

        if (run_once) {
            kill();
        }

        while(not terminate) {
            run_loop(num_pages);
        }

        cleanup_memory();
    }

    int64_t measure_time_ns(std::function<void()> func) {
        const auto time_start {chrono::system_clock::now()};
        func();
        const auto time_end {chrono::system_clock::now()};
        const auto time_needed {chrono::duration_cast<chrono::nanoseconds>(time_end - time_start)};
        auto ns = time_needed.count();

        // To prevent divide by zero errors (causing float infinity), we always
        // report a non-null number. This is only the case for very small sample
        // sizes of a few pages.
        if (ns == 0) {
            ns = 1;
        }
        return ns;
    }

    void run_loop(uint64_t num_pages)
    {
        uint64_t total_pages_to_read  {num_pages};
        uint64_t total_pages_to_write {0};

        uint64_t pages_read  {0};
        uint64_t pages_written {0};

        if (rw_ratio) {
            total_pages_to_write = (num_pages * rw_ratio) / 100;
            total_pages_to_read  = num_pages - total_pages_to_write;
        }

        // Ensure that `page_log_ival` is not more than `pages_to_*`
        auto pages_to_read_per_iter = std::min(page_log_ival, total_pages_to_read);
        auto pages_to_write_per_iter = std::min(page_log_ival, total_pages_to_write);

        // We touch all pages but we report the progress every %n pages
        while ( (pages_read + pages_written) < num_pages) {
            auto remaining_pages_to_read = total_pages_to_read - pages_read;
            auto remaining_pages_to_write = total_pages_to_write - pages_written;

            // Effective pages to read in this iteration
            auto pages_to_read_eff = std::min(pages_to_read_per_iter, remaining_pages_to_read);
            // Effective pages to write in this iteration
            auto pages_to_write_eff = std::min(pages_to_write_per_iter, remaining_pages_to_write);

            auto time_read_ns = measure_time_ns([&]() {
                for (uint64_t n {0}; n < pages_to_read_eff; ++n) {
                    auto page = n + pages_read + pages_written;
                    read_page(page, &read_buffer[0]);
                }
            });
            pages_read += pages_to_read_eff;

            auto time_write_ns = measure_time_ns([&]() {
                for (uint64_t n {0}; n < pages_to_write_eff; ++n) {
                    auto page = n + pages_read + pages_written;
                    write_page(page);
                }
            });
            pages_written += pages_to_write_eff;

            /* if we had reads */
            if (rw_ratio < 100 and pages_to_read_eff > 0) {
                auto bytes = static_cast<float>(pages_to_read_eff * PAGE_SIZE);
                auto mebi_bytes = bytes / 1024.0f / 1024.0f;
                auto seconds = static_cast<float>(time_read_ns) / 1000000000.0f;
                stats.read_rate = mebi_bytes / seconds;
            }

            /* if we had writes */
            if (rw_ratio > 0 and pages_to_write_eff > 0) {
                auto bytes = static_cast<float>(pages_to_write_eff * PAGE_SIZE);
                auto mebi_bytes = bytes / 1024.0f / 1024.0f;
                auto seconds = static_cast<float>(time_write_ns) / 1000000000.0f;
                stats.write_rate = mebi_bytes / seconds;
            }
        }
    }

    void write_page(uint64_t page)
    {
        memset(static_cast<char*>(mem_base) + page * PAGE_SIZE, PATTERN, PAGE_SIZE);
    }

    void read_page(uint64_t page, void* buffer)
    {
        memcpy(buffer, static_cast<char*>(mem_base) + page * PAGE_SIZE, PAGE_SIZE);
    }

    void cleanup_memory()
    {
        if (munmap(mem_base, uint64_t(mem_size_mib) * 1024 * 1024) != 0) {
            printf("Unable to unmap memory\n");
        }
    }

    bool allocate_memory()
    {
        mem_base = mmap(NULL, uint64_t(mem_size_mib) * 1024 * 1024,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1, 0);

        return mem_base != MAP_FAILED;
    }

    void kill()
    {
        terminate = true;
    }

    float write_rate() const
    {
        return stats.write_rate.load();
    }

    float read_rate() const
    {
        return stats.read_rate.load();
    }

private:
    unsigned id;
    bool run_once;
    unsigned mem_size_mib;
    unsigned rw_ratio;
    uint64_t page_log_ival;

    bool terminate {false};

    void* mem_base {nullptr};

    char read_buffer[PAGE_SIZE];

    Statistics stats;
};

class StatisticsThread
{
public:
    StatisticsThread(vector<WorkerThread>& workers_)
        : workers(workers_)
    {
    }

    ~StatisticsThread()
    {
        if (logging_enabled) {
            log_file.close();
        }
    }

    void run()
    {
        while (not terminate) {
            float read_rate  {0};
            float write_rate {0};

            for (auto& worker : workers) {
                read_rate  += worker.read_rate();
                write_rate += worker.write_rate();
            }

            if (logging_enabled) {
                log_file << setprecision(2) << setfill('0')
                         << get_iso8601_time() << " read_mibps:"
                         << fixed << read_rate << " write_mibps:"
                         << fixed << write_rate << "\n";
                log_file.flush();
            }

            usleep(uint64_t(logging_ival_ms) * 1000);
        }
    }

    void kill()
    {
        terminate = true;
    }

    void set_interval(unsigned ival_ms)
    {
        logging_ival_ms = ival_ms;
    }

    string get_iso8601_time()
    {
        const auto now         {chrono::system_clock::now()};
        const auto now_as_time {chrono::system_clock::to_time_t(now)};
        const auto now_ms      {chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000};

        std::stringstream s;

        s << std::put_time(std::localtime(&now_as_time), "%FT%T")
          << '.' << std::setfill('0') << std::setw(3) << now_ms.count()
          << put_time(localtime(&now_as_time), "%z");

        return s.str();
    }

    bool set_log_file(string file_path)
    {
        log_file.open(file_path);

        if (not log_file.is_open()) {
            return false;
        }

        logging_enabled = true;

        return true;
    }

private:
    vector<WorkerThread>& workers;

    bool terminate {false};
    unsigned logging_ival_ms {DEFAULT_STAT_IVAL};

    ofstream log_file {};
    bool logging_enabled {false};
};

vector<WorkerThread> worker_storage;
vector<unique_ptr<thread>> thread_storage;
StatisticsThread stat_thread(worker_storage);

void sigint_handler([[maybe_unused]] int s)
{
    printf("Terminating...\n");
    for (auto& worker : worker_storage) {
        worker.kill();
    }

    stat_thread.kill();
}

void setup_signals()
{
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = sigint_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
}

void setup_argparse(argparse::ArgumentParser& program, int argc, char** argv)
{
    program.add_argument("--thread_mem")
        .required()
        .help("amount of memory a thread touches in MiB")
        .scan<'u', unsigned>();

    program.add_argument("--num_threads")
        .required()
        .help("number of worker threads")
        .scan<'u', unsigned>();

    program.add_argument("--rw_ratio")
        .required()
        .help("read/write ratio where 0 means only reads and 100 only writes")
        .scan<'u', unsigned>();

    program.add_argument("--stat_file")
        .help("filepath where statistics are logged");

    program.add_argument("--stat_ival")
        .help("interval for statistics logging in ms")
        .scan<'u', unsigned>();

    program.add_argument("--page_log_ival")
        .help("log statistics after a specific number of pages have been read/written")
        .scan<'u', uint64_t>();

    program.add_argument("--once")
        .help("touch memory once and then quit memtouch")
        .default_value(false)
        .implicit_value(true);

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        exit(1);
    }
}

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("memtouch", PROJECT_VERSION);

    setup_signals();
    setup_argparse(program, argc, argv);

    auto thread_mem    = program.get<unsigned>("--thread_mem");
    auto num_threads   = program.get<unsigned>("--num_threads");
    auto rw_ratio      = program.get<unsigned>("--rw_ratio");
    auto once          = program.get<bool>("--once");

    std::string stats_file;
    unsigned stats_ival;
    uint64_t page_log_ival;

    bool stats_requested {false};

    try {
        stats_file = program.get<std::string>("--stat_file");
        stats_requested = true;
    } catch (const std::exception& err) {
    }

    try {
        stats_ival = program.get<unsigned>("--stat_ival");
    } catch (const std::exception& err) {
        stats_ival = DEFAULT_STAT_IVAL;
    }

    try {
        page_log_ival = program.get<uint64_t>("--page_log_ival");
    } catch (const std::exception& err) {
        uint64_t num_pages {(uint64_t(thread_mem) * 1024 * 1024) / PAGE_SIZE};
        page_log_ival = num_pages;
    }

    if (rw_ratio > 100) {
        printf("Invalid rw_ratio, range is 0 to 100\n");
        return 1;
    }

    printf("Running %u threads touching %u MiB of memory\n", num_threads, thread_mem);
    printf("    memory consumption : %d MiB\n", num_threads * thread_mem);
    printf("    r/w ratio          : %u\n", rw_ratio);
    printf("    page log interval  : %lu\n", page_log_ival);

    if (stats_requested and not once) {
        printf("    statistics file    : %s\n", stats_file.data());
        printf("    statistics interval: %u ms\n", stats_ival);

        stat_thread.set_interval(stats_ival);
        bool success {stat_thread.set_log_file(stats_file.data())};
        if (not success) {
            printf("Unable to open statistics file\n");
            return 1;
        }
    }

    worker_storage.reserve(num_threads);
    thread_storage.reserve(num_threads + 1 /* statistics thread */);

    for (unsigned num_thread = 0; num_thread < num_threads; num_thread++) {
        worker_storage.emplace_back(num_thread, once, thread_mem, rw_ratio, page_log_ival);
        if (not worker_storage.back().pre_run()) {
            worker_storage.clear();
            return 1;
        }
    }

    for (unsigned num_thread = 0; num_thread < num_threads; num_thread++) {
        thread_storage.emplace_back(std::move(make_unique<thread>(&WorkerThread::run, &worker_storage.at(num_thread))));
    }

    if (stats_requested and not once) {
        thread_storage.emplace_back(std::move(make_unique<thread>(&StatisticsThread::run, &stat_thread)));
    }

    for (auto& t : thread_storage) {
        t->join();
    }

    thread_storage.clear();
    worker_storage.clear();

    return 0;
}
