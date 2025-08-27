# Backtesting Framework CMake Configuration

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Find ZeroMQ
pkg_check_modules(ZMQ REQUIRED libzmq)

# Find JSON library
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# Find CURL
find_package(CURL REQUIRED)

# Historical Data Player library
add_library(historical_data_player
    historical_data_player.cpp
    historical_data_player.h
)

target_link_libraries(historical_data_player
    ${ZMQ_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    common_lib
)

target_include_directories(historical_data_player PUBLIC
    ${ZMQ_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Fill Simulator library
add_library(fill_simulator
    fill_simulator.cpp
    fill_simulator.h
)

target_link_libraries(fill_simulator
    ${CMAKE_THREAD_LIBS_INIT}
    common_lib
)

target_include_directories(fill_simulator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Data Downloader library
add_library(data_downloader
    data_downloader.cpp
    data_downloader.h
)

target_link_libraries(data_downloader
    ${CURL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    common_lib
)

target_include_directories(data_downloader PUBLIC
    ${CURL_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Backtesting Framework executable
add_executable(hft_backtesting
    main.cpp
)

target_link_libraries(hft_backtesting
    historical_data_player
    data_downloader
    common_lib
    ${CMAKE_THREAD_LIBS_INIT}
)

# Install targets
install(TARGETS hft_backtesting
    RUNTIME DESTINATION bin
)

install(TARGETS historical_data_player fill_simulator data_downloader
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES
    historical_data_player.h
    fill_simulator.h
    data_downloader.h
    DESTINATION include/backtesting
)