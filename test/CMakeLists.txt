project(WebP_Test)

add_executable(${PROJECT_NAME}
    main.cpp
    ${CMAKE_SOURCE_DIR}/plugin/WebPlugin.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    WebPCodec
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_BINARY_DIR}/plugin
)

target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Test file generator — run once to produce test/perf.webp
add_executable(WebP_Gen
    gen_perf_webp.cpp
)

target_link_libraries(WebP_Gen PRIVATE
    webp
    libwebpmux
)

install(TARGETS WebP_Gen
    RUNTIME DESTINATION bin
)
