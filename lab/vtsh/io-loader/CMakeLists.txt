add_executable(
    io-loader
    io-loader.c
)

target_link_libraries(
    io-loader
    PRIVATE
    vtpc
)

