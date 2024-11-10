#pragma once

#ifdef _WIN64
#pragma warning(push)
#pragma warning(disable : 4127)
#pragma warning(disable : 4819)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#endif
#include <opencv2/opencv.hpp>
#ifdef _WIN64
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

#ifdef _WIN64
#ifdef _DEBUG
#pragma comment(lib, "opencv_world4100d.lib")
#else
#pragma comment(lib, "opencv_world4100.lib")
#endif
#endif