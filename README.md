> [!WARNING]
> **THIS PROJECT IS ARCHIVED**  
> Intel will not provide or guarantee development of or support for this project,
> including but not limited to, maintenance, bug fixes, new releases or updates.
>
> Patches to this project are no longer accepted by Intel.  

VPL Decode->DPC++ Processing-> DX11 Render Example Project
===
![GitHub License](https://img.shields.io/github/license/IntelLabs/SYCL-DX11-VPL-Demo)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/IntelLabs/SYCL-DX11-VPL-Demo/badge)](https://scorecard.dev/viewer/?uri=github.com/IntelLabs/SYCL-DX11-VPL-Demo)  

```NOTE: This codebase uses Git LFS for meadia files```

## Requirements: 
- Visual Studio 2022
- oneAPI Base Toolkit 2024/2025
- CMake
- Intel GPU

## External dependencies: 
- OpenCV 4.8+
- Intel VPL dispatcher library (libvpl): https://github.com/intel/libvpl
- Intel Level-Zero headers and loader: https://github.com/oneapi-src/level-zero

## Building the code
 *using CMakeLists.txt in the root of the project*  

1. Make sure external dependencies are built and installed in desired locations 
2. CMake configure: set paths to installed external dependencies
3. Generate VS2022 solution/project files
4. Open generated solution and set the following projects to be build with Intel DPC++ 2024/2025: main, sycl-dx11-vpl, test_transcode.
5. Set main as a startup project

## Running the Demo
```Test video is located in 'content' subdirectory```

Running:  
	```main.exe --i \<path to a raw hvec video\>```  

Example:  
  ```main.exe --i ..\..\content\quiltB9F-07_4k.hevc```

Controls:
 p - toggle image processing with SYCL on GPU (sobel edge detection overlayed on top of the source image)
 ESC - exit
