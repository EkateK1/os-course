add_library(
    vtpc
    STATIC
    vtpc.c
    cache.c
)

target_include_directories(
    vtpc
    PUBLIC
    .
)
