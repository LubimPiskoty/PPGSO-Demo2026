// stb_image's implementation in its own file, so CMakeLists.txt can build it
// optimized even in Debug (PNG decoding at -O0 made startup take ~20 s)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
