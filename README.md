# Formlight Renderer

Formlight Renderer 是一个用 C++23 编写的 CPU 渲染器。下图是光栅化渲染的 glTF 纹理方盒 `BoxTextured`。

![CPU 光栅器渲染的方盒，三个可见面上都有 Cesium 标志](docs/images/box-textured-base-color.png)

同一张网格可以在场景中放置多次。每个实例（`MeshInstance`）保存自己的 ID 和变换，通过 `std::span<const Triangle>` 只读访问 `GltfScene` 持有的三角形数据。多个实例共享这份数据，各自可以平移、旋转和缩放。

[双灯方盒](scenes/box-two-lights.toml) 用暖色方向光和冷色点光源照亮同一块地面。运行 `formlight-renderer --scene scenes/box-two-lights.toml --pixel 320 240` 可查看两盏灯各自的线性 RGB 贡献和遮挡结果。将某盏灯的 `enabled` 设为 `false` 可以单独观察另一盏灯。

场景文件用 `[lighting]` 保存全局环境光，每个 `[[lighting.lights]]` 定义一盏直接光源。迁移旧配置时，把方向光、点光源或矩形灯的 `[light]` 改为 `[[lighting.lights]]`，把 `ambient` 移到 `[lighting]`。纯环境光只需 `[lighting]` 和 `ambient`。省略整个照明配置会显示底色和自发光。

源码采用 [MIT 许可证](LICENSE)。方盒模型和标志的来源与许可见[素材说明](assets/README.md)。
