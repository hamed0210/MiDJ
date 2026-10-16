cmake_minimum_required(VERSION 3.22)
project(MiDJ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ruta de JUCE: por defecto D:/Tools/JUCE, se puede cambiar con -DJUCE_DIR=...
set(JUCE_DIR "D:/Tools/JUCE" CACHE PATH "Carpeta de JUCE")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# Motor de audio: independiente de la interfaz para poder probarlo solo
add_library(MiDJEngine STATIC
    Source/Engine/BpmDetector.cpp
    Source/Engine/ChannelStrip.cpp
    Source/Engine/Deck.cpp
    Source/Engine/Mixer.cpp)
target_include_directories(MiDJEngine PUBLIC Source)
# Los modulos JUCE se compilan una sola vez aqui (PRIVATE) y se exportan sus
# cabeceras y definiciones a la app y a las pruebas (patron recomendado por JUCE).
target_link_libraries(MiDJEngine
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
target_compile_definitions(MiDJEngine PUBLIC
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_compile_definitions(MiDJEngine INTERFACE
    $<TARGET_PROPERTY:MiDJEngine,COMPILE_DEFINITIONS>)
target_include_directories(MiDJEngine INTERFACE
    $<TARGET_PROPERTY:MiDJEngine,INCLUDE_DIRECTORIES>)
set_target_properties(MiDJEngine PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE VISIBILITY_INLINES_HIDDEN TRUE C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)

# Aplicacion
juce_add_gui_app(MiDJ PRODUCT_NAME "MiDJ" COMPANY_NAME "MiDJ")
target_sources(MiDJ PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/UI/DeckComponent.cpp
    Source/UI/MixerComponent.cpp
    Source/UI/DarkLookAndFeel.cpp)
target_link_libraries(MiDJ PRIVATE MiDJEngine)

# Pruebas automaticas (ctest)
enable_testing()
juce_add_console_app(MiDJTests PRODUCT_NAME "MiDJTests")
target_sources(MiDJTests PRIVATE Tests/TestMain.cpp)
target_link_libraries(MiDJTests PRIVATE MiDJEngine)
add_test(NAME MiDJTests COMMAND MiDJTests)
