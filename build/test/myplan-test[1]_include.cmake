if(EXISTS "/mnt/c/Users/kanka/Desktop/Dev/MyDisassembler/build/test/myplan-test[1]_tests.cmake")
  include("/mnt/c/Users/kanka/Desktop/Dev/MyDisassembler/build/test/myplan-test[1]_tests.cmake")
else()
  add_test(myplan-test_NOT_BUILT myplan-test_NOT_BUILT)
endif()
