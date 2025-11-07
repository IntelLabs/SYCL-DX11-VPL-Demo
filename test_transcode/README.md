# Universal VPL codec wrapper test application
The app uses VPLCodec class to perform decode, decode-vpp (cropping | scaling) and encode functions by processign multiple video streams in parallem in multiple threads.

Building: 
1. Use CMake in top-level dir to generate sln/vcxproj files (newer vpl version provided in libs directory)
2. Open solution in VS2022
3. Manually configure test_transcode project for DPC++:
	a) right-click test_transcode and set it to use Intel Compiler 2025
	b) in test_transcode Properties select All Cofiguartions, and then set Properties->General->Platform toolset to Intel oneAPI DPC++ compiler 2025 (or 2024)
	c) in Properties window select Debug configuration and set Properties->DPC++->Optimization->Set optimization->Disabled

Running: The app takes a directory with collections of specifically named jpg or hevc files. Decodes them performing vpp cropping/scaling if requested and encoding resulting frames into jpg or hevc streams (and if requested) storing them to files named output_*.* located in the inout directory.
The app takes following main parameters:
data_dir_path - first parameter, directory with input files
-Nf=<number of frames to process>
-inputHEVC - use hevc streams as input, otherwise search for jpg files
-finalW=<output width in pixels>
-finalH=<output height in pixels>
-Crop - use cropping to get finalW/finalH, otherwise use scaling
-encode - encode decoder/vpp output (transcode), see next option for encoder selection
-encodeJPEG - encode to jpg otherwise encode to HEVC. Effective only if -encode is specified.

Example:
test_transcode ..\..\data -Nf=100 -finalW=1440 -finalH=720 -Crop -encode -encodeJPEG
