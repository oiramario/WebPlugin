project(WebP_Test)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    WebPCodec
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/avir
)

target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
