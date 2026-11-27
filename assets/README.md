# Assets

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

A square panel uses one two-texel PNG for both base color and metallic-roughness. The RGB bytes are (32, 64, 192) and (224, 160, 48). Both material factors are 0.5, and the sampler uses linear filtering. Created for Formlight and distributed under the [project license](../LICENSE).

## Viewer font

The viewer uses the bundled Noto Sans SC Regular 2.004 for both Chinese and English. This static OpenType font is distributed under the SIL Open Font License 1.1. [Upstream font](https://github.com/notofonts/noto-cjk/blob/Sans2.004/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf) · [Font license](fonts/OFL.txt)
