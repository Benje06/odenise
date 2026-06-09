# ===========================================================================
#  src/plugin/juce/CMakeLists.txt  --  Plugin VST3/CLAP odenise (couche JUCE).
#
#  Layout sources :
#    src/  -> JucePlugin, JuceAudioLayer  (traitement / cycle de vie audio)
#    ui/   -> JuceEditorComponent         (interface graphique)
#
#  JUCE 8 est fetche par le CMakeLists.txt racine dans build/<preset>/_deps/
#  avant d'inclure ce sous-repertoire. ODENISE_PLUGIN_READY garantit que
#  les cibles juce:: sont disponibles.
#
#  Conditionne par ODENISE_BUILD_PLUGIN (OFF par defaut dans les presets).
# ===========================================================================

if(NOT ODENISE_PLUGIN_READY)
    message(STATUS "odenise plugin : JUCE non disponible, cible ignoree")
    return()
endif()

# ---------------------------------------------------------------------------
#  Metadonnees plugin
# ---------------------------------------------------------------------------
set(ODENISE_PLUGIN_NAME         "odenise")
set(ODENISE_PLUGIN_MANUFACTURER "Uru")
set(ODENISE_PLUGIN_VERSION      "${PROJECT_VERSION}")
set(ODENISE_PLUGIN_CLAP_ID      "fr.uru.odenise")

# ---------------------------------------------------------------------------
#  Cible plugin JUCE
# ---------------------------------------------------------------------------
juce_add_plugin(odenise_plugin
    PLUGIN_NAME             "${ODENISE_PLUGIN_NAME}"
    COMPANY_NAME            "${ODENISE_PLUGIN_MANUFACTURER}"
    VERSION                 "${ODENISE_PLUGIN_VERSION}"
    PLUGIN_MANUFACTURER_CODE Uru1
    PLUGIN_CODE             Odns
    FORMATS                 VST3 CLAP
    VST3_CATEGORIES         "Fx" "Dynamics"
    CLAP_ID                 "${ODENISE_PLUGIN_CLAP_ID}"
    CLAP_FEATURES           audio-effect noise-suppression mono
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

# ---------------------------------------------------------------------------
#  Sources
# ---------------------------------------------------------------------------
target_sources(odenise_plugin
    PRIVATE
        # Traitement / cycle de vie audio
        src/JucePlugin.h
        src/JucePlugin.cpp
        src/JuceAudioLayer.h
        src/JuceAudioLayer.cpp
        # Interface graphique
        ui/JuceEditorComponent.h
        ui/JuceEditorComponent.cpp
)

# ---------------------------------------------------------------------------
#  Modules JUCE requis
# ---------------------------------------------------------------------------
target_compile_definitions(odenise_plugin
    PUBLIC
        JUCE_WINDOWS
        JUCE_ASIO=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_DEBUG
    PRIVATE
        AUDIO_EXPORTS
)

target_link_libraries(odenise_plugin
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        odenise_core
    PUBLIC
        juce::juce_recommended_lto_flags
)

# ---------------------------------------------------------------------------
#  Includes
#
#  Les deux sous-dossiers (src/ et ui/) sont mutuellement visibles pour que
#  JuceEditorComponent.cpp puisse inclure JucePlugin.h et JuceAudioLayer.h,
#  et que JucePlugin.cpp puisse inclure JuceEditorComponent.h.
#  Les headers internes odenise (tools/, engine/, audio/) sont aussi
#  exposes en PRIVATE.
# ---------------------------------------------------------------------------
target_include_directories(odenise_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/tools
        ${CMAKE_SOURCE_DIR}/src/core/engine
        ${CMAKE_SOURCE_DIR}/src/core/audio
        ${CMAKE_SOURCE_DIR}/src/core/chain
)

# ---------------------------------------------------------------------------
#  /W4 conditionne au C++ uniquement (jamais transmis a nvcc si present)
# ---------------------------------------------------------------------------
target_compile_options(odenise_plugin PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    >
)

add_custom_command(TARGET odenise_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:odenise_core>
        $<TARGET_FILE:odenise_log>
        $<TARGET_FILE:odenise_chain>
        $<TARGET_FILE:odenise_thread>
        $<TARGET_FILE_DIR:odenise_plugin_VST3>
)
