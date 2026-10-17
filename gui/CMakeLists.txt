find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Network Concurrent LinguistTools)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup(REQUIRES 6.8)
add_subdirectory(../third_party/material-color-utilities ${CMAKE_CURRENT_BINARY_DIR}/material-color-utilities)
qt_add_library(transcribe_gui_logic STATIC backend.cpp backend.hpp history.cpp history.hpp models.cpp models.hpp language.cpp language.hpp theme.cpp theme.hpp text.hpp)
target_link_libraries(transcribe_gui_logic PUBLIC transcribe_core Qt6::Quick Qt6::Network Qt6::Concurrent material_color_utilities)
target_include_directories(transcribe_gui_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
transcribe_warnings(transcribe_gui_logic)
qt_add_resources(transcribe_gui_logic model_manifest PREFIX "/models" BASE "${PROJECT_SOURCE_DIR}/scripts" FILES "${PROJECT_SOURCE_DIR}/scripts/models.tsv")
set_source_files_properties(UiStyle.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(transcribe_gui_logic URI Transcribe VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Transcribe"
    SOURCES backend.hpp history.hpp models.hpp language.hpp theme.hpp
    QML_FILES UiStyle.qml)
qt_add_executable(transcribe-gui main.cpp)
if(MSVC)
    # Qt 6.8 script-to-C++ output instantiates SDK templates that trigger C4702.
    # Keep cached QML bytecode and /W4 /WX for all application C++ sources.
    set_target_properties(transcribe_gui_logic transcribe-gui PROPERTIES
        QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")
endif()
target_compile_definitions(transcribe-gui PRIVATE TRANSCRIBE_VERSION="${TRANSCRIBE_VERSION}")
qt_add_resources(transcribe_gui_logic app_icon PREFIX "/icons" BASE "${PROJECT_SOURCE_DIR}"
    FILES "${PROJECT_SOURCE_DIR}/icon.png")
set_target_properties(transcribe-gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
qt_add_qml_module(transcribe-gui URI Transcribe.App VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Transcribe/App"
    IMPORT_PATH "${CMAKE_CURRENT_BINARY_DIR}"
    QML_FILES Main.qml ComponentGallery.qml TranscriptionPage.qml HistoryPage.qml ModelsPage.qml AppearancePage.qml UiButton.qml UiField.qml UiTextArea.qml UiCheck.qml UiSwitch.qml UiCombo.qml UiSpin.qml UiCard.qml UiProgress.qml UiIcon.qml UiStatus.qml)
target_link_libraries(transcribe-gui PRIVATE transcribe_gui_logic transcribe_gui_logicplugin Qt6::QuickControls2)
transcribe_warnings(transcribe-gui)
if(WIN32)
    target_link_libraries(transcribe_gui_logic PUBLIC ole32 shell32 uuid)
    target_sources(transcribe-gui PRIVATE ../packaging/windows/transcribe-icon.rc)
    target_include_directories(transcribe-gui PRIVATE ../packaging/windows)
    set_target_properties(transcribe-gui PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_sources(transcribe-gui PRIVATE ../packaging/windows/transcribe.manifest)
    else()
        target_sources(transcribe-gui PRIVATE ../packaging/windows/transcribe.manifest.rc)
        target_include_directories(transcribe-gui PRIVATE ../packaging/windows)
    endif()
endif()
add_dependencies(transcribe-gui transcribe)
install(TARGETS transcribe-gui RUNTIME DESTINATION bin)
if(NOT WIN32)
install(FILES ../packaging/linux/transcribe-gui.desktop DESTINATION share/applications)
install(FILES ../packaging/linux/transcribe-gui.png DESTINATION share/icons/hicolor/512x512/apps)
endif()
if(WIN32)
    qt_generate_deploy_qml_app_script(TARGET transcribe-gui OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
endif()
if(BUILD_TESTING)
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    add_test(NAME gui_translations COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/check_translations.py $<TARGET_FILE:Qt6::lupdate>)
    set_tests_properties(gui_translations PROPERTIES TIMEOUT 30 LABELS "gui;unit")
    # Candidate-only smoke; external speech/model prerequisites are deliberate.
    # Build it on every PR, run explicitly against the installed prefix/ZIP.
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE validation_source OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND git status --porcelain WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE validation_changes OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(validation_changes)
        string(APPEND validation_source "-dirty")
    endif()
    qt_add_executable(test_gui_real ../tests/test_gui_real.cpp)
    set_target_properties(test_gui_real PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(test_gui_real PRIVATE transcribe_gui_logic transcribe_gui_logicplugin Qt6::Test Qt6::QuickControls2)
    target_compile_definitions(test_gui_real PRIVATE GUI_QML_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/Main.qml"
        TRANSCRIBE_SOURCE_SHA="${validation_source}")
    transcribe_warnings(test_gui_real)
    qt_add_executable(test_gui_integration ../tests/test_gui_integration.cpp)
    set_target_properties(test_gui_integration PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(test_gui_integration PRIVATE transcribe_gui_logic Qt6::Test)
    target_compile_definitions(test_gui_integration PRIVATE TRANSCRIBE_BINARY="$<TARGET_FILE:transcribe>" MOCK_BINARY="$<TARGET_FILE:mock_portable>")
    add_dependencies(test_gui_integration transcribe mock_portable)
    transcribe_warnings(test_gui_integration)
    add_test(NAME gui_cli_integration COMMAND test_gui_integration -o "${CMAKE_BINARY_DIR}/Testing/Temporary/gui_cli_integration.log,txt")
    set_tests_properties(gui_cli_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "gui;integration")
    qt_add_executable(test_gui ../tests/test_gui.cpp)
    set_target_properties(test_gui PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(test_gui PRIVATE transcribe_gui_logic Qt6::Test)
    target_compile_definitions(test_gui PRIVATE TRANSCRIBE_BINARY="$<TARGET_FILE:transcribe>")
    transcribe_warnings(test_gui)
    add_test(NAME gui_controller COMMAND test_gui -o "${CMAKE_BINARY_DIR}/Testing/Temporary/gui_controller.log,txt")
    set_tests_properties(gui_controller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 LABELS gui)
    qt_add_executable(test_theme ../tests/test_theme.cpp)
    set_target_properties(test_theme PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(test_theme PRIVATE transcribe_gui_logic transcribe_gui_logicplugin Qt6::Test)
    transcribe_warnings(test_theme)
    add_test(NAME gui_theme COMMAND test_theme -o "${CMAKE_BINARY_DIR}/Testing/Temporary/gui_theme.log,txt" -v1)
    set_tests_properties(gui_theme PROPERTIES TIMEOUT 60 LABELS "gui;unit")
    qt_add_executable(test_qml ../tests/test_qml.cpp)
    set_target_properties(test_qml PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(test_qml PRIVATE transcribe_gui_logic transcribe_gui_logicplugin Qt6::Test Qt6::QuickControls2)
    target_compile_definitions(test_qml PRIVATE GUI_QML_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/Main.qml" TEST_GUI_HELPER="$<TARGET_FILE:test_gui>")
    add_dependencies(test_qml test_gui)
    transcribe_warnings(test_qml)
    set(qml_test_platform offscreen)
    set(qml_test_theme generic)
    if(WIN32)
        # Native font lookup uses the installed Windows fonts. Offscreen's
        # generic database expects SDK/lib/fonts, which Qt does not ship.
        set(qml_test_platform windows)
        # Qt 6.8.3's native window creation dereferences QWindowsTheme::instance;
        # the generic theme does not create that Windows-specific object.
        set(qml_test_theme windows)
    endif()
    set_tests_properties(gui_theme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${qml_test_platform};QT_QPA_PLATFORMTHEME=${qml_test_theme}")
    add_test(NAME gui_qml COMMAND test_qml -o "${CMAKE_BINARY_DIR}/Testing/Temporary/gui_qml.log,txt")
    set_tests_properties(gui_qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${qml_test_platform};QT_QUICK_BACKEND=software;QT_QPA_PLATFORMTHEME=${qml_test_theme}" TIMEOUT 30 LABELS gui)
    foreach(scale IN ITEMS 150 200)
        if(scale EQUAL 150)
            set(scale_factor 1.5)
        else()
            set(scale_factor 2)
        endif()
        add_test(NAME gui_qml_scale${scale} COMMAND test_qml minimumWindowAndPalettes
            -o "${CMAKE_BINARY_DIR}/Testing/Temporary/gui_qml_scale${scale}.log,txt")
        set_tests_properties(gui_qml_scale${scale} PROPERTIES TIMEOUT 30 LABELS gui
            ENVIRONMENT "QT_QPA_PLATFORM=${qml_test_platform};QT_QUICK_BACKEND=software;QT_QPA_PLATFORMTHEME=${qml_test_theme};QT_SCALE_FACTOR=${scale_factor}")
    endforeach()
endif()

# Compile catalogs into every GUI executable; installed packages need no SDK catalogs.
qt_add_translations(TARGETS transcribe-gui
    SOURCE_TARGETS transcribe_gui_logic transcribe-gui
    TS_FILES i18n/transcribe_en.ts
    QM_FILES_OUTPUT_VARIABLE transcribe_qm
    NO_GENERATE_PLURALS_TS_FILE)
get_target_property(qt_qmake Qt6::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${qt_qmake}" -query QT_INSTALL_TRANSLATIONS
    OUTPUT_VARIABLE qt_translations_dir OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
set(transcribe_translation_targets transcribe-gui)
if(BUILD_TESTING)
    list(APPEND transcribe_translation_targets test_gui test_qml test_gui_integration test_gui_real)
endif()
foreach(gui_target IN LISTS transcribe_translation_targets)
    qt_add_resources(${gui_target} ${gui_target}_translations PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${transcribe_qm})
    qt_add_resources(${gui_target} ${gui_target}_qt_translations PREFIX "/i18n"
        BASE "${qt_translations_dir}"
        FILES "${qt_translations_dir}/qtbase_ru.qm" "${qt_translations_dir}/qtdeclarative_ru.qm")
endforeach()
