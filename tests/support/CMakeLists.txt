set(SUPPORT_SRC
    src/fixtures.cc
    src/service_helpers.cc
)

add_library(zero_test_support STATIC ${SUPPORT_SRC})

target_include_directories(zero_test_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(zero_test_support
    PUBLIC
        zero
        GTest::gtest
        GTest::gmock
)

# Redefine __FILE__ to repository-relative paths for consistent logs/asserts.
force_redefine_file_macro_for_sources(zero_test_support)
