# Formlight Renderer

Formlight Renderer 是一个用 C++23 编写的 CPU 渲染器。下图是光栅化渲染的 glTF 纹理方盒 `BoxTextured`。

![CPU 光栅器渲染的方盒，三个可见面上都有 Cesium 标志](docs/images/box-textured-base-color.png)

导入 glTF 时，三角形列表、三角带和三角扇都会展开为独立三角形。同一张网格可以在场景中放置多次。每个实例（`MeshInstance`）保存自己的 ID 和变换，通过 `std::span<const Triangle>` 只读访问 `GltfScene` 持有的三角形数据。多个实例共享这份数据，各自可以平移、旋转和缩放。

当模型省略 `NORMAL` 时，Formlight 沿用 Rust 参照实现的平滑着色约定。每个图元内，共享顶点按相邻三角形面积加权生成法线，分开索引的边保留硬边。glTF 2.0 对缺失法线规定了逐面平坦着色，因此共享顶点的折面在两种约定下外观会有差异。[glTF 2.0 网格几何规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview)

材质可通过 `KHR_texture_transform` 为每个纹理绑定单独选择 UV0 或 UV1，然后依次缩放、旋转和平移。缩小过滤按变换后的纹理足迹选择 mip 层，镂空材质的主画面和投影也使用变换后的 UV。[Khronos 扩展说明](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform)

[双灯方盒](scenes/box-two-lights.toml) 用暖色方向光和冷色点光源照亮同一块地面。运行 `formlight-renderer --scene scenes/box-two-lights.toml --pixel 320 240` 可查看两盏灯各自的线性 RGB 贡献和遮挡结果。将某盏灯的 `enabled` 设为 `false` 可以单独观察另一盏灯。

场景文件用 `[lighting]` 保存全局环境光，每个 `[[lighting.lights]]` 定义一盏直接光源。迁移旧配置时，把方向光、点光源或矩形灯的 `[light]` 改为 `[[lighting.lights]]`，把 `ambient` 移到 `[lighting]`。纯环境光只需 `[lighting]` 和 `ambient`。省略整个照明配置会显示底色和自发光。

在 `raster` 模式下，可在方向光的 `[[lighting.lights]]` 中设置 `pcf = true`。渲染器在 3×3 个阴影纹素上分别比较深度，按可见比例缩放这盏灯的直接光。默认使用单纹素硬阴影。`--pixel X Y` 和 viewer 的像素诊断会显示边缘处的可见比例。`hybrid` 模式使用阴影射线。

CLI 和 viewer 都可以打开单文件 `.glb` 和多文件 `.gltf`。[双 UV 面板](scenes/dual-uv-panel.toml) 的模型路径相对于 TOML，模型里的缓冲区和纹理路径相对于 `.gltf` 文件。将 TOML 另存到其他目录时，程序会重算模型路径。

默认每个像素采样一次。在 TOML 的 `[render]` 中设置 `ssaa = 2`，或在命令行加上 `--ssaa 2`，即可启用 2×2 超采样。四个子样本各自处理深度、镂空、光照和透明层，然后在线性空间取平均。用 `--pixel X Y` 可以查看每个子样本的位置、权重、命中表面和颜色，以及合成后的像素颜色。

源码采用 [MIT 许可证](LICENSE)。方盒模型和标志的来源与许可见[素材说明](assets/README.md)。
