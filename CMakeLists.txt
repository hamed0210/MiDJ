cmake_minimum_required(VERSION 3.22)
project(MiDJ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ruta de JUCE: por defecto D:/Tools/JUCE, se puede cambiar con -DJUCE_DIR=...
set(JUCE_DIR "D:/Tools/JUCE" CACHE PATH "Carpeta de JUCE")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# Rubber Band (cambio de tempo sin cambio de tono). Licencia GPL: para vender MiDJ
# hace falta su licencia comercial. Se compila en un solo archivo, como recomienda su autor.
set(RUBBERBAND_DIR "D:/Tools/rubberband" CACHE PATH "Carpeta de Rubber Band")
add_library(rubberband STATIC ${RUBBERBAND_DIR}/single/RubberBandSingle.cpp)
target_include_directories(rubberband SYSTEM PUBLIC ${RUBBERBAND_DIR})
target_compile_definitions(rubberband PRIVATE _USE_MATH_DEFINES NOMINMAX)
option(RUBBERBAND_KISSFFT "Rubber Band con KissFFT en vez de su FFT interna" ON)
if(RUBBERBAND_KISSFFT)
    target_sources(rubberband PRIVATE ${RUBBERBAND_DIR}/src/ext/kissfft/kiss_fft.c
                                      ${RUBBERBAND_DIR}/src/ext/kissfft/kiss_fftr.c)
    target_include_directories(rubberband PRIVATE ${RUBBERBAND_DIR}/src/ext/kissfft)
    target_compile_definitions(rubberband PRIVATE ALREADY_CONFIGURED USE_BQRESAMPLER NO_TIMING
                               NO_THREADING NO_THREAD_CHECKS HAVE_KISSFFT)
endif()
if(MSVC)
    # Siempre optimizada, tambien en Debug: sin optimizar es ~20 veces mas lenta y no da
    # tiempo real. /O2 es incompatible con las comprobaciones /RTC1 de Debug, que se quitan.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    target_compile_options(rubberband PRIVATE /W0 /bigobj /arch:AVX /fp:fast $<$<CONFIG:Debug>:/O2 /Ob2>)
endif()

# SQLite (biblioteca de canciones). Dominio publico, un solo archivo C.
set(SQLITE_DIR "D:/Tools/sqlite" CACHE PATH "Carpeta de SQLite (amalgamation)")
add_library(sqlite3 STATIC ${SQLITE_DIR}/sqlite3.c)
target_include_directories(sqlite3 SYSTEM PUBLIC ${SQLITE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
endif()

# Motor de audio: independiente de la interfaz para poder probarlo solo
add_library(MiDJEngine STATIC
    Source/Control/Actions.cpp
    Source/Control/ControlSurface.cpp
    Source/Control/MidiController.cpp
    Source/Control/MidiMapping.cpp
    Source/Engine/Automix.cpp
    Source/Engine/BpmDetector.cpp
    Source/Engine/ChannelStrip.cpp
    Source/Engine/Deck.cpp
    Source/Engine/Effects.cpp
    Source/Engine/Mixer.cpp
    Source/Engine/MixPoints.cpp
    Source/Engine/SampleData.cpp
    Source/Engine/Sampler.cpp
    Source/Engine/TimeStretcher.cpp
    Source/Library/Library.cpp
    Source/Library/TagReader.cpp)
target_include_directories(MiDJEngine PUBLIC Source)
# Los modulos JUCE se compilan una sola vez aqui (PRIVATE) y se exportan sus
# cabeceras y definiciones a la app y a las pruebas (patron recomendado por JUCE).
target_link_libraries(MiDJEngine
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp rubberband sqlite3
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
target_compile_definitions(MiDJEngine PUBLIC
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
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
    Source/UI/EffectsRow.cpp
    Source/UI/MixerComponent.cpp
    Source/UI/LibraryBrowser.cpp
    Source/UI/MidiLearnOverlay.cpp
    Source/UI/SamplerPanel.cpp
    Source/UI/DarkLookAndFeel.cpp)
target_link_libraries(MiDJ PRIVATE MiDJEngine)

# Pruebas automaticas (ctest)
enable_testing()
juce_add_console_app(MiDJTests PRODUCT_NAME "MiDJTests")
target_sources(MiDJTests PRIVATE Tests/TestMain.cpp)
target_link_libraries(MiDJTests PRIVATE MiDJEngine)
add_test(NAME MiDJTests COMMAND MiDJTests)
