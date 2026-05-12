project(WebP_Test)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    WebPCodec
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
