cmake_minimum_required(VERSION 3.16)
project(ft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(ft
    main.cpp
    ftwindow.cpp
    ftwindow_mouse.cpp
    ftwindow_paint.cpp
    ftwindow_draw.cpp
    ftwindow_slots.cpp
    fft.cpp
    mrcloader.cpp
)
set_target_properties(ft PROPERTIES
    WIN32_EXECUTABLE ON
)

if(EMSCRIPTEN)
    # ---- WebAssembly build ----
    target_link_libraries(ft PRIVATE Qt6::Widgets)
    target_link_options(ft PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=256MB
        -sMAXIMUM_MEMORY=2GB
        -sMODULARIZE=1
        -sEXPORT_NAME=createQtAppInstance
        "-sEXPORTED_RUNTIME_METHODS=UTF16ToString,stringToUTF16,JSEvents,specialHTMLTargets,FS,callMain,ccall,cwrap"
        -lembind
    )
    # Optionally enable pthreads (requires COOP/COEP headers on the server):
    # target_compile_options(ft PRIVATE -pthread)
    # target_link_options(ft PRIVATE -pthread -sPTHREAD_POOL_SIZE=4)
else()
    # ---- Native desktop build (macOS / Ubuntu) ----
    find_package(Threads REQUIRED)
    target_link_libraries(ft PRIVATE Qt6::Widgets Threads::Threads)
endif()
