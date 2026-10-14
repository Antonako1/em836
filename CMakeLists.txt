cmake_minimum_required(VERSION 3.20)

project(em836 LANGUAGES C CXX)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

# Dependencies
set(UNICORN_ROOT "C:/unicorn" CACHE PATH "Unicorn installation")
set(SDL3_ROOT "C:/sdl3" CACHE PATH "SDL3 installation")
set(CAPSTONE_ROOT "C:/capstone" CACHE PATH "Capstone installation")
set(IMGUI_ROOT "C:/imgui" CACHE PATH "Dear ImGui source directory")

# Unicorn headers
find_path(UNICORN_INCLUDE_DIR
    NAMES unicorn/unicorn.h
    HINTS "${UNICORN_ROOT}/include"
)

# Unicorn library
find_library(UNICORN_LIBRARY_DEBUG
    NAMES unicorn unicorn_static
    PATHS "${UNICORN_ROOT}/Debug"
    NO_DEFAULT_PATH
)

find_library(UNICORN_LIBRARY_RELEASE
    NAMES unicorn unicorn_static
    PATHS
        "${UNICORN_ROOT}/Release"
        "${UNICORN_ROOT}/lib"
        "${UNICORN_ROOT}/lib/x64"
        "${UNICORN_ROOT}/build/Release"
    NO_DEFAULT_PATH
)

if(UNICORN_LIBRARY_DEBUG)
    set(UNICORN_LIBRARY
        "$<$<CONFIG:Debug>:${UNICORN_LIBRARY_DEBUG}>"
        "$<$<NOT:$<CONFIG:Debug>>:${UNICORN_LIBRARY_RELEASE}>"
    )
else()
    message(FATAL_ERROR
        "Unicorn Debug library not found in ${UNICORN_ROOT}/Debug")
endif()

# SDL3 headers
find_path(SDL3_INCLUDE_DIR
    NAMES SDL3/SDL.h
    HINTS
        "${SDL3_ROOT}/include"
        "${SDL3_ROOT}"
)

# SDL3 library
find_library(SDL3_LIBRARY
    NAMES SDL3
    HINTS
        "${SDL3_ROOT}/lib"
        "${SDL3_ROOT}/lib/x64"
        "${SDL3_ROOT}/lib/x86"
        "${SDL3_ROOT}/lib/Release"
        "${SDL3_ROOT}/lib/Debug"
)

# Capstone headers
find_path(CAPSTONE_INCLUDE_DIR
    NAMES capstone/capstone.h
    HINTS
        "${CAPSTONE_ROOT}/include"
        "${CAPSTONE_ROOT}"
    PATH_SUFFIXES include
)

# Capstone library
find_library(CAPSTONE_LIBRARY
    NAMES capstone capstone_static
    HINTS
        "${CAPSTONE_ROOT}/lib"
        "${CAPSTONE_ROOT}/lib/x64"
        "${CAPSTONE_ROOT}/lib/x86"
        "${CAPSTONE_ROOT}/build"
        "${CAPSTONE_ROOT}/build/Release"
)

if(NOT UNICORN_INCLUDE_DIR OR NOT UNICORN_LIBRARY)
    message(FATAL_ERROR
        "Unicorn not found. Check C:/unicorn and its include/lib folders.")
endif()

if(NOT SDL3_INCLUDE_DIR OR NOT SDL3_LIBRARY)
    message(FATAL_ERROR
        "SDL3 not found. Check C:/sdl3 and its include/lib folders.")
endif()

if(NOT CAPSTONE_INCLUDE_DIR OR NOT CAPSTONE_LIBRARY)
    message(FATAL_ERROR
        "Capstone not found. Check C:/capstone and its include/lib folders.")
endif()

message(STATUS "Unicorn headers: ${UNICORN_INCLUDE_DIR}")
message(STATUS "Unicorn library: ${UNICORN_LIBRARY}")
message(STATUS "SDL3 library:   ${SDL3_LIBRARY}")
message(STATUS "Capstone library: ${CAPSTONE_LIBRARY}")

add_executable(em836
    src/main.cpp
    src/emulator.cpp
    src/syscalls.cpp
    src/vga.cpp
    src/console.cpp
    src/astrac.cpp
    src/config.cpp
    src/gui/gui_manager.cpp
    src/gui/gui_registers.cpp
    src/gui/gui_execution.cpp
    src/gui/gui_disassembly.cpp
    src/gui/gui_memory.cpp
    src/gui/gui_vga.cpp
    src/gui/gui_console.cpp
    src/gui/gui_editor.cpp
    src/gui/gui_modals.cpp

    # Dear ImGui core
    "${IMGUI_ROOT}/imgui.cpp"
    "${IMGUI_ROOT}/imgui_draw.cpp"
    "${IMGUI_ROOT}/imgui_tables.cpp"
    "${IMGUI_ROOT}/imgui_widgets.cpp"

    # SDL3 platform and renderer backends
    "${IMGUI_ROOT}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_ROOT}/backends/imgui_impl_sdlrenderer3.cpp"
)

target_include_directories(em836 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${UNICORN_INCLUDE_DIR}"
    "${SDL3_INCLUDE_DIR}"
    "${CAPSTONE_INCLUDE_DIR}"
    "${IMGUI_ROOT}"
    "${IMGUI_ROOT}/backends"
)

target_link_libraries(em836 PRIVATE
    "${UNICORN_LIBRARY}"
    "${SDL3_LIBRARY}"
    "${CAPSTONE_LIBRARY}"
)

if(MSVC)
    target_compile_options(em836 PRIVATE /EHsc /Zi)
    target_link_options(em836 PRIVATE /DEBUG)
endif()

if(WIN32)
    # Copy SDL3 runtime
    if(EXISTS "${SDL3_ROOT}/lib/x64/SDL3.dll")
        add_custom_command(TARGET em836 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL3_ROOT}/lib/x64/SDL3.dll"
                "$<TARGET_FILE_DIR:em836>"
            COMMENT "Copying SDL3 runtime"
            VERBATIM
        )
    endif()

    # Copy Unicorn runtime if dynamic
    if(EXISTS "${UNICORN_ROOT}/Debug/unicorn.dll")
        add_custom_command(TARGET em836 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UNICORN_ROOT}/Debug/unicorn.dll"
                "$<TARGET_FILE_DIR:em836>"
            COMMENT "Copying Unicorn runtime"
            VERBATIM
        )
    elseif(EXISTS "${UNICORN_ROOT}/bin/unicorn.dll")
        add_custom_command(TARGET em836 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UNICORN_ROOT}/bin/unicorn.dll"
                "$<TARGET_FILE_DIR:em836>"
            COMMENT "Copying Unicorn runtime"
            VERBATIM
        )
    endif()
if(EXISTS "${UNICORN_ROOT}/Debug")
    file(GLOB UNICORN_PDB_FILES
        "${UNICORN_ROOT}/Debug/*.pdb"
    )

    if(UNICORN_PDB_FILES)
        add_custom_command(TARGET em836 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${UNICORN_PDB_FILES}
                "$<TARGET_FILE_DIR:em836>"
            COMMENT "Copying Unicorn PDB debug symbols"
            VERBATIM
        )
    endif()
endif()

    # Copy Capstone runtime if dynamic
    if(EXISTS "${CAPSTONE_ROOT}/bin/capstone.dll")
        add_custom_command(TARGET em836 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CAPSTONE_ROOT}/bin/capstone.dll"
                "$<TARGET_FILE_DIR:em836>"
            COMMENT "Copying Capstone runtime"
            VERBATIM
        )
    endif()
endif()