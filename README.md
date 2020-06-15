# DXR Ambient Occlusion Baking

A demo of ambient occlusion map baking using DXR inline ray tracing.
Uses [xatlas](https://github.com/jpcy/xatlas) to unwrap the mesh.
The mesh is rasterized into the atlas image by using the UV
coordinates as the output vertex positions, and the AO factor
computed by using inline ray tracing in the fragment shader.

## Dependencies

Requires DXR 1.1 support for in line ray tracing.

- [SDL2](https://www.libsdl.org/download-2.0.php)
- [glm](https://github.com/g-truc/glm)


## Examples

Sponza:

![Baked AO map on Sponza](https://i.imgur.com/O3BSRJ9.png)

Suzanne:

![Baked AO map on Suzanne](https://i.imgur.com/998oHBO.png)

