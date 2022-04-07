# DXR Ambient Occlusion Baking

A demo of ambient occlusion map baking using DXR inline ray tracing.
Uses [xatlas](https://github.com/jpcy/xatlas) to unwrap the mesh.
The mesh is rasterized into the atlas image by using the UV
coordinates as the output vertex positions, and the AO factor
computed by using inline ray tracing in the fragment shader.

## External Dependencies

Requires DXR 1.1 support for in line ray tracing.

- [SDL2](https://www.libsdl.org/download-2.0.php)

The easiest path to get SDL2 is through [vcpkg](). After installing SDL2
you should be able to build the app via:

```
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="<vcpkg install root>/scripts/buildsystems/vcpkg.cmake"
cmake --build . --config relwithdebinfo
```

If CMake doesn't find your SDL2 install you can point it to the root
of your SDL2 directory by passing `-DSDL2_DIR=<path>`. The app also uses
GLM, which will be automatically downloaded by CMake during the build process.


## Examples

Sponza:

![Baked AO map on Sponza](https://i.imgur.com/O3BSRJ9.png)

Suzanne:

![Baked AO map on Suzanne](https://i.imgur.com/998oHBO.png)

