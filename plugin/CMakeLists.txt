project(ID_WebP VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /source-charset:utf-8")

set(BUILD_SHARED_LIBS ON)
    
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(${PROJECT_NAME} SHARED 
    dllmain.cpp
    ID_WebP.cpp
    ID_APIWrapper.cpp
    res/resources.rc
    ID_WebP.def 
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    WebPCodec
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "ID_WebP"
    SUFFIX ".apl"
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
