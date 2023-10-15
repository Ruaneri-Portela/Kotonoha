#define STRCPYFIX strcpy
#define COMPILE "Unknown"
#ifdef _MSC_VER
#define COMPILE "MSVC Compile Build"
#define STRCPYFIX strcpy_s
#elif defined(__GNUG__)
#define COMPILE "GNU Compile Build"
#endif