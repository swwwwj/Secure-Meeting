cmake_minimum_required(VERSION 3.16)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)

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
