project(WebPCodec VERSION 1.0.0)

add_library(${PROJECT_NAME} STATIC
    WebPDecoder.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    webp
    webpdemux
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(FILES WebPDecoder.h DESTINATION include)
