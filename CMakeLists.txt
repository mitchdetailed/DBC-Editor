cmake_minimum_required(VERSION 3.16)
project(QtDbcEditor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(QtDbcEditor WIN32
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/dbcmodel.h
    src/dbcdialogs.h
    src/dbcutil.h
    src/dbcdocument.h
    src/dbcdocument.cpp
    src/bitlayoutwidget.h
    src/bitlayoutwidget.cpp
    src/attributeswidget.h
    src/attributeswidget.cpp
    src/valuetableswidget.h
    src/valuetableswidget.cpp
    src/nodeswidget.h
    src/nodeswidget.cpp
    src/messageswidget.h
    src/messageswidget.cpp
    src/signalswidget.h
    src/signalswidget.cpp
    src/overviewwidget.h
    src/overviewwidget.cpp
)

target_link_libraries(QtDbcEditor PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(APPLE)
    set_target_properties(QtDbcEditor PROPERTIES MACOSX_BUNDLE TRUE)
endif()
