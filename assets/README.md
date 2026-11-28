# Assets

## Gallery image credits

Formlight rendered the images in the [model gallery](../README.md#模型预览). The six-model contact sheet and the before/after details use cropped and resized output. The pawn comparison also includes a render with the glass thickness set to zero.

| Model | Credit and terms |
| --- | --- |
| Car Concept | Model and textures by Eric Chadwick, © 2024 Darmstadt Graphics Group GmbH, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The Khronos and 3D Commerce marks have separate terms. [Source and mark notices](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CarConcept#legal) |
| A Beautiful Game | Original model by the MaterialX Project, © 2020 ASWF. glTF conversion © 2022 Ed Mackey. Both under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). [Source](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/ABeautifulGame#legal) |
| Diffuse Transmission Plant | Original asset by Rico Cilliers (2021), [CC0](https://creativecommons.org/publicdomain/zero/1.0/). Materials and animation by Eric Chadwick, © 2024 Darmstadt Graphics Group GmbH, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). [Source](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DiffuseTransmissionPlant#legal) |
| Old Rusty Car | Renafox, [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/). [Source](https://sketchfab.com/3d-models/old-rusty-car-95baa20ebc5d4d2e869f0b549be838fe) |
| CCity Building Set 1 | Neberkenezer, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). [Source](https://sketchfab.com/3d-models/ccity-building-set-1-a2d5c7bfcc2148fb8994864c43dfcc97) |
| Damaged Helmet | glTF rebuild and conversion © 2018 ctxwing, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Earlier model © 2016 theblueturtle\_, [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/). [Source](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet#legal) |
| TransmissionTest | Adobe (2020), [CC0](https://creativecommons.org/publicdomain/zero/1.0/). [Source](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/TransmissionTest#legal) |
| CompareIor | Eric Chadwick and DGG (2024), [CC0](https://creativecommons.org/publicdomain/zero/1.0/). The glTF mark has separate terms. [Source and mark notice](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CompareIor#legal) |

## BoxTextured.glb

Box Textured, © 2017 Cesium, distributed under [CC BY 4.0](licenses/CC-BY-4.0.txt). This is an unmodified copy from the [Khronos glTF Sample Assets collection](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/BoxTextured).

The texture contains the Cesium logo, © 2015 Cesium. See the [Cesium mark notice](licenses/LicenseRef-LegalMark-Cesium.txt).

## MaskPanel.glb

A perforated panel stands in front of an ochre block. Its embedded texture, vertex alpha and material factor multiply to exactly the cutoff on the solid part of the panel. The holes fall below it. Created for Formlight and distributed under the [project license](../LICENSE).

## BlendLayers.glb

Two intersecting, half-opacity sheets change depth order across the image. An opaque block passes between them. The coral sheet gets its alpha from the material and the blue sheet from its vertices. Created for Formlight and distributed under the [project license](../LICENSE).

## EmissivePanels.glb

A half-opacity sheet covers the right half of an amber panel. Its two emissive texels have different RGB and alpha values. The sheet uses `KHR_materials_emissive_strength` to multiply emission by four. In the backlit example, the cyan texel exceeds 1 in linear RGB before compositing, and Reinhard tone mapping preserves the difference between its green and blue channels. Created for Formlight and distributed under the [project license](../LICENSE).

## MaterialSpheres.glb

Six spheres share one untextured geometry and the same copper-colored base. The top row is metallic and the bottom row is dielectric. Roughness increases from 0.2 to 0.5 to 1 across each row. The rightmost metal uses the glTF defaults for both factors. Created for Formlight and distributed under the [project license](../LICENSE).

## MetallicRoughnessPanel.glb

A square panel uses one two-texel PNG for base color, metallic-roughness, and ambient occlusion. The RGB bytes are (32, 64, 192) and (224, 160, 48). The metallic and roughness factors are both 0.5. The red channel controls occlusion with strength 0.25. The sampler uses linear filtering. The [ambient lighting scene](../scenes/materials/occlusion-panel.toml) shows the panel under uniform colored light. Created for Formlight and distributed under the [project license](../LICENSE).

## DualUvPanel.glb

This variant of MetallicRoughnessPanel uses the same two texels for all four material bindings. UV0 runs left to right. UV1 mirrors U and uses normalized unsigned-short coordinates. Base color (UV1) and emission (UV0) use linear filtering. Metallic-roughness (UV0) and occlusion (UV1) use nearest filtering. Created for Formlight and distributed under the [project license](../LICENSE).

The [multi-file version](DualUvPanel/DualUvPanel.gltf) uses the same mesh and PNG, stored in separate subdirectories. Its image URI uses `%20` for the space in the filename.

## MipChecker.glb

A floor uses UV1 to repeat a 64×64 checker at 32 texels per world unit. UV0 is constant. The same image supplies sRGB base color and linear metallic-roughness data. The sampler uses nearest magnification and trilinear minification. In the [oblique camera view](../scenes/materials/mip-checker.toml), distant squares converge to the average of the decoded texels. Created for Formlight and distributed under the [project license](../LICENSE).

## NormalPanels.glb

The [normal-map scene](../scenes/materials/normal-panels.toml) pairs authored tangents (left) with MikkTSpace tangents (right). The lower pair shares a mirrored, nonuniform scale; the center triangle supplies neither normals nor tangents. UV0 is constant. UV1 has negative handedness and samples a two-texel normal map with scale 0.5. Occlusion reads the same image as scalar data. Created for Formlight and distributed under the [project license](../LICENSE).

## ColorBleedRoom.gltf

Five inward-facing walls share one four-vertex triangle strip. The [scene](../scenes/lighting/color-bleed-room.toml) lights the room with a single analytical rectangle to compare direct light and color bleeding. Created for Formlight and distributed under the [project license](../LICENSE).

## CornellDiffuse.gltf

An open Cornell-style room with red and green side walls, a rotated white block, and a downward-facing emissive ceiling patch. The geometry and embedded vertex buffer were created for Formlight and are distributed under the [project license](../LICENSE). The [diffuse scene](../scenes/path/cornell-diffuse.toml) uses this mesh light to measure color bleeding.

## CornellGlass.gltf

The same room with a closed, smooth-shaded icosphere in front of the block. The sphere has its own glTF material (index 4). The [glass scene](../scenes/path/cornell-glass.toml) assigns the dielectric and its refractive index. The mesh and embedded buffers were created for Formlight under the [project license](../LICENSE).

## earthmap.jpg

The Earth scene uses the same JPEG as the Rust reference renderer. Its SHA-256 matches the file supplied with *Ray Tracing: The Next Week*. [Upstream image](https://raytracing.github.io/images/earthmap.jpg) · [Book example](https://raytracing.github.io/books/RayTracingTheNextWeek.html#imagetexturemapping/renderingtheimagetexture)

## Viewer font

The viewer uses the bundled Noto Sans SC Regular 2.004 for both Chinese and English. This static OpenType font is distributed under the SIL Open Font License 1.1. [Upstream font](https://github.com/notofonts/noto-cjk/blob/Sans2.004/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf) · [Font license](fonts/OFL.txt)
