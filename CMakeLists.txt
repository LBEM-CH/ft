cmake_minimum_required(VERSION 3.16)
project(ft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# When FT_EMBEDDED is ON, only the ft_core library target is built so that
# another Qt application (e.g. the 4d STEM app) can link FtWindow in directly.
# Standalone builds (default) are unchanged and produce the ft executable.
option(FT_EMBEDDED "Build ft as a library for embedding into another Qt application" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(FT_CORE_SOURCES
    ftwindow.cpp
    ftwindow_mouse.cpp
    ftwindow_paint.cpp
    ftwindow_draw.cpp
    ftwindow_slots.cpp
    ftwindow_amyloid.cpp
    fft.cpp
    mrcloader.cpp
)

add_library(ft_core STATIC ${FT_CORE_SOURCES})
target_include_directories(ft_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ft_core PUBLIC Qt6::Widgets)
if(EMSCRIPTEN)
    # Enable C++ exceptions so a failed allocation (with ABORTING_MALLOC=0,
    # see below) surfaces as std::bad_alloc that the load code can catch and
    # report, instead of throwing through code that cannot unwind. PUBLIC so
    # the flag propagates to the ft executable that links this library.
    target_compile_options(ft_core PUBLIC -fexceptions)
endif()
if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(ft_core PUBLIC Threads::Threads)
endif()

if(NOT FT_EMBEDDED)
    add_executable(ft main.cpp)
    target_link_libraries(ft PRIVATE ft_core)
    set_target_properties(ft PROPERTIES
        WIN32_EXECUTABLE ON
    )

    if(EMSCRIPTEN)
        # ---- WebAssembly build ----
        target_link_options(ft PRIVATE
            -fexceptions
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=256MB
            # 32-bit WASM can address up to 4 GB; raise the ceiling from the
            # previous 2 GB so more / larger images fit before the heap is
            # exhausted.
            -sMAXIMUM_MEMORY=4GB
            # Do not hard-abort ("Application Exit") when an allocation fails.
            # malloc returns null and operator new throws std::bad_alloc, which
            # the load code catches to show a graceful out-of-memory message.
            -sABORTING_MALLOC=0
            -sMODULARIZE=1
            -sEXPORT_NAME=createQtAppInstance
            "-sEXPORTED_RUNTIME_METHODS=UTF16ToString,stringToUTF16,JSEvents,specialHTMLTargets,FS,callMain,ccall,cwrap"
            -lembind
        )
        # Optionally enable pthreads (requires COOP/COEP headers on the server):
        # target_compile_options(ft PRIVATE -pthread)
        # target_link_options(ft PRIVATE -pthread -sPTHREAD_POOL_SIZE=4)
    endif()
endif()
