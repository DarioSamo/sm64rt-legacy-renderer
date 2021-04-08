## RT64

RT64 is a hardware-accelerated raytracing real-time renderer intended to recreate the visual style of promotional offline renders of the mid 90s to early 2000s. It's currently licensed under the terms of the MIT license.

This library is not meant to be used in the traditional way of linking it as an static or dynamic library. It can be loaded during runtime by other processes as long as they can include the basic C-style header and call the provided function pointers. This is mostly meant for ease of use as it allows to run this renderer without having to port it to the build system used by the host application.

[sm64rtx](https://github.com/DarioSamo/sm64rtx) makes heavy use of this library, and its reliance on MinGW presented some problems when making D3D12 code that uses the latest raytracing features. This design allows both projects to communicate without issue.

### Current support
* D3D12 backend for Windows 10

### Future support
* Vulkan backend for both Windows 10 and Linux

### Requirements
* Visual Studio Community 2019 Version 16.9.0 or newer
* Windows SDK 10.0.18362.0 or newer

### Building
Open **src/RT64.sln** in **Visual Studio Community 2019** and build the solution.

A sample is included to showcase how to use the renderer library.

### Screenshot
![Sample screenshot](/images/screen1.jpg?raw=true)
