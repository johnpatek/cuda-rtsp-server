find_package(PkgConfig REQUIRED)
pkg_search_module(GLU REQUIRED IMPORTED_TARGET glu)

add_executable(example main.c egl.h egl.c)

add_subdirectory(sigfn)

add_subdirectory(eglext-loader)

target_link_libraries(example PkgConfig::GLU cudartsp m sigfn eglextloader)