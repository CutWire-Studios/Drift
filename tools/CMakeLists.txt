add_executable(probe probe.cpp)
target_link_libraries(probe PRIVATE Qt6::Core driftengine)

# Not shipped: the Windows packaging job stages it beside drift.exe, runs it against the deployed
# multimedia plugin, and deletes it again. See tools/audiobackendcheck.cpp.
add_executable(audiobackendcheck audiobackendcheck.cpp)
target_link_libraries(audiobackendcheck PRIVATE Qt6::Core Qt6::Multimedia)

add_executable(renderframe renderframe.cpp)
target_include_directories(renderframe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(renderframe PRIVATE Qt6::Core Qt6::Gui driftengine driftcore)
drift_copy_effects(renderframe)
drift_copy_transitions(renderframe)

add_executable(effectthumbs effectthumbs.cpp)
target_include_directories(effectthumbs PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(effectthumbs PRIVATE Qt6::Core Qt6::Gui driftengine driftcore)
drift_copy_effects(effectthumbs)
drift_bundle_onnxruntime(effectthumbs)

add_executable(benchcomposite benchcomposite.cpp)
target_include_directories(benchcomposite PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(benchcomposite PRIVATE Qt6::Core Qt6::Gui driftengine driftcore)
drift_copy_effects(benchcomposite)
drift_copy_transitions(benchcomposite)

add_executable(transitionthumbs transitionthumbs.cpp)
target_include_directories(transitionthumbs PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(transitionthumbs PRIVATE Qt6::Core Qt6::Gui driftengine driftcore)
drift_copy_transitions(transitionthumbs)

add_executable(templatethumbs templatethumbs.cpp)
target_include_directories(templatethumbs PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(templatethumbs PRIVATE Qt6::Core Qt6::Gui driftengine driftcore)
drift_copy_effects(templatethumbs)

add_executable(transcribe transcribe.cpp)
target_include_directories(transcribe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(transcribe PRIVATE Qt6::Core driftengine driftcore)
drift_bundle_onnxruntime(transcribe)

add_executable(segment segment.cpp)
target_include_directories(segment PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(segment PRIVATE Qt6::Core Qt6::Gui driftengine driftcore)
drift_bundle_onnxruntime(segment)

add_executable(scenedetect scenedetect.cpp)
target_include_directories(scenedetect PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(scenedetect PRIVATE Qt6::Core Qt6::Gui driftengine driftcore)
drift_bundle_onnxruntime(scenedetect)

add_executable(facedetect facedetect.cpp)
target_include_directories(facedetect PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(facedetect PRIVATE Qt6::Core Qt6::Gui driftengine driftcore)
drift_copy_effects(facedetect)
drift_bundle_onnxruntime(facedetect)
