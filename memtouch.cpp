#include <argparse.hpp>

#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/mman.h>

static constexpr char VERSION[] {"0.1"};

static constexpr uint64_t PAGE_SIZE (4096);
static constexpr int      PATTERN   (0xff);

class WorkerThread
{
public:
    WorkerThread(unsigned id_, unsigned mem_size_mb_, unsigned rw_ratio_, bool random_)
        : id(id_)
        , mem_size_mb(mem_size_mb_)
        , rw_ratio(rw_ratio_)
        , random(random_)
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

    void run_loop(uint64_t num_pages)
    {
        for (uint64_t page {0}; page < num_pages; ++page) {

            uint64_t actual_page {random ? (rand() % num_pages) : page};

            if ((page % 100) >= (100 - rw_ratio)) {
                write_page(actual_page);
            } else {
                read_page(actual_page, &read_buffer[0]);
            }

            if (terminate) {
                break;
            }
        }
    }

    void write_page(uint64_t page) {
        memset(static_cast<char*>(mem_base) + page * PAGE_SIZE, PATTERN, PAGE_SIZE);
    }

    void read_page(uint64_t page, void* buffer) {
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

        if (mem_base == MAP_FAILED) {
            return false;
        }

        return true;
    }

    void kill()
    {
        terminate = true;
    }

private:
    unsigned id;
    unsigned mem_size_mb;
    unsigned rw_ratio;
    bool random;

    bool terminate {false};

    void* mem_base {nullptr};

    char read_buffer[PAGE_SIZE];
};

using namespace std;

vector<WorkerThread> worker_storage;
vector<unique_ptr<thread>> thread_storage;

void sigint_handler(int s){
    printf("Terminating...\n");
    for (auto& worker : worker_storage) {
        worker.kill();
    }
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
        .help("use random access pattern for memory access (default is false)")
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
    argparse::ArgumentParser program("memtouch", VERSION);

    setup_signals();
    setup_argparse(program, argc, argv);


    auto thread_mem    = program.get<unsigned>("--thread_mem");
    auto num_threads   = program.get<unsigned>("--num_threads");
    auto rw_ratio      = program.get<unsigned>("--rw_ratio");
    auto random_access = program.get<bool>("--random");

    if (rw_ratio > 100) {
        printf("Invalid rw_ratio, range is 0 to 100\n");
        return 1;
    }

    if (random_access) {
        srand(time(nullptr));
    }

    printf("Running %u threads touching %u MB of memory\n", num_threads, thread_mem);
    printf("    access pattern: %s\n", random_access ? "random" : "sequential");
    printf("    r/w ratio     : %u\n", rw_ratio);

    worker_storage.reserve(num_threads);
    thread_storage.reserve(num_threads);

    for (unsigned num_thread = 0; num_thread < num_threads; num_thread++) {
        worker_storage.emplace_back(num_thread, thread_mem, rw_ratio, random_access);
        thread_storage.emplace_back(std::move(make_unique<thread>(&WorkerThread::run, &worker_storage.back())));
    }

    for (unsigned num_thread = 0; num_thread < num_threads; num_thread++) {
        thread_storage[num_thread]->join();
    }

    thread_storage.clear();
    worker_storage.clear();

    return 0;
}
