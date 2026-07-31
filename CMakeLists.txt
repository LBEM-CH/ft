cmake_minimum_required(VERSION 3.16)
project(ft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# When FT_EMBEDDED is ON, only the ft_core library target is built so that
# another Qt application (e.g. the 4d STEM app) can link FtWindow in directly.
# Standalone builds (default) are unchanged and produce the ft executable.
option(FT_EMBEDDED "Build ft as a library for embedding into another Qt application" OFF)

# Network: the Help dialog's "Find in manual" downloads the online manual
# pages to search them for snippets.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(FT_CORE_SOURCES
    ftwindow.cpp
    ftwindow_mouse.cpp
    ftwindow_paint.cpp
    ftwindow_draw.cpp
    ftwindow_slots.cpp
    ftwindow_amyloid.cpp
    ftwindow_align.cpp
    fft.cpp
    mrcloader.cpp
)

add_library(ft_core STATIC ${FT_CORE_SOURCES})
target_include_directories(ft_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ft_core PUBLIC Qt6::Widgets Qt6::Network)
if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(ft_core PUBLIC Threads::Threads)
else()
    # WebAssembly multithreading. Compiling the core with -pthread makes
    # std::thread available and causes Emscripten to define
    # __EMSCRIPTEN_PTHREADS__, which FT_HAVE_THREADS (fft.h) keys on so the FFT
    # paths run multithreaded in the browser. This REQUIRES the Qt
    # "wasm_multithread" kit and COOP/COEP headers on the server (see
    # build_wasm.sh and ft-apache.conf). PUBLIC so the flag propagates to the
    # ft executable's own translation units.
    target_compile_options(ft_core PUBLIC -pthread)
endif()

if(NOT FT_EMBEDDED)
    add_executable(ft main.cpp)
    # Embed the app icon so main.cpp can set it as the window icon (shown in
    # the taskbar/dock and in the About dialog) instead of the default Qt logo.
    qt_add_resources(ft "ft_icon" PREFIX "/" FILES icon-ft.png)
    target_link_libraries(ft PRIVATE ft_core)
    set_target_properties(ft PROPERTIES
        WIN32_EXECUTABLE ON
    )

    if(EMSCRIPTEN)
        # ---- WebAssembly build ----
        target_link_options(ft PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=256MB
            # 32-bit WASM can address up to 4 GB; raise the ceiling from the
            # previous 2 GB so more / larger images fit before the heap is
            # exhausted.
            -sMAXIMUM_MEMORY=4GB
            # Make a failed malloc return null instead of hard-aborting
            # ("Application Exit"). The code probes allocations with malloc()
            # before large operations and refuses gracefully when memory is
            # short. (C++ exceptions are intentionally NOT enabled: this Qt
            # WASM kit has wasm-exceptions disabled, so -fexceptions would fall
            # back to slow JS-based exception handling across the whole app.)
            -sABORTING_MALLOC=0
            -sMODULARIZE=1
            -sEXPORT_NAME=createQtAppInstance
            "-sEXPORTED_RUNTIME_METHODS=UTF16ToString,stringToUTF16,JSEvents,specialHTMLTargets,FS,callMain,ccall,cwrap"
            -lembind
            # ---- Multithreading ----
            -pthread
            # Pre-warm a worker pool sized to the machine's logical CPUs. The
            # FFT spawns std::thread::hardware_concurrency() workers and joins
            # them immediately; if the (busy) main thread had to create those
            # workers on demand it would deadlock, so the pool must be at least
            # as large as the peak number of simultaneous threads. Browsers
            # report the same count via navigator.hardwareConcurrency, so this
            # matches hardware_concurrency() exactly.
            -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency
        )
    endif()
endif()
