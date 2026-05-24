set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(model-repair-gui
    main.cpp
    MainWindow.cpp
    BatchWindow.cpp
    RepairWorker.cpp
    ReportView.cpp
    MeshViewWidget.cpp
    PreviewWindow.cpp
    WizardWorker.cpp
    WizardWindow.cpp
    resources.qrc
)

target_link_libraries(model-repair-gui
    PRIVATE
        modelrepair
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        spdlog::spdlog
        modelrepair_warnings
)

set_target_properties(model-repair-gui PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE    TRUE
)

install(TARGETS model-repair-gui RUNTIME DESTINATION bin COMPONENT Runtime)
install(FILES model-repair.desktop
    DESTINATION share/applications
    COMPONENT Runtime)
install(FILES icons/model-repair.svg
    DESTINATION share/icons/hicolor/scalable/apps
    COMPONENT Runtime)
