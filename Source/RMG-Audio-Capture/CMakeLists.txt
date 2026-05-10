#
# RMG-Audio-Capture CMakeLists.txt
#
project(RMG-Audio-Capture)

set(CMAKE_SHARED_LIBRARY_PREFIX "")

add_library(RMG-Audio-Capture SHARED
    audio_capture_plugin.cpp
)

set_target_properties(RMG-Audio-Capture PROPERTIES PREFIX "")
