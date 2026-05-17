add_executable(model-repair main.cpp)

target_link_libraries(model-repair
    PRIVATE
        modelrepair
        CLI11::CLI11
        spdlog::spdlog
        modelrepair_warnings
)
