#include <argparse.hpp>

#include <thread>
#include <vector>
#include <memory>

#include <signal.h>

class WorkerThread
{
public:
    WorkerThread(unsigned id_, unsigned mem_size_mb_, unsigned rw_ratio_, bool random_)
        : id(id_)
        , mem_size_mb(mem_size_mb_)
        , rw_ratio(rw_ratio_)
        , random(random_)
    {
        printf("Worker %d created\n", id);
    }

    void run()
    {
        printf("Worker %d executing\n", id);
        while(not terminate) {
            asm volatile ("nop");
        }
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
};

using namespace std;

vector<WorkerThread> worker_storage;
vector<unique_ptr<thread>> thread_storage;

void sigint_handler(int s){
    printf("Terminating...");
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

    program.add_argument("--random")
        .help("use random access pattern for memory access (default is false)")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("--rw_ratio")
        .required()
        .help("read/write ratio where 0 means only reads and 100 only writes")
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
    argparse::ArgumentParser program("memtouch");

    setup_signals();
    setup_argparse(program, argc, argv);


    auto thread_mem    = program.get<unsigned>("--thread_mem");
    auto num_threads   = program.get<unsigned>("--num_threads");
    auto rw_ratio      = program.get<unsigned>("--rw_ratio");
    auto random_access = program.get<bool>("--random");

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

    return 0;
}
