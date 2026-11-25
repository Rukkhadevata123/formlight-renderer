export module formlight.scene_toml;

import std;
export import formlight.scene_config;

export namespace formlight
{
[[nodiscard]] SceneConfig load_scene_config(const std::filesystem::path &path);
void save_scene_config(const std::filesystem::path &path, const SceneConfig &config);
} // namespace formlight
