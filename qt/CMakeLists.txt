cmake_minimum_required(VERSION 3.20)
project(tandem-kirigami VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus Widgets)
find_package(KF6Kirigami ${KF_MIN_VERSION} REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

qt_add_executable(tandem-kirigami
  src/main.cpp
  src/daemon.cpp
  src/device.cpp
  src/frontend.cpp
)

set_source_files_properties(qml/PluginTitles.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(tandem-kirigami
  URI de.flk.Tandem
  VERSION 1.0
  RESOURCE_PREFIX /
  NO_PLUGIN
  QML_FILES
    qml/Main.qml
    qml/PluginTitles.qml
    qml/DevicesPage.qml
    qml/DevicePage.qml
    qml/SmsPage.qml
    qml/ConversationPage.qml
    qml/SettingsPage.qml
    qml/PresenterWindow.qml
    qml/ReplyDialog.qml
    qml/AddCommandDialog.qml
)

target_compile_definitions(tandem-kirigami PRIVATE TANDEM_VERSION="${PROJECT_VERSION}")
target_link_libraries(tandem-kirigami PRIVATE
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Widgets
)

install(TARGETS tandem-kirigami ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES data/de.flk.Tandem.desktop DESTINATION ${KDE_INSTALL_APPDIR})
configure_file(data/de.flk.Tandem.service.in ${CMAKE_CURRENT_BINARY_DIR}/de.flk.Tandem.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/de.flk.Tandem.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
