import std;

auto main(int argc, char **argv) -> int
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println("Usage: formlight-renderer [--help]");
        return 0;
    }

    std::cerr << "Unexpected arguments. Run formlight-renderer --help for usage.\n";
    return 1;
}
