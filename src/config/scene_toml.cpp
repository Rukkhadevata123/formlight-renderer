module;
#include <toml++/toml.hpp>

module formlight.scene_toml;
import std;
import formlight.output_file;

namespace formlight
{
namespace
{
using Path = std::filesystem::path;

std::string utf8(const Path &path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

double read_real(const toml::node &node)
{
    if (node.is_number())
    {
        if (const auto number = node.value<double>(); number && std::isfinite(*number))
        {
            return *number;
        }
    }
    throw std::invalid_argument("expected a finite number");
}

template <class T> void read_value(const toml::node &node, T &value, const Path &directory)
{
    if constexpr (std::same_as<T, Vec3>)
    {
        const auto *array = node.as_array();
        if (!array || array->size() != 3)
        {
            throw std::invalid_argument("expected three coordinates");
        }
        value = {read_real((*array)[0]), read_real((*array)[1]), read_real((*array)[2])};
    }
    else if constexpr (std::same_as<T, double>)
    {
        value = read_real(node);
    }
    else if constexpr (std::same_as<T, bool>)
    {
        if (!node.is_boolean())
        {
            throw std::invalid_argument("expected a boolean");
        }
        value = node.as_boolean()->get();
    }
    else if constexpr (std::integral<T>)
    {
        if (const auto *number = node.as_integer(); number && std::in_range<T>(number->get()))
        {
            value = static_cast<T>(number->get());
            return;
        }
        // TOML integers stop at INT64_MAX. Larger unsigned seeds round-trip as decimal strings.
        if constexpr (std::same_as<T, std::uint64_t>)
        {
            if (const auto s = node.value_exact<std::string_view>())
            {
                const auto end = s->data() + s->size();
                const auto [last, error] = std::from_chars(s->data(), end, value);
                if (error == std::errc{} && last == end)
                {
                    return;
                }
            }
        }
        throw std::invalid_argument("expected an integer in range");
    }
    else if constexpr (std::is_enum_v<T> || std::same_as<T, Path>)
    {
        const auto s = node.value_exact<std::string_view>();
        if (!s)
        {
            throw std::invalid_argument("expected a string");
        }
        if constexpr (std::is_enum_v<T>)
        {
            value = parse_choice<T>(*s);
        }
        else if (!s->empty())
        {
            value = (directory / Path{std::u8string{s->begin(), s->end()}}).lexically_normal();
        }
    }
    else
    {
        static_assert(false, "Missing TOML field reader");
    }
}

void read_group(const toml::table &table, ConfigGroup auto &group, std::string_view prefix, const Path &directory)
{
    std::set<std::string_view> remaining;
    for (const auto &[key, node] : table)
    {
        remaining.insert(key.str());
    }
    group.fields(
        [&](std::string_view key, auto &value)
        {
            const auto *node = table.get(key);
            if (!node)
            {
                return;
            }
            remaining.erase(key);
            const auto name = std::string(prefix) + std::string(key);
            if constexpr (ConfigGroup<std::remove_cvref_t<decltype(value)>>)
            {
                if (!node->is_table())
                {
                    throw std::invalid_argument(name + ": expected a table");
                }
                read_group(*node->as_table(), value, name + ".", directory);
            }
            else
            {
                try
                {
                    read_value(*node, value, directory);
                }
                catch (const std::invalid_argument &error)
                {
                    throw std::invalid_argument(name + ": " + error.what());
                }
            }
        });
    if (!remaining.empty())
    {
        throw std::invalid_argument(std::format("Unknown or inactive field: {}{}", prefix, *remaining.begin()));
    }
}

toml::table write_group(const ConfigGroup auto &group, const Path &directory)
{
    toml::table table;
    group.fields(
        [&](std::string_view key, const auto &value)
        {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (ConfigGroup<T>)
            {
                table.insert(key, write_group(value, directory));
            }
            else if constexpr (std::same_as<T, Path>)
            {
                const auto absolute = std::filesystem::absolute(value);
                const auto relative = absolute.lexically_relative(directory);
                table.insert(key, utf8(relative.empty() ? absolute : relative));
            }
            else if constexpr (std::same_as<T, Vec3>)
            {
                table.insert(key, toml::array{value.x, value.y, value.z});
            }
            else if constexpr (std::is_enum_v<T>)
            {
                for (const auto &choice : choices(value))
                {
                    if (choice.value == value)
                    {
                        table.insert(key, choice.name);
                        return;
                    }
                }
                throw std::invalid_argument(std::format("Invalid enum for {}", key));
            }
            else if constexpr (std::same_as<T, std::uint64_t>)
            {
                if (std::in_range<std::int64_t>(value))
                {
                    table.insert(key, static_cast<std::int64_t>(value));
                }
                else
                {
                    table.insert(key, std::to_string(value));
                }
            }
            else
            {
                table.insert(key, value);
            }
        });
    return table;
}
} // namespace

SceneConfig load_scene_config(const Path &path)
{
    try
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("cannot open scene configuration");
        }
        const auto table = toml::parse(file);
        SceneConfig config;
        read_group(table, config, "", std::filesystem::absolute(path).parent_path());
        return config;
    }
    catch (const toml::parse_error &error)
    {
        throw std::runtime_error(std::format("{}:{}:{}: {}", utf8(path), error.source().begin.line,
                                             error.source().begin.column, error.description()));
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error(std::format("{}: {}", utf8(path), error.what()));
    }
}

void save_scene_config(const Path &path, const SceneConfig &config)
{
    (void)prepare_render(config);
    if (std::filesystem::exists(path) && std::filesystem::exists(config.scene.asset) &&
        std::filesystem::equivalent(path, config.scene.asset))
    {
        throw std::invalid_argument("Scene configuration and model refer to the same file");
    }
    const auto table = write_group(config, std::filesystem::absolute(path).parent_path());
    replace_output_file(path, [&](std::ostream &file) { file << table << '\n'; });
}
} // namespace formlight
