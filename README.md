# Formlight Renderer

Formlight Renderer 是一个用 C++23 编写的 CPU 渲染器。下图是光栅化渲染的 glTF 纹理方盒 `BoxTextured`。

![CPU 光栅器渲染的方盒，三个可见面上都有 Cesium 标志](docs/images/box-textured-base-color.png)

导入 glTF 时，三角形列表、三角带和三角扇都会展开为独立三角形。同一张网格可以在场景中放置多次。每个实例（`MeshInstance`）保存自己的 ID 和变换，通过 `std::span<const Triangle>` 只读访问 `GltfScene` 持有的三角形数据。多个实例共享这份数据，各自可以平移、旋转和缩放。

当模型省略 `NORMAL` 时，Formlight 沿用 Rust 参照实现的平滑着色约定。每个图元内，共享顶点按相邻三角形面积加权生成法线，分开索引的边保留硬边。glTF 2.0 对缺失法线规定了逐面平坦着色，因此共享顶点的折面在两种约定下外观会有差异。[glTF 2.0 网格几何规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview)

材质可通过 `KHR_texture_transform` 为每个纹理绑定单独选择 UV0 或 UV1，然后依次缩放、旋转和平移。缩小过滤按变换后的纹理足迹选择 mip 层，镂空材质的主画面和投影也使用变换后的 UV。[Khronos 扩展说明](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform)

### 场景

| 想观察什么 | 从哪个场景开始 |
| --- | --- |
| 方盒的灯光与阴影 | 在 [lighting](scenes/lighting/) 中，从[方向光硬阴影](scenes/lighting/box-directional.toml)或[矩形灯软阴影](scenes/lighting/box-area-light.toml)开始。 |
| 材质、纹理与透明层 | 在 [materials](scenes/materials/) 中，用[材质球](scenes/materials/material-spheres.toml)比较金属度与粗糙度，用[交叠透明层](scenes/materials/blend-layers.toml)观察遮挡和混合。 |
| 漫反射间接光 | 用 [Cornell 漫反射房间](scenes/path/cornell-diffuse.toml)观察顶灯照亮白墙，以及红绿侧墙向墙角反射的颜色。 |

[方盒阴影对照](docs/box-shadows.md) 并排展示同机位的阴影图、阴影射线与矩形灯软阴影，并给出本影、半影、亮区的像素重放命令。

[双灯方盒](scenes/lighting/box-two-lights.toml) 用暖色方向光和冷色点光源照亮同一块地面。运行 `formlight-renderer --scene scenes/lighting/box-two-lights.toml --pixel 320 240` 可查看两盏灯各自的线性 RGB 贡献和遮挡结果。将某盏灯的 `enabled` 设为 `false` 可以单独观察另一盏灯。

场景文件用 `[lighting]` 保存全局环境光，每个 `[[lighting.lights]]` 定义一盏直接光源。迁移旧配置时，把方向光、点光源或矩形灯的 `[light]` 改为 `[[lighting.lights]]`，把 `ambient` 移到 `[lighting]`。纯环境光只需 `[lighting]` 和 `ambient`。省略整个照明配置会显示底色和自发光。

在 `raster` 模式下，可在方向光的 `[[lighting.lights]]` 中设置 `pcf = true`。渲染器在 3×3 个阴影纹素上分别比较深度，按可见比例缩放这盏灯的直接光。默认使用单纹素硬阴影。`--pixel X Y` 和 viewer 的像素诊断会显示边缘处的可见比例。`hybrid` 模式使用阴影射线。

CLI 和 viewer 都可以打开单文件 `.glb` 和多文件 `.gltf`。[双 UV 面板](scenes/materials/dual-uv-panel.toml) 的模型路径相对于 TOML，模型里的缓冲区和纹理路径相对于 `.gltf` 文件。将 TOML 另存到其他目录时，程序会重算模型路径。

默认每个像素采样一次。在 TOML 的 `[render]` 中设置 `ssaa = 2`，或在命令行加上 `--ssaa 2`，即可启用 2×2 超采样。四个子样本各自处理深度、镂空、光照和透明层，然后在线性空间取平均。用 `--pixel X Y` 可以查看每个子样本的位置、权重、命中表面和颜色，以及合成后的像素颜色。

### 路径追踪

在 CLI 加上 `--mode path`，或在 TOML 的 `[render]` 中设置 `mode = "path"`。积分器从相机发射主射线，经 BVH 取最近的可见表面。默认 `max_bounces = 0`，返回材质自发光与显式矩形灯的直接反射。未命中时环境为黑色。射线接受 OPAQUE 和通过阈值的 MASK，跳过 BLEND，按材质的单双面设置求交。光线命中纹理时使用第零层 mip。

运行 `formlight-renderer --scene scenes/materials/emissive-panels.toml --mode path --pixel 200 160`，可在背光的不透明面板上重放首命中，预期线性 RGB 为 (0.25, 0.125, 0.0625)。将像素参数换成 `--output build/release/path-emission.png` 可输出整图。[场景](scenes/materials/emissive-panels.toml) · [积分器](src/path/integrator.cppm)

运行 `formlight-renderer --scene scenes/lighting/box-area-light.toml --mode path --pixel 85 310` 可重放半影。种子 7 下，方盒挡住了 64 个灯面样本中的 35 个，地面的线性 RGB 三个通道均为 0.06451683。把模式切换为 `hybrid` 可逐样本核对灯面位置、遮挡物与直接光贡献。[方盒阴影对照](docs/box-shadows.md)

将 `max_bounces` 设为正数会从首命中继续追踪漫反射。每个相机样本平均 `path_samples` 条路径，按余弦加权的半球分布采样方向；Lambertian 反射的 BRDF 乘以余弦再除以采样密度，得到该表面的漫反射颜色。当前反弹按底色乘以非金属占比着色。固定 `seed` 可用 `--pixel` 重放同一组路径，报告中的 `Diffuse bounces` 为它们带回的线性 RGB。

Cornell 房间的唯一光源是向下发光的天花板网格。一次反弹能让白墙直接看见灯，三次反弹还会带回侧墙的颜色。在同一机位运行：

```powershell
$scene = "scenes/path/cornell-diffuse.toml"
build/release/formlight-renderer.exe --scene $scene --path-samples 2048 --max-bounces 1 --output build/release/cornell-direct.png
build/release/formlight-renderer.exe --scene $scene --path-samples 2048 --max-bounces 3 --output build/release/cornell-bounces.png
```

### 自定义光栅 shader

构建后运行 `build/release/custom_shader.exe build/release/custom-shader.png`，可看到近面裁剪后的渐变三角形，中间的条纹透出背景。[示例源码](examples/custom_shader.cpp)

导入 `formlight.shader`，把 shader 对象传给 `render_shader`。顶点函数返回网格局部位置和自定义 `Varying`，之后管线应用实例变换和相机投影。属性类型支持加法和乘以标量，光栅器用原三角形的透视校正权重插值。片元函数返回线性颜色时写入颜色和深度，返回 `std::nullopt` 时两者都保留原值。这个入口每像素取一个中心样本。[接口定义](src/raster/shader.cppm)

源码采用 [MIT 许可证](LICENSE)。方盒模型和标志的来源与许可见[素材说明](assets/README.md)。
