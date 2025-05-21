cmake_minimum_required(VERSION 3.10)
project(upd)


set(CMAKE_CXX_STANDARD 20)
# Find OpenCV
find_package(OpenCV REQUIRED)

# Include directories
include_directories(
    ${OpenCV_INCLUDE_DIRS}
    /usr/local/opt/boost/include
)

# Link directories (optional with pkg-config but can help)
link_directories(/usr/local/opt/boost/lib)

# Add executables
add_executable(sender sender.cpp)
add_executable(receiver receiver.cpp)

# Link libraries
target_link_libraries(sender ${OpenCV_LIBS} boost_system)
target_link_libraries(receiver ${OpenCV_LIBS} boost_system)