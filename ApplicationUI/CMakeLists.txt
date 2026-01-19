cmake_minimum_required(VERSION 3.19)
project(ApplicationUI LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(ApplicationUI
    WIN32 MACOSX_BUNDLE
    main.cpp
    MainWindow.cpp
    MainWindow.hpp
    MainWindow.ui


    ViewportWidget.hpp ViewportWidget.cpp ViewportWidget.ui
    ScrollButton.hpp ScrollButton.cpp
    ViewportManager.hpp
    ViewportManager.cpp
    ViewportRenderWindow.hpp
    ViewportRenderWindow.cpp
    VulkanBackend.hpp
    VulkanBackend.cpp

    MenuStackedWidget.hpp MenuStackedWidget.cpp
    ShortcutButton.hpp ShortcutButton.cpp
    Resources.qrc
    SubWindows/SubWindowBase.hpp
    SubWindows/MaterialAssignDialog.hpp SubWindows/MaterialAssignDialog.cpp SubWindows/MaterialAssignDialog.ui
    SubWindows/SubWindowManager.hpp
    SubWindows/PropertyWindow.hpp SubWindows/PropertyWindow.cpp SubWindows/PropertyWindow.ui
    SubWindows/PropertyItem.hpp
    SubWindows/MaterialEditorDialog.hpp SubWindows/MaterialEditorDialog.cpp SubWindows/MaterialEditorDialog.ui
)

set_target_properties(ApplicationUI PROPERTIES OUTPUT_NAME "Imp3d")

# Include directories for CoreLib
target_include_directories(ApplicationUI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../CoreLib/Core/include
    ${CMAKE_CURRENT_LIST_DIR}/../CoreLib/Core/src
)

target_link_libraries(ApplicationUI
    PRIVATE
        Qt::Core
        Qt::Widgets
        CoreLib
)

include(GNUInstallDirs)

install(TARGETS ApplicationUI
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET ApplicationUI
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
