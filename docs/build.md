# 构建目录与本机配置

[首页](../README.md#构建与运行)给出了带交互窗口的最短构建命令。仓库自带的共享 preset `release` 使用 **Ninja**、**`clang-cl`** 和自定义 triplet **`x64-windows-clang-release`**，默认构建目录为 `build/release`。以下命令均在仓库根目录的 PowerShell 中运行。[共享 preset](../CMakePresets.json) · [triplet](../triplets/x64-windows-clang-release.cmake)

## 1. 用 `CMakeUserPresets.json` 收口本机路径与缓存

共享 preset 通过环境变量 `VCPKG_ROOT` 定位 `scripts/buildsystems/vcpkg.cmake`。如果不希望每次配置前手动设置环境变量，也不希望 vcpkg 向 `%LOCALAPPDATA%\vcpkg` 写入缓存，可以在仓库根目录创建 `CMakeUserPresets.json`（已被 `.gitignore` 忽略），把本机路径和缓存统一收口到 vcpkg 根目录与项目构建目录中：

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "release-local",
      "inherits": "release",
      "binaryDir": "${sourceDir}/build/release",
      "environment": {
        "VCPKG_ROOT": "D:/vcpkg",
        "VCPKG_DOWNLOADS": "$env{VCPKG_ROOT}/downloads",
        "VCPKG_DEFAULT_BINARY_CACHE": "$env{VCPKG_ROOT}/archives",
        "X_VCPKG_REGISTRIES_CACHE": "$env{VCPKG_ROOT}/registries",
        "VCPKG_DISABLE_METRICS": "1"
      },
      "cacheVariables": {
        "FORMLIGHT_BUILD_VIEWER": "ON",
        "VCPKG_INSTALL_OPTIONS": "--x-buildtrees-root=${sourceDir}/build/release/vcpkg_installed/vcpkg/blds;--x-packages-root=${sourceDir}/build/release/vcpkg_installed/vcpkg/pkgs"
      }
    }
  ],
  "buildPresets": [
    { "name": "release-local", "configurePreset": "release-local" }
  ],
  "testPresets": [
    { "name": "release-local", "inherits": "release", "configurePreset": "release-local" }
  ]
}
```

配置要点：
- **目录准备**：按本机实际位置替换 `VCPKG_ROOT`，并提前建好 `archives` 和 `registries` 子目录。
- **缓存分工**：跨项目共享的下载包、二进制缓存和 registry 留在 `D:\vcpkg`；单项目的编译临时树（`blds`）与打包目录（`pkgs`）留在 `build/release/vcpkg_installed/vcpkg`，删除 `build/release` 即可一并清理。
- **依赖基线与本地补丁**：[vcpkg.json](../vcpkg.json) 锁定了依赖版本基线，仓库另通过 [ports/fastgltf](../ports/fastgltf) 提供 fastgltf 的 overlay port。
- **与 clangd 对齐**：配置好之后，configure / build / test 均可直接使用 `--preset release-local`。根目录的 [.clangd](../.clangd) 默认从 `build/release` 读取 `compile_commands.json`，若修改了构建目录，需同步更新编辑器配置。

## 2. 单独验证无窗口 CLI 构建

需要确认无窗口依赖（不引入 SDL3 和 Dear ImGui）的 CLI 构建与测试时，可以单独建一个 `build/headless` 目录。vcpkg 会把包安装到该目录下，同时继续复用共享的 `downloads` 和 `archives` 二进制缓存。

下面的命令基于上面的 `release-local` preset；若在 PowerShell 中自行配置了 `VCPKG_ROOT` 等环境变量，把 `release-local` 换成 `release` 即可：

```powershell
$headless = Join-Path $PWD 'build/headless'
$vcpkgWork = "$headless/vcpkg_installed/vcpkg"
cmake --preset release-local -B $headless -DFORMLIGHT_BUILD_VIEWER=OFF -DBUILD_TESTING=ON `
    "-DVCPKG_INSTALL_OPTIONS=--x-buildtrees-root=$vcpkgWork/blds;--x-packages-root=$vcpkgWork/pkgs"
cmake --build $headless --parallel 4
ctest --test-dir $headless --output-on-failure

$cli = Join-Path $headless 'formlight-renderer.exe'
& $cli --scene scenes/path/cornell-glass.toml --output "$headless/cornell-glass.png"
& $cli --preset many_balls --output "$headless/many-balls.png"
& $cli --preset final_scene --output "$headless/final-scene.png"
```

## 3. 构建选项与运行时注意

- **`FORMLIGHT_BUILD_VIEWER`**：设为 `ON` 时会启用 `vcpkg.json` 的 `viewer` feature，额外引入 SDL3、Dear ImGui 和窗口相关回归测试；设为 `OFF` 时仅构建 CLI 和示例，仍完整支持 `raster`、`hybrid` 和 `path` 三种模式。
- **动态库部署**：当前 triplet 采用动态链接，运行时请保留可执行文件同目录下的依赖 DLL（由 vcpkg 自动拷贝）。
- **资源路径规则**：内置 `final_scene` 预设在 CMake 配置期记录了源码目录下的 `assets/earthmap.jpg`；自定义 TOML 场景中的资产与纹理路径则统一相对于该 TOML 文件所在目录解析。
