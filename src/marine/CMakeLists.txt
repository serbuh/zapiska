add_library(marine-core STATIC
    core/MarineCore.cpp
    core/MarineCore.h
)

target_include_directories(marine-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)

target_link_libraries(marine-core
    PUBLIC
        ${ZAPISKA_QT_PACKAGE}::Core
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

install(TARGETS marine-recorder-gui RUNTIME DESTINATION bin)

