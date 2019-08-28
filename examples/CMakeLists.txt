find_package(Boost COMPONENTS system date_time filesystem chrono iostreams REQUIRED)

add_executable( appbase_example main.cpp)
target_link_libraries( appbase_example appbase ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} fc ssl crypto secp256k1 gmp pthread ${Boost_LIBRARIES})
