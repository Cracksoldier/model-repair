set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(model-repair-gui
    main.cpp
    MainWindow.cpp
    RepairWorker.cpp
    ReportView.cpp
    resources.qrc
)

target_link_libraries(model-repair-gui
    PRIVATE
        modelrepair
        Qt6::Widgets
        Qt6::Concurrent
        spdlog::spdlog
        modelrepair_warnings
)

set_target_properties(model-repair-gui PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE    TRUE
)
