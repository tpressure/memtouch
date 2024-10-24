#include <argparse.hpp>

int main(int argc, char** argv)
{
  argparse::ArgumentParser program("program_name");

  program.add_argument("--thread_mem")
    .help("amount of memory a thread touches in MB")
    .scan<'u', unsigned>();

  program.add_argument("--num_threads")
    .help("number of worker threads")
    .scan<'u', unsigned>();

  try {
    program.parse_args(argc, argv);
  }
  catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    return 1;
  }

  auto thread_mem  = program.get<unsigned>("--thread_mem");
  auto num_threads = program.get<unsigned>("--num_threads");

  printf("Running %u threads touching %u MB of memory\n", num_threads, thread_mem);

  return 0;
}
