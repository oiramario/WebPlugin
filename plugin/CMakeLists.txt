# -- plugin/CMakeLists.txt - ID_WebP.apl (ACDSee plug-in DLL) -------------
#
# Produces a shared library with .apl extension. Links against WebPCodec
# (the static codec wrapper) and Windows GDI32 / USER32 implicitly.
#

project(ID_WebP)

add_library(${PROJECT_NAME} SHARED
    dllmain.cpp
    ID_WebP.cpp
    WebPlugin.cpp
    res/resources.rc
    ID_WebP.def
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    WebPCodec
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${CMAKE_PROJECT_VERSION}
    SUFFIX  ".apl"
)
