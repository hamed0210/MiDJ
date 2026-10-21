cmake_minimum_required(VERSION 3.22)
project(MiDJ VERSION 0.3.0 LANGUAGES C CXX)

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

# ONNX Runtime con DirectML (Microsoft, licencia MIT) para los stems con IA: separacion en CPU o en la
# tarjeta grafica. Se usa la DLL que trae el paquete (no la de System32) copiandola junto a cada programa.
set(ONNXRUNTIME_DIR "D:/Tools/onnxruntime" CACHE PATH "Carpeta de ONNX Runtime (include y lib)")
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ONNXRUNTIME_DIR}/lib/onnxruntime.dll
    IMPORTED_IMPLIB ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib
    INTERFACE_INCLUDE_DIRECTORIES ${ONNXRUNTIME_DIR}/include)
# Modelo de separacion (Spleeter 2 stems de sherpa-onnx, pesos de Deezer con licencia MIT).
set(STEMS_MODEL_DIR "D:/Tools/models/spleeter-2stems-int8" CACHE PATH "Carpeta del modelo de stems")

function(midj_copy_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_DIR}/lib/onnxruntime.dll ${ONNXRUNTIME_DIR}/lib/DirectML.dll $<TARGET_FILE_DIR:${target}>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/models/spleeter-2stems
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${STEMS_MODEL_DIR} $<TARGET_FILE_DIR:${target}>/models/spleeter-2stems)
endfunction()

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
    Source/Engine/AuxAudio.cpp
    Source/Engine/HeadphoneOutput.cpp
    Source/Engine/MasterTap.cpp
    Source/Engine/Mixer.cpp
    Source/Engine/MixPoints.cpp
    Source/Engine/SampleData.cpp
    Source/Engine/Sampler.cpp
    Source/Engine/StemSeparator.cpp
    Source/Engine/StemsService.cpp
    Source/Engine/TimeStretcher.cpp
    Source/Library/Library.cpp
    Source/Library/TagReader.cpp
    Source/Video/MediaFoundation.cpp
    Source/Video/DeckVideo.cpp
    Source/Video/Compositor.cpp
    Source/Video/VideoMix.cpp
    Source/Video/Scenes.cpp
    Source/Video/VideoProgram.cpp
    Source/Video/Capture.cpp
    Source/Video/Visualizer.cpp
    Source/Video/MediaSources.cpp
    Source/Video/GpuFrames.cpp
    Source/Video/YouTubeSearch.cpp)
target_include_directories(MiDJEngine PUBLIC Source)
# Los modulos JUCE se compilan una sola vez aqui (PRIVATE) y se exportan sus
# cabeceras y definiciones a la app y a las pruebas (patron recomendado por JUCE).
target_link_libraries(MiDJEngine
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp rubberband sqlite3 onnxruntime
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
    Source/UI/VideoWindow.cpp
    Source/UI/ScenePanel.cpp
    Source/UI/SourceDialogs.cpp
    Source/UI/AutomixPanel.cpp
    Source/UI/Icons.cpp
    Source/UI/Widgets.cpp
    Source/UI/TransitionDialog.cpp
    Source/UI/DarkLookAndFeel.cpp
    Source/UI/YouTubePlayer.cpp
    Source/UI/YouTubePanel.cpp
    Source/UI/YouTubeThumbnails.cpp)
# WebView2 (Microsoft, motor de Edge) para el reproductor de YouTube: el cargador estatico; el
# motor lo pone Windows (WebView2 Runtime).
set(WEBVIEW2_DIR "D:/Tools/WebView2" CACHE PATH "Carpeta del SDK de WebView2 (include y x64)")
target_include_directories(MiDJ PRIVATE ${WEBVIEW2_DIR}/include)
target_link_libraries(MiDJ PRIVATE MiDJEngine ${WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib version.lib)
midj_copy_runtime(MiDJ)

# Pruebas automaticas (ctest)
enable_testing()
juce_add_console_app(MiDJTests PRODUCT_NAME "MiDJTests")
target_sources(MiDJTests PRIVATE Tests/TestMain.cpp)
target_link_libraries(MiDJTests PRIVATE MiDJEngine)
midj_copy_runtime(MiDJTests)
add_test(NAME MiDJTests COMMAND MiDJTests)
