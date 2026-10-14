# Stub gbarecomp: only the library targets the host build links against.
foreach(_lib gbarecomp_armv4t gbarecomp_gba gbarecomp_runtime gbarecomp_debug)
    add_library(${_lib} STATIC src/core.cpp)
endforeach()
