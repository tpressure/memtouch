#include <argparse.hpp>

#include <thread>
#include <vector>
#include <memory>

class WorkerThread
{
public:
    WorkerThread(unsigned id_) : id(id_)
    {
        printf("Worker %d created\n", id);
    }

    void run()
    {
        printf("Worker %d executing\n", id);
    }
private:
    unsigned id;
};

using namespace std;

int main(int argc, char** argv)
{
    vector<WorkerThread> worker_storage;
    vector<unique_ptr<thread>> thread_storage;
    argparse::ArgumentParser program("program_name");

    program.add_argument("--thread_mem")
        .help("amount of memory a thread touches in MB")
        .scan<'u', unsigned>();

    program.add_argument("--num_threads")
        .help("number of worker threads")
        .scan<'u', unsigned>();

    program.add_argument("--random")
        .help("use random access pattern for memory access (default is false)")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("--rw_ratio")
        .help("read/write ratio where 0 means only reads and 100 only writes")
        .scan<'u', unsigned>();

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

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
        worker_storage.emplace_back(num_thread);
        thread_storage.emplace_back(std::move(make_unique<thread>(&WorkerThread::run, &worker_storage.back())));
    }

    for (unsigned num_thread = 0; num_thread < num_threads; num_thread++) {
        thread_storage[num_thread]->join();
    }

    return 0;
}
