# PICOSplat: Open-Source 3DGS Rendering Components

This repository contains the open-source components of the [PICOSplat Unreal Engine Plugin](https://www.fab.com/listings/a7e35c41-592d-493d-bbf6-2d048f398c1e).
For the full source code to the Unreal Engine plugin, see [splat_unreal](https://github.com/Pico-Developer/splat_unreal).

This project is comprised of two independent pieces, organized by subdirectory:

- `import`: C++ PLY Importer

  This module contains functionality for parsing 3DGS `.ply` files and converting them into runtime formats.

  Interfaces are provided as templates or with callbacks in order to easily integrate with different engines.

- `shaders`: HLSL Shaders for 3DGS Rendering

  This modules includes all of the shader logic needed to draw 3DGS scenes.
  These shaders are optimized for real-time rasterization of splats on mobile GPUs, such as that of the PICO 4 Ultra.
  They are extensively commented in order to act as a reference for 3DGS rendering, and to clarify any non-obvious optimizations made.

  In order to be usable across many different possible rendering pipelines, these shaders do not include all necessary constants, defines, helper functions and includes.
  Instead, each shader will list any necessary definitions needed in a header comment.
