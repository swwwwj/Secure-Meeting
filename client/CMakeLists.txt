cmake_minimum_required(VERSION 3.16)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)
if(APPLE)
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
    include("/opt/homebrew/Cellar/qtbase/6.11.1/lib/cmake/Qt6Core/Qt6QDarwinCameraPermissionPluginConfig.cmake")
endif()

if(DEFINED ENV{SM_MACOS_CODESIGN_IDENTITY} AND NOT "$ENV{SM_MACOS_CODESIGN_IDENTITY}" STREQUAL "")
    set(SM_MACOS_CODESIGN_IDENTITY "$ENV{SM_MACOS_CODESIGN_IDENTITY}" CACHE STRING
        "macOS codesign identity for SecureMeetingClient.app; leave empty to fall back to ad-hoc signing" FORCE)
else()
    set(SM_MACOS_CODESIGN_IDENTITY "" CACHE STRING
        "macOS codesign identity for SecureMeetingClient.app; leave empty to fall back to ad-hoc signing")
endif()

add_executable(SecureMeetingClient
    main.cpp

    ui/MainWindow.h
    ui/MainWindow.cpp
    ui/LoginWindow.h
    ui/LoginWindow.cpp
    ui/RegisterWindow.h
    ui/RegisterWindow.cpp
    ui/JoinMeetingWindow.h
    ui/JoinMeetingWindow.cpp
    ui/MeetingWindow.h
    ui/MeetingWindow.cpp
    ui/VideoWidget.h
    ui/VideoWidget.cpp

    controller/MainController.h
    controller/MainController.cpp
    config/AppConfig.h
    config/AppConfig.cpp

    video/VideoSource.h
    video/CameraVideoSource.h
    video/CameraVideoSource.cpp
    video/MockVideoSource.h
    video/MockVideoSource.cpp

    services/AIProcessor.h
    services/CameraPermissionService.h
    services/HttpAIProcessor.h
    services/HttpAIProcessor.cpp
    services/MeetingService.h
    services/HttpMeetingService.h
    services/HttpMeetingService.cpp
    services/MockMeetingService.h
    services/MockMeetingService.cpp
    services/NetworkService.h
    services/MockNetworkService.h
    services/MockNetworkService.cpp
    services/UserService.h
    services/HttpUserService.h
    services/HttpUserService.cpp
    services/MockUserService.h
    services/MockUserService.cpp
)

if(APPLE)
    target_sources(SecureMeetingClient PRIVATE
        services/CameraPermissionService_mac.mm
    )
else()
    target_sources(SecureMeetingClient PRIVATE
        services/CameraPermissionService.cpp
    )
endif()

target_include_directories(SecureMeetingClient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(SecureMeetingClient PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
)

if(APPLE)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    set_target_properties(SecureMeetingClient PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.securemeeting.client"
        MACOSX_BUNDLE_BUNDLE_NAME "SecureMeetingClient"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
    )
    target_link_libraries(SecureMeetingClient PRIVATE
        ${AVFOUNDATION_FRAMEWORK}
        Qt6::QDarwinCameraPermissionPlugin
        Qt6::QDarwinCameraPermissionPlugin_init
    )
endif()

add_custom_command(TARGET SecureMeetingClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SecureMeetingClient>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/dev.json"
        "$<TARGET_FILE_DIR:SecureMeetingClient>/config/dev.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/test.json"
        "$<TARGET_FILE_DIR:SecureMeetingClient>/config/test.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/prod.json"
        "$<TARGET_FILE_DIR:SecureMeetingClient>/config/prod.json"
)

if(APPLE)
    add_custom_command(TARGET SecureMeetingClient POST_BUILD
        COMMAND /bin/sh -c "if [ -n \"${SM_MACOS_CODESIGN_IDENTITY}\" ]; then codesign --force --deep --sign \"${SM_MACOS_CODESIGN_IDENTITY}\" --identifier com.securemeeting.client \"$<TARGET_BUNDLE_DIR:SecureMeetingClient>\"; else codesign --force --deep --sign - --identifier com.securemeeting.client \"$<TARGET_BUNDLE_DIR:SecureMeetingClient>\"; fi"
        VERBATIM
    )
endif()
