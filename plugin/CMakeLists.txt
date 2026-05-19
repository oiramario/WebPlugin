project(ID_WebP)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/res/resource.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/res/resource.h
    @ONLY
)

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
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/avir
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${CMAKE_PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "ID_WebP"
    SUFFIX ".apl"
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
