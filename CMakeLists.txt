cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


project(myk-neural-jammer VERSION 0.0.1)

add_subdirectory(./libs/JUCE JUCE)  
# uncomment for static libraries build, which is slower but easier to share your plugin :) 
# set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build llama as a static library")                
# makes the static library build work with JUCE
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# llama.cpp / ggml build toggles (exposed at the top-level project)
option(NJAM_LLAMA_NATIVE "Build ggml with native CPU tuning (-march=native where applicable)" ON)
option(NJAM_LLAMA_VULKAN "Enable Vulkan backend in ggml" OFF)
option(NJAM_LLAMA_OPENMP "Enable OpenMP in ggml CPU backend" ON)
option(NJAM_LLAMA_LTO "Enable LTO/IPO for ggml/llama targets (if toolchain supports it)" OFF)
option(NJAM_LLAMA_BLAS "Enable BLAS backend (mainly helps prompt/prefill throughput)" OFF)
set(NJAM_LLAMA_BLAS_VENDOR "Generic" CACHE STRING "BLAS vendor for ggml when NJAM_LLAMA_BLAS=ON")

if (APPLE)
    option(NJAM_LLAMA_METAL "Enable Apple Metal backend in ggml" ON)
else()
    option(NJAM_LLAMA_METAL "Enable Apple Metal backend in ggml (Apple platforms only)" OFF)
endif()


if (NJAM_LLAMA_VULKAN)
    find_package(Vulkan)
    if (NOT Vulkan_FOUND OR NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR
            "NJAM_LLAMA_VULKAN=ON requires Vulkan development components that were not found.\n"
            "Detected: Vulkan_FOUND='${Vulkan_FOUND}', Vulkan_GLSLC_EXECUTABLE='${Vulkan_GLSLC_EXECUTABLE}'\n"
            "\n"
            "What you need:\n"
            "  1. Vulkan loader/development package (headers + loader)\n"
            "  2. glslc (Shaderc compiler)\n"
            "\n"
            "Linux (Debian/Ubuntu):\n"
            "  sudo apt-get install libvulkan-dev glslc\n"
            "\n"
            "macOS / Windows:\n"
            "  Install the LunarG Vulkan SDK and ensure 'glslc' is on PATH.\n"
            "\n"
            "Then re-run CMake with -DNJAM_LLAMA_VULKAN=ON.")
    endif()
endif()

set(GGML_NATIVE ${NJAM_LLAMA_NATIVE} CACHE BOOL "ggml: optimize the build for the current system" FORCE)
set(GGML_LTO ${NJAM_LLAMA_LTO} CACHE BOOL "ggml: enable link time optimization" FORCE)
set(GGML_OPENMP ${NJAM_LLAMA_OPENMP} CACHE BOOL "ggml: use OpenMP" FORCE)
set(GGML_BLAS ${NJAM_LLAMA_BLAS} CACHE BOOL "ggml: use BLAS" FORCE)
set(GGML_BLAS_VENDOR ${NJAM_LLAMA_BLAS_VENDOR} CACHE STRING "ggml: BLAS library vendor" FORCE)
set(GGML_METAL ${NJAM_LLAMA_METAL} CACHE BOOL "ggml: use Metal" FORCE)
set(GGML_VULKAN ${NJAM_LLAMA_VULKAN} CACHE BOOL "ggml: use Vulkan" FORCE)

add_subdirectory(./libs/llama.cpp llama.cpp)

## command-line test programs
## Super simple one that verifies a model can be laoded and exits
add_executable(myk-model-tester 
src/tests/llama_cpp_cli.cpp
src/LLMController.cpp
)
target_link_libraries(myk-model-tester PRIVATE llama ${CMAKE_THREAD_LIBS_INIT})


# ## JUCE plugin
# ##

juce_add_plugin(myk-neural-jammer-plugin
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME Yee-King                          # Specify the name of the plugin's author
    IS_SYNTH FALSE                      # Is this a synth or an effect?
    NEEDS_MIDI_INPUT TRUE               # Does the plugin need midi input?
    NEEDS_MIDI_OUTPUT TRUE              # Does the plugin need midi output?
    IS_MIDI_EFFECT TRUE                 # Is this plugin a MIDI effect?
    # EDITOR_WANTS_KEYBOARD_FOCUS TRUE/FALSE    # Does the editor need keyboard focus?
    COPY_PLUGIN_AFTER_BUILD TRUE        # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE Yeek               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE Nja1                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME "myk-njam-plugin"        # The name of the final executable, which can differ from the target name
    )

juce_generate_juce_header(myk-neural-jammer-plugin)

target_sources(myk-neural-jammer-plugin
    PRIVATE
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
    # src/NJamLanguage.cpp
    src/LLMController.cpp
    src/InferenceThreadManager.cpp
    src/NJamLanguage.cpp

)

target_compile_definitions(myk-neural-jammer-plugin
    PUBLIC # 
        JUCE_ALSA=1
        JUCE_DIRECTSOUND=1
        JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
        #JUCE_PLUGINHOST_LADSPA=1
        #JUCE_PLUGINHOST_LV2=1
        #JUCE_PLUGINHOST_VST3=1
        JUCE_USE_OGGVORBIS=1
        #JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_GTK=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(myk-neural-jammer-plugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_opengl
        llama
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

