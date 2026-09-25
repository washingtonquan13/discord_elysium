cmake_minimum_required(VERSION 3.21)
project(Kestrel VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(KESTREL_VOICE "Build voice support (Opus, libsodium, libdave)" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network WebSockets Qml Quick QuickControls2 Widgets Svg)
qt_standard_project_setup()

# ---------------------------------------------------------------- third party
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# nlohmann json (header only) - needed by mlspp and libdave.
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${TP}/nlohmann)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shims/nlohmann_json/nlohmann_jsonConfig.cmake" "set(nlohmann_json_FOUND TRUE)\n")
set(nlohmann_json_DIR "${CMAKE_CURRENT_BINARY_DIR}/shims/nlohmann_json" CACHE PATH "" FORCE)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(qrcodegen STATIC ${TP}/qrcodegen/qrcodegen.cpp)
target_include_directories(qrcodegen PUBLIC ${TP}/qrcodegen)

# ---------------------------------------------------------------- sources
set(SOURCES
    src/main.cpp
    src/core/Json.h
    src/core/Types.h
    src/core/Settings.h src/core/Settings.cpp
    src/core/TokenStore.h src/core/TokenStore.cpp
    src/core/Http.h src/core/Http.cpp
    src/core/Gateway.h src/core/Gateway.cpp
    src/core/RemoteAuth.h src/core/RemoteAuth.cpp
    src/core/Store.h src/core/Store.cpp
    src/core/Permissions.h src/core/Permissions.cpp
    src/core/Protobuf.h src/core/Protobuf.cpp
    src/core/Markdown.h src/core/Markdown.cpp
    src/core/Emoji.h src/core/Emoji.cpp
    src/core/Client.h src/core/Client.cpp
    src/ui/ImageProvider.h src/ui/ImageProvider.cpp
    src/ui/QrProvider.h src/ui/QrProvider.cpp
    src/ui/Models.h src/ui/Models.cpp
    src/ui/MessageModel.h src/ui/MessageModel.cpp
    src/ui/MemberModel.h src/ui/MemberModel.cpp
    src/ui/AppController.h src/ui/AppController.cpp
    src/ui/Urls.h
)

if (KESTREL_VOICE)
    list(APPEND SOURCES
        src/voice/VoiceClient.h src/voice/VoiceClient.cpp
        src/voice/VoiceUdp.h src/voice/VoiceUdp.cpp
        src/voice/AudioEngine.h src/voice/AudioEngine.cpp
        src/voice/DaveSession.h src/voice/DaveSession.cpp
        src/voice/miniaudio_impl.c
    )
endif ()

qt_add_executable(kestrel WIN32 ${SOURCES} resources/resources.qrc)
if (WIN32)
    target_sources(kestrel PRIVATE resources/kestrel.rc)
endif ()

set(QML_FILES
    qml/Main.qml qml/Theme.qml qml/LoginView.qml qml/GuildRail.qml qml/ChannelSidebar.qml qml/DmSidebar.qml
    qml/UserPanel.qml qml/VoicePanel.qml qml/ChatView.qml qml/MessageDelegate.qml qml/Composer.qml qml/MemberList.qml
    qml/Avatar.qml qml/IconButton.qml qml/Icon.qml qml/ImageViewer.qml qml/SettingsDialog.qml qml/ContextMenu.qml
    qml/Tooltip.qml qml/ProfilePopup.qml qml/RichText.qml qml/Badge.qml qml/StatusDot.qml
    qml/EmojiPicker.qml qml/QuickSwitcher.qml)
foreach (f ${QML_FILES})
    get_filename_component(n ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${n})
endforeach ()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(kestrel
    URI Kestrel
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES ${QML_FILES}
)

target_include_directories(kestrel PRIVATE src)
target_link_libraries(kestrel PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::WebSockets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Svg
    ZLIB::ZLIB OpenSSL::Crypto qrcodegen)

if (KESTREL_VOICE)
    target_compile_definitions(kestrel PRIVATE KESTREL_VOICE)
    target_include_directories(kestrel PRIVATE ${TP}/miniaudio)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
    pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
    target_link_libraries(kestrel PRIVATE PkgConfig::OPUS PkgConfig::SODIUM)

    # mlspp + libdave (Discord's end-to-end encryption for voice)
    set(DISABLE_GREASE ON CACHE BOOL "" FORCE)
    set(TESTING OFF CACHE BOOL "" FORCE)
    set(MLS_CXX_NAMESPACE "mlspp" CACHE STRING "" FORCE)
    set(CMAKE_EXPORT_PACKAGE_REGISTRY OFF CACHE BOOL "" FORCE)
    set(CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY ON)
    add_subdirectory(${TP}/mlspp ${CMAKE_BINARY_DIR}/mlspp EXCLUDE_FROM_ALL)
    if (NOT TARGET MLSPP::mlspp)
        add_library(MLSPP::mlspp ALIAS mlspp)
    endif ()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shims/mlspp/MLSPPConfig.cmake" "set(MLSPP_FOUND TRUE)\n")
    set(MLSPP_DIR "${CMAKE_CURRENT_BINARY_DIR}/shims/mlspp" CACHE PATH "" FORCE)
    add_subdirectory(${TP}/libdave/cpp ${CMAKE_BINARY_DIR}/libdave EXCLUDE_FROM_ALL)
    target_link_libraries(kestrel PRIVATE libdave)

    if (WIN32)
        target_link_libraries(kestrel PRIVATE ws2_32 winmm ole32)
    else ()
        target_link_libraries(kestrel PRIVATE ${CMAKE_DL_LIBS} pthread m)
    endif ()
endif ()

if (WIN32)
    target_compile_definitions(kestrel PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(kestrel PRIVATE crypt32 user32)
    # mingw: ship a self-contained exe for the C/C++ runtime
    if (MINGW)
        target_link_options(kestrel PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
    endif ()
endif ()

install(TARGETS kestrel RUNTIME DESTINATION .)
