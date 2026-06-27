add_library(marine-core STATIC
    core/GrOsmoSdrSource.cpp
    core/GrOsmoSdrSource.h
    core/IqPowerSink.cpp
    core/IqPowerSink.h
    core/MarineCore.cpp
    core/MarineCore.h
    core/SdrSource.cpp
    core/SdrSource.h
)

find_package(Gnuradio REQUIRED COMPONENTS blocks)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GR_OSMOSDR REQUIRED IMPORTED_TARGET gnuradio-osmosdr)

target_include_directories(marine-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)

target_link_libraries(marine-core
    PUBLIC
        ${ZAPISKA_QT_PACKAGE}::Core
    PRIVATE
        gnuradio::gnuradio-blocks
        PkgConfig::GR_OSMOSDR
)

add_executable(marine-recorder-gui
    recorder_gui/MainWindow.cpp
    recorder_gui/MainWindow.h
    recorder_gui/main.cpp
)

target_link_libraries(marine-recorder-gui
    PRIVATE
        marine-core
        ${ZAPISKA_QT_PACKAGE}::Widgets
)

add_executable(marine-sdr-smoke
    tools/sdr_smoke.cpp
)

target_link_libraries(marine-sdr-smoke
    PRIVATE
        marine-core
)

set(ZAPISKA_CHANNEL_DATA_FILE ${PROJECT_SOURCE_DIR}/data/marine_channels.json)

add_custom_command(TARGET marine-recorder-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:marine-recorder-gui>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ZAPISKA_CHANNEL_DATA_FILE}"
        "$<TARGET_FILE_DIR:marine-recorder-gui>/data/marine_channels.json"
)

install(TARGETS marine-recorder-gui RUNTIME DESTINATION bin)
install(FILES "${ZAPISKA_CHANNEL_DATA_FILE}" DESTINATION bin/data)
