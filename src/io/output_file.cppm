export module formlight.output_file;

import std;

export namespace formlight
{
template <class Write> void replace_output_file(const std::filesystem::path &path, Write write)
{
    auto temporary = path;
    temporary += std::format(".{:08x}.tmp", std::random_device{}());
    std::ofstream file(temporary, std::ios::binary | std::ios::noreplace);
    if (!file)
    {
        throw std::runtime_error(std::format("Cannot open output: {}", path.string()));
    }
    try
    {
        write(file);
        file.close();
        if (!file)
        {
            throw std::runtime_error(std::format("Cannot write output: {}", path.string()));
        }
        std::filesystem::rename(temporary, path);
    }
    catch (...)
    {
        file.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
} // namespace formlight
