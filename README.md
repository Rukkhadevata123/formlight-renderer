# Formlight Renderer

Formlight Renderer 是一个用 C++23 编写的 CPU 渲染器。下图是光栅化渲染的 glTF 纹理方盒 `BoxTextured`。

![CPU 光栅器渲染的方盒，三个可见面上都有 Cesium 标志](docs/images/box-textured-base-color.png)

导入 glTF 时，三角形列表、三角带和三角扇都会展开为独立三角形。同一张网格可以在场景中放置多次。每个实例（`MeshInstance`）保存自己的 ID 和变换，通过 `std::span<const Triangle>` 只读访问 `GltfScene` 持有的三角形数据。多个实例共享这份数据，各自可以平移、旋转和缩放。

当模型省略 `NORMAL` 时，Formlight 沿用 Rust 参照实现的平滑着色约定。每个图元内，共享顶点按相邻三角形面积加权生成法线，分开索引的边保留硬边。glTF 2.0 对缺失法线规定了逐面平坦着色，因此共享顶点的折面在两种约定下外观会有差异。[glTF 2.0 网格几何规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview)

材质可通过 `KHR_texture_transform` 为每个纹理绑定单独选择 UV0 或 UV1，然后依次缩放、旋转和平移。缩小过滤按变换后的纹理足迹选择 mip 层，镂空材质的主画面和投影也使用变换后的 UV。[Khronos 扩展说明](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform)

### 无窗口构建

在仓库根目录的 x64 Developer PowerShell 中运行以下命令。共享 preset `release` 从 `VCPKG_ROOT` 读取工具链。如果本机用 `release-local` 配置 vcpkg 路径和缓存，可在同一条配置命令中换用它。`FORMLIGHT_BUILD_VIEWER=OFF` 在独立构建目录中只安装核心依赖，SDL3 和 Dear ImGui 由可选的 viewer 引入。

```powershell
$headless = Join-Path $PWD 'build/headless'
$vcpkgWork = "$headless/vcpkg_installed/vcpkg"
cmake --preset release -B $headless -DFORMLIGHT_BUILD_VIEWER=OFF -DBUILD_TESTING=ON `
    "-DVCPKG_INSTALL_OPTIONS=--x-buildtrees-root=$vcpkgWork/blds;--x-packages-root=$vcpkgWork/pkgs"
cmake --build $headless --parallel 4
ctest --test-dir $headless --output-on-failure

$cli = Join-Path $headless 'formlight-renderer.exe'
& $cli --scene scenes/path/cornell-glass.toml --output "$headless/cornell-glass.png"
& $cli --preset many_balls --output "$headless/many-balls.png"
& $cli --preset final_scene --output "$headless/final-scene.png"
```

### 场景

| 想观察什么 | 从哪个场景开始 |
| --- | --- |
| 方盒的灯光与阴影 | 在 [lighting](scenes/lighting/) 中，从[方向光硬阴影](scenes/lighting/box-directional.toml)或[矩形灯软阴影](scenes/lighting/box-area-light.toml)开始。 |
| 材质、纹理与透明层 | 在 [materials](scenes/materials/) 中，用[材质球](scenes/materials/material-spheres.toml)比较金属度与粗糙度，用[交叠透明层](scenes/materials/blend-layers.toml)观察遮挡和混合。 |
| 漫反射间接光 | 用 [Cornell 漫反射房间](scenes/path/cornell-diffuse.toml)观察顶灯照亮白墙，以及红绿侧墙向墙角反射的颜色。 |
| 镜面与折射 | 在 [Cornell 玻璃球](scenes/path/cornell-glass.toml)中观察球面反射的顶灯和透过球体的墙面。 |

[方盒阴影对照](docs/box-shadows.md) 并排展示同机位的阴影图、阴影射线与矩形灯软阴影，并给出本影、半影、亮区的像素重放命令。

[双灯方盒](scenes/lighting/box-two-lights.toml) 用暖色方向光和冷色点光源照亮同一块地面。运行 `formlight-renderer --scene scenes/lighting/box-two-lights.toml --pixel 320 240` 可查看两盏灯各自的线性 RGB 贡献和遮挡结果。将某盏灯的 `enabled` 设为 `false` 可以单独观察另一盏灯。

场景文件用 `[lighting]` 保存全局环境光，每个 `[[lighting.lights]]` 定义一盏直接光源。迁移旧配置时，把方向光、点光源或矩形灯的 `[light]` 改为 `[[lighting.lights]]`，把 `ambient` 移到 `[lighting]`。纯环境光只需 `[lighting]` 和 `ambient`。省略整个照明配置会显示底色和自发光。

在 `raster` 模式下，可在方向光的 `[[lighting.lights]]` 中设置 `pcf = true`。渲染器在 3×3 个阴影纹素上分别比较深度，按可见比例缩放这盏灯的直接光。默认使用单纹素硬阴影。`--pixel X Y` 和 viewer 的像素诊断会显示边缘处的可见比例。`hybrid` 模式使用阴影射线。

CLI 和 viewer 都可以打开单文件 `.glb` 和多文件 `.gltf`。[双 UV 面板](scenes/materials/dual-uv-panel.toml) 的模型路径相对于 TOML，模型里的缓冲区和纹理路径相对于 `.gltf` 文件。将 TOML 另存到其他目录时，程序会重算模型路径。

默认每个像素采样一次。在 TOML 的 `[render]` 中设置 `ssaa = 2`，或在命令行加上 `--ssaa 2`，即可启用 2×2 超采样。四个子样本各自处理深度、镂空、光照和透明层，然后在线性空间取平均。用 `--pixel X Y` 可以查看每个子样本的位置、权重、命中表面和颜色，以及合成后的像素颜色。

### 路径追踪

在 CLI 加上 `--mode path`，或在 TOML 的 `[render]` 中设置 `mode = "path"`。积分器从相机发射主射线，经 BVH 取最近的可见表面。默认 `max_bounces = 0`，返回材质自发光与显式矩形灯的直接反射。主射线和反弹射线离开场景时读取 `[scene]` 的 `environment`（线性 RGB），默认值为黑色。射线接受 OPAQUE 和通过阈值的 MASK，跳过 BLEND，按材质的单双面设置求交。光线命中纹理时使用第零层 mip。

运行 `formlight-renderer --scene scenes/materials/emissive-panels.toml --mode path --pixel 200 160`，可在背光的不透明面板上重放首命中，预期线性 RGB 为 (0.25, 0.125, 0.0625)。将像素参数换成 `--output build/release/path-emission.png` 可输出整图。[场景](scenes/materials/emissive-panels.toml) · [积分器](src/path/integrator.cppm)

运行 `formlight-renderer --scene scenes/lighting/box-area-light.toml --mode path --pixel 85 310` 可重放半影。种子 7 下，方盒挡住了 64 个灯面样本中的 35 个，地面的线性 RGB 三个通道均为 0.06451683。把模式切换为 `hybrid` 可逐样本核对灯面位置、遮挡物与直接光贡献。[方盒阴影对照](docs/box-shadows.md)

将 `max_bounces` 设为正数会从首命中继续追踪材质指定的反射或透射。每个相机样本平均 `path_samples` 条路径。默认的 `bounce_sampling = "cosine"` 按余弦加权半球分布采样，此时 Lambertian 权重化简为表面底色乘以非金属占比。固定 `seed` 可用 `--pixel` 重放同一组路径，报告中的 `Path bounces` 为它们带回的线性 RGB。

离线输出路径图像时，`--path-samples` 指定每个 SSAA 子像素的总样本数，`--batch-size` 指定每批最多追加多少个样本，默认一次完成。下例把 128 个样本拆成六批 19 个和最后一批 14 个。在同一构建下，固定场景和种子，各子像素按相同编号顺序累加，拆批前后的线性图像逐像素相等。[分批累积回归](tests/path_frame_test.cpp)

```powershell
build/release/formlight-renderer.exe --preset final_scene --path-samples 128 --batch-size 19 --output build/release/final-scene-batches.png
```

运行 `build/release/formlight-viewer.exe --scene scenes/path/final-scene.toml` 可观察同一场景逐渐收敛。面板顶部的 **Path samples** 显示当前画面每个 SSAA 子像素已完成的样本数和目标值。默认场景从 8 / 128 显示到 128 / 128，每批最多追加 8 个样本。点选像素后，诊断会随同一机位的新批次刷新。[viewer 渐进显示回归](tests/viewer_path_progress_test.cpp)

想让当前画面继续收敛，在 **Render** 中调高 `path_samples` 后点击 **Apply**。例如从 19 提到 35，进度会从 19 / 35 走到 27 / 35、35 / 35。渲染途中也可以提高目标，当前批次会先完成。同时修改种子、灯光或快门等渲染条件，或调低目标，会重新采样。

点击 **Path samples** 旁的 **Cancel**，图像和计数会停在屏幕上最后一个完整批次。此时仍可调曝光、拾取像素。点击 **Resume** 或调高目标并点击 **Apply**，会从保留的样本数继续。移动相机后，viewer 会为新机位重新采样。

要核对窗口颜色与导出的 PNG，在启用 viewer 的构建中运行 `ctest --preset release-local -R viewer_display_rgb8 -V`。回归在 SDL 软件后端按原尺寸读回场景纹理，逐像素比较同一采样数和显示设置下 CLI 输出的 PNG，覆盖渐进帧、降低一档曝光和切换 Reinhard。[显示像素回归](tests/viewer_display_test.cpp)

Cornell 漫反射场景启用 `bounce_sampling = "light_mixture"`。在漫反射表面，各有一半概率采样余弦半球或发光网格。网格分支按实例变换后的三角形面积选灯面，再在面内均匀取点。两个分支都用同一混合概率密度计算路径权重，实际命中的表面由 BVH 决定。发光网格和额外采样目标均为空时使用纯余弦采样。命令行可用 `--bounce-sampling cosine` 或 `--bounce-sampling light_mixture` 在相同路径数下比较噪声。[采样器](src/path/emitter_sampling.cppm) · [积分对照](tests/cornell_diffuse_test.cpp)

Cornell 房间的唯一光源是向下发光的天花板网格。一次反弹能让白墙直接看见灯，三次反弹还会带回侧墙的颜色。在同一机位运行：

```powershell
$scene = "scenes/path/cornell-diffuse.toml"
build/release/formlight-renderer.exe --scene $scene --path-samples 2048 --max-bounces 1 --output build/release/cornell-direct.png
build/release/formlight-renderer.exe --scene $scene --path-samples 2048 --max-bounces 3 --output build/release/cornell-bounces.png
```

[Cornell 玻璃球](scenes/path/cornell-glass.toml)使用同一机位。运行 `build/release/formlight-renderer.exe --scene scenes/path/cornell-glass.toml --output build/release/cornell-glass.png` 即可出图。场景通过 `[[scene.path_materials]]` 的 `material` 选择 glTF 中从 0 开始的材质索引。设为 `type = "dielectric"` 时，`ior = 1.5` 表示空气中封闭的无吸收玻璃，光线在入射面和出射面分别按 Fresnel 反射率抽样，全内反射时沿镜面方向继续。改成 `type = "mirror"` 并移除 `ior`，即可用同一个球观察理想镜面，反射颜色取自 glTF 底色。两种材质都可沿次级射线看见发光网格。显式矩形灯目前仅参与普通表面的直接光估计。[光学回归](tests/path_specular_test.cpp)

在玻璃球场景的 `[scene]` 中，`sampling_targets = [1]` 指定 glTF 节点 1 的网格实例为额外方向采样目标。漫反射路径有 1/2 的概率采样余弦半球、1/4 采样顶灯、1/4 均匀采样玻璃球包围球张开的立体角。光线随后仍与真实网格求交，玻璃的自发光为零。将列表改为 `[]` 即可在同一场景比较只采样余弦半球与顶灯的效果。固定 128 条路径、64 个种子，球底三个地面探针的线性红通道综合 RMS 误差由 0.264 降到 0.232，球外对照点由 0.028 升到 0.045。[采样实现](src/path/target_sampling.cppm) · [焦散回归](tests/cornell_caustic_test.cpp)

[Cornell 解析球](scenes/path/cornell-analytic.toml)沿用网格球的机位、球心、半径和折射率。这个版本在 TOML 的 `[[scene.spheres]]` 中直接指定世界坐标 `center` 和 `radius`。在 `path` 模式下，三角形和球共用一个 BVH；每个球占一个图元，像素诊断会显示球的序号和曲面法线。球可使用 `diffuse`、`mirror`、`metal` 或 `dielectric` 材质，`sampling_target = true` 将它加入方向采样。只含球的场景可以省略 `scene.asset`。用 `--pixel 46 96` 对照两份 Cornell 配置，解析球的视深为 3.32187887，网格球为 3.32302275。[求交回归](tests/sphere_query_test.cpp)

[Many Balls](scenes/path/many-balls.toml) 使用薄透镜相机观察固定布局的静止球，环境辐亮度为 (0.7, 0.8, 1.0)。默认场景种子 42 生成 480 个小球，另有三个半径为 1 的主球和一个地面球。其中 73 个小金属球各有固定的模糊度，前景大金属球使用理想镜面。默认预览为 256×144，每像素 32 条路径，最多反弹 8 次。

```powershell
build/release/formlight-renderer.exe --preset many_balls --scene-seed 42 --seed 7 --output build/release/many-balls.png
```

`--scene-seed` 控制小球的位置和材质，`--seed` 控制路径采样。CLI 和 viewer 均可用 `--scene scenes/path/many-balls.toml` 加载同一预设，再用 TOML 覆盖相机、环境色或采样数。[布局与环境光回归](tests/many_balls_test.cpp)

在 Many Balls 的 `[scene]` 中，`metal_fuzz_max` 控制小金属球模糊度的上限，默认值为 0.5。将它设为 0，并关闭光圈，可用同一机位和种子复现理想镜面基线。在单个 `[[scene.spheres]]` 或 `[[scene.path_materials]]` 中，设 `type = "metal"` 和 `fuzz = 0.3` 即可指定模糊度（范围 0 到 1）。积分器在镜面反射方向上加一个长度为 `fuzz`、方向在球面上均匀分布的向量，再将结果归一化。落到几何表面下方的样本贡献为零。`--pixel` 可查看命中金属的实际模糊度。[散射实现](src/path/specular.cppm) · [反射探针](tests/path_specular_test.cpp)

Many Balls 默认使用 `[camera]` 中的 `focus_distance = 10` 和 `defocus_angle_degrees = 0.6`。前者是沿相机前向轴量到焦平面的世界距离，后者是从轴上焦点看光圈直径的张角。每条路径在圆形光圈上均匀取一个起点，朝该像素在焦平面上的点发射射线；改变光圈时，焦平面上的投影位置保持一致。

```powershell
build/release/formlight-renderer.exe --preset many_balls --defocus-angle 0 --output build/release/many-balls-pinhole.png
build/release/formlight-renderer.exe --preset many_balls --focus-distance 10 --defocus-angle 0.6 --output build/release/many-balls-thin-lens.png
```

在 viewer 的 **Lens** 面板修改这两项后点击 **Apply** 即可对照。光圈开启时，每个 SSAA 子样本使用 `path_samples` 条透镜射线；像素诊断可重放每条射线的光圈起点、命中和颜色。[光圈边缘回归](tests/thin_lens_test.cpp)

[运动球示例](scenes/path/moving-sphere.toml) 中，`center` 是球在时间 0 的位置，`displacement` 是从时间 0 到 1 的世界坐标位移。`[camera]` 的 `shutter_open` 与 `shutter_close` 选取这段运动的曝光区间，默认都为 0。区间有宽度时，每条路径均匀抽取一次时间，反弹、阴影和方向采样都使用该时刻的几何。两端同设为 0.5 可观察正中间的静止画面。在 viewer 的 **Shutter** 面板可调整区间，CLI 的 `--pixel` 可重放样本。

```powershell
build/release/formlight-renderer.exe --scene scenes/path/moving-sphere.toml --output build/release/moving-sphere.png
```

[纯吸收介质示例](scenes/path/absorbing-sphere.toml) 在白色环境前放了一个半径为 1 的球形介质。每个 `[[scene.media]]` 用 `center`、`radius` 指定边界，可用 `displacement` 指定运动。`extinction` 是每单位世界距离的消光系数。在这个纯吸收示例中，射线穿过距离 `d` 后仍能到达背景的概率为 `exp(-extinction * d)`。示例取 `extinction = 0.5`，中心射线穿过直径 2，透过率约为 36.8%。像素诊断可区分看见背景和被吸收的路径，并报告吸收事件的射线参数。

```powershell
build/release/formlight-renderer.exe --scene scenes/path/absorbing-sphere.toml --output build/release/absorbing-sphere.png
```

[蓝色玻璃球与全局雾](scenes/path/scattering-media.toml) 单独取出 `final_scene` 的两种介质，放在均匀白色环境中。局部玻璃球半径为 70，内部介质的 `scattering_albedo = [0.2, 0.4, 0.9]`；外层白雾半径为 5000，消光系数为 0.0001。玻璃表面负责折射，介质边界只限定自由程的采样区间。

`scattering_albedo` 是每次碰撞后各颜色通道保留的比例，默认为零。散射方向在整个球面上均匀采样，每次继续传播占用一次 `max_bounces`。有矩形灯时，碰撞点还会采样灯面，计算遮挡和沿途介质的透过率。

```powershell
build/release/formlight-renderer.exe --scene scenes/path/scattering-media.toml --output build/release/scattering-media.png
```

[地球贴图示例](scenes/path/earth.toml) 取出 `final_scene` 中半径为 100 的漫反射地球。可在 `[[scene.spheres]]` 的 `albedo_texture` 中填写 PNG 或 JPEG 的路径，路径相对于 TOML 所在目录。纹理按 sRGB 解码，在线性 RGB 中双线性过滤，再乘以 `albedo`。同一张图可由多个球共用。

球面经度在 −X 方向接合，沿 U 循环采样。北极对应图像首行，南极对应末行，V 在两端钳制。CLI 的 `--pixel` 和 viewer 拾取都可查看命中点的 UV。Rust 参考实现采用最近邻采样，并将图像字节直接除以 255；比较材质时要考虑两种采样方式和颜色空间的差异。

```powershell
build/release/formlight-renderer.exe --scene scenes/path/earth.toml --output build/release/earth.png
```

[大理石球示例](scenes/path/marble.toml) 使用 `final_scene` 中球心为 (220, 280, 300)、半径为 80 的球。在 `[[scene.spheres]]` 中设置 `marble = { scale = 0.2, seed = 42 }` 即可复现石纹。`scale` 控制沿 Z 轴的条纹频率，`seed` 控制纹路形状。渲染种子只影响采样。球有运动时，材质仍在快门起点的球面坐标上求值，纹路会随球平移。

程序纹理把七层三维 Perlin 噪声相加，每层频率翻倍、权重减半，最后取绝对值得到 `turbulence(p)`。线性反照率乘以 `0.5 * (1 + sin(scale * p.z + 10 * turbulence(p)))`。指定颜色或图像贴图时，石纹也会调制它们。[实现](src/path/perlin.cppm) · [数值回归](tests/perlin_test.cpp)

```powershell
build/release/formlight-renderer.exe --scene scenes/path/marble.toml --output build/release/marble.png
```

[三维棋盘示例](scenes/path/checker.toml) 把解析球放进立方体网格，球面穿过两种颜色的格子时形成花纹。在 `[[scene.spheres]]` 中设置 `checker = { cell_size = 0.5, even = [0.8, 0.72, 0.54], odd = [0.035, 0.22, 0.18] }`。`cell_size` 是格子在世界坐标中的边长，`even` 和 `odd` 是线性 RGB。每个轴的格子编号取 `floor(坐标 / cell_size)`，三个编号之和的奇偶决定颜色。例如 X 从略小于零移到零，Y、Z 都为零时，颜色从 `odd` 切换到 `even`。棋盘色乘到材质底色上，球平移时格纹随球移动。

```powershell
build/release/formlight-renderer.exe --scene scenes/path/checker.toml --output build/release/checker.png
```

[final_scene 场景](scenes/path/final-scene.toml) 采用 Rust 参考机位：相机从 (478, 278, −600) 看向 (278, 278, 0)，方形画幅，垂直视场 40°。单面发光顶灯照亮运动球、玻璃球、模糊金属球、蓝色玻璃介质、地球、大理石球、地块和千球群。全局白雾与局部蓝色介质可重叠。

地面由 20×20 个地块组成。每个地块底面为 100×100，地面从 X、Z 的 −1000 延伸到 1000。地块共用一个闭合的立方体网格；每格的位置和高度由实例变换给出。`--scene-seed` 决定各格在 [1, 101) 内的高度，`--seed` 控制光照和路径采样。千球群使用同一个布局种子和独立的随机流。1000 个局部球心分布在边长为 165 的立方体内，先绕 Y 轴旋转 15°，再平移 (−100, 270, 395)。所有球的半径均为 10，共用线性反照率为 (0.73, 0.73, 0.73) 的漫反射材质。生成场景时将球心一次性变换到世界坐标，解析球与地块随后共用同一棵 BVH。[球群几何回归](tests/final_cluster_test.cpp)

```powershell
build/release/formlight-renderer.exe --preset final_scene --scene-seed 42 --output build/release/final-scene.png
```

默认预览为 192×192，每像素 128 条路径，最多反弹 16 次，快门在 [0, 1] 均匀采样。顶灯由两个朝下的发光三角形组成，线性辐亮度为 (7, 7, 7)，漫反射路径使用 `light_mixture` 采样。提高 `--path-samples` 可降低玻璃和雾的噪点。内置地球材质从配置构建时的项目目录读取 `assets/earthmap.jpg`。自定义 TOML 可以提供自己的 `scene.spheres` 或 `scene.media` 列表来替换对应预设内容。[完整场景回归](tests/final_scene_test.cpp)

### 自定义光栅 shader

构建后运行 `build/release/custom_shader.exe build/release/custom-shader.png`，可看到近面裁剪后的渐变三角形，中间的条纹透出背景。[示例源码](examples/custom_shader.cpp)

导入 `formlight.shader`，把 shader 对象传给 `render_shader`。顶点函数返回网格局部位置和自定义 `Varying`，之后管线应用实例变换和相机投影。属性类型支持加法和乘以标量，光栅器用原三角形的透视校正权重插值。片元函数返回线性颜色时写入颜色和深度，返回 `std::nullopt` 时两者都保留原值。这个入口每像素取一个中心样本。[接口定义](src/raster/shader.cppm)

源码采用 [MIT 许可证](LICENSE)。方盒模型和标志的来源与许可见[素材说明](assets/README.md)。
