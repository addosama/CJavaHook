cmake_minimum_required(VERSION 3.16)
project(JavaHookLib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Sources ----
set(JAVAHOOK_SOURCES
    src/JavaHook.cpp
    src/HotSpot.cpp
    src/Module.cpp
    src/Capstone.cpp
)

# ---- Public headers ----
set(JAVAHOOK_HEADERS
    include/JavaHook/JavaHook.h
    include/JavaHook/HotSpot.h
)

# ---- Internal headers ----
set(JAVAHOOK_PRIVATE_HEADERS
    include/JavaHook/Module.h
    include/JavaHook/Capstone.h
)

# ---- Static library target ----
add_library(JavaHookLib STATIC
    ${JAVAHOOK_SOURCES}
    ${JAVAHOOK_HEADERS}
    ${JAVAHOOK_PRIVATE_HEADERS}
)

target_include_directories(JavaHookLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/JavaHook
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---- External dependencies (consumer must provide these) ----
# JNI headers   → expect JNI_ROOT or JAVA_HOME pointing to JDK include dirs
# Capstone      → expect Capstone installed or capstone_ROOT
# Windows SDK   → linked implicitly on MSVC

find_package(JNI QUIET)
if(JNI_FOUND)
    target_include_directories(JavaHookLib PUBLIC ${JNI_INCLUDE_DIRS})
    target_link_libraries(JavaHookLib PUBLIC ${JNI_LIBRARIES})
else()
    message(STATUS "JNI not found via find_package — ensure JNI include dirs are on the include path")
endif()

# Capstone (optional; if not found, define CAPSTONE_INCLUDE_DIR and CAPSTONE_LIBRARY manually)
find_path(CAPSTONE_INCLUDE_DIR capstone/capstone.h)
find_library(CAPSTONE_LIBRARY capstone)
if(CAPSTONE_INCLUDE_DIR AND CAPSTONE_LIBRARY)
    target_include_directories(JavaHookLib PRIVATE ${CAPSTONE_INCLUDE_DIR})
    target_link_libraries(JavaHookLib PRIVATE ${CAPSTONE_LIBRARY})
else()
    message(STATUS "Capstone not found — set CAPSTONE_INCLUDE_DIR and CAPSTONE_LIBRARY if needed")
endif()

# Windows libraries
if(WIN32)
    target_link_libraries(JavaHookLib PRIVATE psapi)
endif()

# ---- Install rules ----
include(GNUInstallDirs)
install(TARGETS JavaHookLib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES ${JAVAHOOK_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/JavaHook
)
