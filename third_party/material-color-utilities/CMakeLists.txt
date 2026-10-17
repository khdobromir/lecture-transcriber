# Vendored, pinned sources; never downloads code during configure or build.
file(GLOB_RECURSE mcu_sources CONFIGURE_DEPENDS cpp/*.cc)
add_library(material_color_utilities STATIC ${mcu_sources})
target_include_directories(material_color_utilities SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(material_color_utilities PUBLIC cxx_std_17)
