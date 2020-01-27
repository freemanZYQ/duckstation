add_library(frontend-common
  icon.cpp
  icon.h
  imgui_styles.cpp
  imgui_styles.h
  sdl_audio_stream.cpp
  sdl_audio_stream.h
)

target_link_libraries(duckstation PRIVATE core common imgui SDL2::Core)
