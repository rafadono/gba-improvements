# GbaimpFindSDL2.cmake
#
# Locates SDL2 and exposes it as the INTERFACE target `gbaimp_sdl2`, which the
# library links against. Lookup order:
#   1. an already existing target (SDL2::SDL2, SDL2::SDL2-static, SDL2)
#   2. find_package(SDL2 CONFIG)          (vcpkg, SDL2's own cmake files, MSYS2)
#   3. find_package(SDL2)                 (module mode, e.g. FindSDL2.cmake)
#   4. pkg-config sdl2
#   5. SDL2_INCLUDE_DIR + SDL2_LIBRARY    (explicit cache variables; optionally
#                                          SDL2MAIN_LIBRARY)
#
# The library's headers include <SDL.h>. If the include directory found only
# contains the <SDL2/SDL.h> layout, the SDL2/ subdirectory is added too.
#
# Result variables: GBAIMP_SDL2_METHOD (which step succeeded).

include_guard(GLOBAL)

# Add `dir` and, if it only holds SDL2/SDL.h, `dir/SDL2` to `out_var` (a list).
function(_gbaimp_sdl2_normalize_includes out_var)
    set(_result "")
    foreach(_dir IN LISTS ARGN)
        if(NOT _dir)
            continue()
        endif()
        list(APPEND _result "${_dir}")
        if(NOT EXISTS "${_dir}/SDL.h" AND EXISTS "${_dir}/SDL2/SDL.h")
            list(APPEND _result "${_dir}/SDL2")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _result)
    set(${out_var} "${_result}" PARENT_SCOPE)
endfunction()

function(_gbaimp_sdl2_publish)
    # Arguments: LINK <items...> INCLUDE <dirs...>
    cmake_parse_arguments(_p "" "" "LINK;INCLUDE" ${ARGN})
    add_library(gbaimp_sdl2 INTERFACE IMPORTED GLOBAL)
    if(_p_LINK)
        set_property(TARGET gbaimp_sdl2 PROPERTY INTERFACE_LINK_LIBRARIES "${_p_LINK}")
    endif()
    if(_p_INCLUDE)
        _gbaimp_sdl2_normalize_includes(_inc ${_p_INCLUDE})
        set_property(TARGET gbaimp_sdl2 PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_inc}")
    endif()
endfunction()

if(TARGET gbaimp_sdl2)
    return()
endif()

set(GBAIMP_SDL2_METHOD "")

# 1. Existing target -----------------------------------------------------------
foreach(_t SDL2::SDL2 SDL2::SDL2-static SDL2)
    if(TARGET ${_t})
        # Guard against the plain name `SDL2` being something other than a library.
        get_target_property(_ty ${_t} TYPE)
        if(_ty)
            _gbaimp_sdl2_publish(LINK ${_t})
            set(GBAIMP_SDL2_METHOD "existing target ${_t}")
            break()
        endif()
    endif()
endforeach()

# 2. CONFIG package ------------------------------------------------------------
if(NOT GBAIMP_SDL2_METHOD)
    find_package(SDL2 QUIET CONFIG)
    if(TARGET SDL2::SDL2)
        _gbaimp_sdl2_publish(LINK SDL2::SDL2)
        set(GBAIMP_SDL2_METHOD "find_package(SDL2 CONFIG): SDL2::SDL2")
    elseif(TARGET SDL2::SDL2-static)
        _gbaimp_sdl2_publish(LINK SDL2::SDL2-static)
        set(GBAIMP_SDL2_METHOD "find_package(SDL2 CONFIG): SDL2::SDL2-static")
    elseif(SDL2_FOUND AND SDL2_LIBRARIES)
        # Older config files only provide variables.
        _gbaimp_sdl2_publish(LINK ${SDL2_LIBRARIES} INCLUDE ${SDL2_INCLUDE_DIRS})
        set(GBAIMP_SDL2_METHOD "find_package(SDL2 CONFIG): SDL2_LIBRARIES")
    endif()
endif()

# 3. MODULE package ------------------------------------------------------------
if(NOT GBAIMP_SDL2_METHOD)
    find_package(SDL2 QUIET MODULE)
    if(TARGET SDL2::SDL2)
        _gbaimp_sdl2_publish(LINK SDL2::SDL2)
        set(GBAIMP_SDL2_METHOD "find_package(SDL2) module: SDL2::SDL2")
    elseif(SDL2_FOUND AND SDL2_LIBRARIES)
        set(_inc ${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIR})
        _gbaimp_sdl2_publish(LINK ${SDL2_LIBRARIES} INCLUDE ${_inc})
        set(GBAIMP_SDL2_METHOD "find_package(SDL2) module: SDL2_LIBRARIES")
    endif()
endif()

# 4. pkg-config ----------------------------------------------------------------
if(NOT GBAIMP_SDL2_METHOD)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GBAIMP_PC_SDL2 QUIET IMPORTED_TARGET sdl2)
        if(TARGET PkgConfig::GBAIMP_PC_SDL2)
            # Re-expose so the <SDL2/SDL.h> include layout is normalised as well.
            get_target_property(_pc_inc PkgConfig::GBAIMP_PC_SDL2 INTERFACE_INCLUDE_DIRECTORIES)
            _gbaimp_sdl2_publish(LINK PkgConfig::GBAIMP_PC_SDL2 INCLUDE ${_pc_inc})
            set(GBAIMP_SDL2_METHOD "pkg-config sdl2")
        endif()
    endif()
endif()

# 5. Explicit cache variables --------------------------------------------------
if(NOT GBAIMP_SDL2_METHOD)
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        set(_link ${SDL2_LIBRARY})
        if(SDL2MAIN_LIBRARY)
            list(APPEND _link ${SDL2MAIN_LIBRARY})
        endif()
        _gbaimp_sdl2_publish(LINK ${_link} INCLUDE ${SDL2_INCLUDE_DIR})
        set(GBAIMP_SDL2_METHOD "SDL2_INCLUDE_DIR/SDL2_LIBRARY")
    endif()
endif()

if(NOT GBAIMP_SDL2_METHOD)
    message(FATAL_ERROR
        "gba-improvements: SDL2 development files were not found.\n"
        "Tried, in order: existing SDL2::SDL2 target, find_package(SDL2 CONFIG), "
        "find_package(SDL2) module mode, pkg-config sdl2, SDL2_INCLUDE_DIR/SDL2_LIBRARY.\n"
        "Fix it with one of:\n"
        "  * Debian/Ubuntu: sudo apt install libsdl2-dev\n"
        "  * vcpkg:         vcpkg install sdl2  and configure with "
        "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake\n"
        "  * SDL2 package:  -DSDL2_DIR=<sdl2>/cmake   (VC/MinGW development zip)\n"
        "  * Explicit:      -DSDL2_INCLUDE_DIR=<dir containing SDL.h> "
        "-DSDL2_LIBRARY=<path to SDL2 library>\n"
        "  * Or define the SDL2::SDL2 target in the parent project before add_subdirectory().")
endif()

message(STATUS "gba-improvements: SDL2 via ${GBAIMP_SDL2_METHOD}")
