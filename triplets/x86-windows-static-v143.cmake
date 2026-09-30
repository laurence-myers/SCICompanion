# The triplet of the build: x86, static C runtime and static libraries, and
# the v143 toolset of the projects. With the newest toolset, a library that
# vcpkg compiles needs STL functions that the v143 runtime does not have.
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v143)
