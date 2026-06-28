add_library(zapiska-core STATIC
    core/AudioLevelSink.cpp
    core/AudioLevelSink.h
    core/ChannelReceiver.cpp
    core/ChannelReceiver.h
    core/ChannelCatalog.cpp
    core/ChannelCatalog.h
    core/GrOsmoSdrSource.cpp
    core/GrOsmoSdrSource.h
    core/IqFftSink.cpp
    core/IqFftSink.h
    core/IqPowerSink.cpp
    core/IqPowerSink.h
    core/SdrSource.cpp
    core/SdrSource.h
)

find_package(Gnuradio REQUIRED COMPONENTS blocks fft filter analog audio)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GR_OSMOSDR REQUIRED IMPORTED_TARGET gnuradio-osmosdr)

target_include_directories(zapiska-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)

target_link_libraries(zapiska-core
    PUBLIC
        ${ZAPISKA_QT_PACKAGE}::Core
    PRIVATE
        gnuradio::gnuradio-blocks
        gnuradio::gnuradio-fft
        gnuradio::gnuradio-filter
        gnuradio::gnuradio-analog
        gnuradio::gnuradio-audio
        PkgConfig::GR_OSMOSDR
)

add_executable(zapiska
    gui/MainWindow.cpp
    gui/MainWindow.h
    gui/WaterfallWidget.cpp
    gui/WaterfallWidget.h
    gui/main.cpp
)

target_link_libraries(zapiska
    PRIVATE
        zapiska-core
        ${ZAPISKA_QT_PACKAGE}::Widgets
)

add_executable(zapiska-sdr-smoke
    tools/sdr_smoke.cpp
)

target_link_libraries(zapiska-sdr-smoke
    PRIVATE
        zapiska-core
)

set(ZAPISKA_CHANNEL_PRESET_FILE ${PROJECT_SOURCE_DIR}/data/presets/marine-vhf.json)

add_custom_command(TARGET zapiska POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:zapiska>/data/presets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ZAPISKA_CHANNEL_PRESET_FILE}"
        "$<TARGET_FILE_DIR:zapiska>/data/presets/marine-vhf.json"
)

install(TARGETS zapiska RUNTIME DESTINATION bin)
install(FILES "${ZAPISKA_CHANNEL_PRESET_FILE}" DESTINATION bin/data/presets)
