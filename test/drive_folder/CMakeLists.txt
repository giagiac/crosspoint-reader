add_executable(DriveFolderTest
  DriveFolderTest.cpp
  ${REPO_ROOT}/src/util/DriveFolder.cpp
)

target_include_directories(DriveFolderTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(DriveFolderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DriveFolderTest)
