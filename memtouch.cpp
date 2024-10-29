#include <argparse.hpp>

#include <chrono>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

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
    WorkerThread(unsigned id_, unsigned mem_size_mb_,
                 unsigned rw_ratio_, bool random_,
                 bool collect_stats_)
        : id(id_)
        , mem_size_mb(mem_size_mb_)
        , rw_ratio(rw_ratio_)
        , random(random_)
        , collect_stats(collect_stats_)
        , stats()
    {
    }

    void run()
    {
        if (not allocate_memory()) {
            printf("Unable to allocate memory\n");
            return;
        }

        uint64_t num_pages {(uint64_t(mem_size_mb) * 1024 * 1024) / PAGE_SIZE};

        // Warmup, write every page
        for (uint64_t page {0}; page < num_pages; ++page) {
            write_page(page);
        }

        while(not terminate) {
            run_loop(num_pages);
        }

        cleanup_memory();
    }

    int64_t measure_time(std::function<void()> func) {
        const auto time_start {chrono::system_clock::now()};
        func();
        const auto time_end {chrono::system_clock::now()};

        const auto time_needed {chrono::duration_cast<chrono::milliseconds>(time_end - time_start)};

        return time_needed.count();
    }

    void run_loop(uint64_t num_pages)
    {
        uint64_t pages_to_read  {num_pages};
        uint64_t pages_to_write {0};

        if (rw_ratio) {
            pages_to_write = (num_pages * rw_ratio) / 100;
            pages_to_read  = num_pages - pages_to_write;
        }

        auto time_read = measure_time([&]() {
            for (uint64_t page {0}; page < pages_to_read; ++page) {
                auto random_value = static_cast<uint64_t>(rand());
                uint64_t actual_page {random ? (random_value % num_pages) : page};
                read_page(actual_page, &read_buffer[0]);
            }
        });

        auto time_write = measure_time([&]() {
            for (uint64_t page {pages_to_read}; page < num_pages; ++page) {
                auto random_value = static_cast<uint64_t>(rand());
                uint64_t actual_page {random ? (random_value % num_pages) : page};
                write_page(actual_page);
            }
        });

        if (rw_ratio < 100) {
            stats.read_rate  = (static_cast<float>(pages_to_read  * PAGE_SIZE) / (1024 * 1024)) / static_cast<float>(time_read) * 1000;
        }

        if (rw_ratio > 0) {
            stats.write_rate = (static_cast<float>(pages_to_write * PAGE_SIZE) / (1024 * 1024)) / static_cast<float>(time_write) * 1000;
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
        if (munmap(mem_base, uint64_t(mem_size_mb) * 1024 * 1024) != 0) {
            printf("Unable to unmap memory\n");
        }
    }

    bool allocate_memory()
    {
        mem_base = mmap(NULL, uint64_t(mem_size_mb) * 1024 * 1024,
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
    unsigned mem_size_mb;
    unsigned rw_ratio;
    bool random;

    bool terminate {false};
    bool collect_stats {false};

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

    void run()
    {
        while (not terminate) {
            float read_rate  {0};
            float write_rate {0};
            for (auto& worker : workers) {
                read_rate  += worker.read_rate();
                write_rate += worker.write_rate();
            }

            printf("[%s] read:%.1f write:%.1f\n", get_iso8601_time().c_str(), read_rate, write_rate);
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

private:
    vector<WorkerThread>& workers;

    bool terminate {false};
    unsigned logging_ival_ms {DEFAULT_STAT_IVAL};
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
        .help("amount of memory a thread touches in MB")
        .scan<'u', unsigned>();

    program.add_argument("--num_threads")
        .required()
        .help("number of worker threads")
        .scan<'u', unsigned>();

    program.add_argument("--rw_ratio")
        .required()
        .help("read/write ratio where 0 means only reads and 100 only writes")
        .scan<'u', unsigned>();

    program.add_argument("--random")
        .help("use random access pattern for memory access [default: false]")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("--stat_file")
        .help("filepath where statistics are logged");

    program.add_argument("--stat_ival")
        .help("interval for statistics logging")
        .scan<'u', unsigned>();

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
    auto random_access = program.get<bool>("--random");

    std::string stats_file;
    unsigned stats_ival;

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

    if (rw_ratio > 100) {
        printf("Invalid rw_ratio, range is 0 to 100\n");
        return 1;
    }

    if (random_access) {
        auto seed = static_cast<unsigned int>(time(nullptr));
        srand(seed);
    }

    printf("Running %u threads touching %u MB of memory\n", num_threads, thread_mem);
    printf("    memory consumption : %d MB\n", num_threads * thread_mem);
    printf("    access pattern     : %s\n", random_access ? "random" : "sequential");
    printf("    r/w ratio          : %u\n", rw_ratio);

    if (stats_requested) {
        printf("    statistics file    : %s\n", stats_file.data());
        printf("    statistics interval: %u\n", stats_ival);
    }

    worker_storage.reserve(num_threads);
    thread_storage.reserve(num_threads + 1 /* statistics thread */);

    for (unsigned num_thread = 0; num_thread < num_threads; num_thread++) {
        worker_storage.emplace_back(num_thread, thread_mem, rw_ratio, random_access, stats_requested);
        thread_storage.emplace_back(std::move(make_unique<thread>(&WorkerThread::run, &worker_storage.back())));
    }

    if (stats_requested) {
        thread_storage.emplace_back(std::move(make_unique<thread>(&StatisticsThread::run, &stat_thread)));
        stat_thread.set_interval(stats_ival);
    }

    for (auto& t : thread_storage) {
        t->join();
    }

    thread_storage.clear();
    worker_storage.clear();

    return 0;
}
