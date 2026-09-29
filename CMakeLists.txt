cmake_minimum_required(VERSION 3.21)
project(tinySA-App VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# auto-detect a Qt installed under C:/Qt if no prefix was given
if(NOT DEFINED CMAKE_PREFIX_PATH)
    file(GLOB _qt_candidates "C:/Qt/6.*/msvc*_64")
    if(_qt_candidates)
        list(SORT _qt_candidates)
        list(GET _qt_candidates -1 CMAKE_PREFIX_PATH)
        message(STATUS "Using Qt at ${CMAKE_PREFIX_PATH}")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets SerialPort PrintSupport Test)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /W3)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()

# --- core: device protocol, data model, maths. No QtWidgets. ---
qt_add_library(tinysa_core STATIC
    src/core/units.cpp
    src/core/model.cpp
    src/core/protocol.cpp
    src/core/sweepconfig.cpp
    src/core/simdevice.cpp
    src/core/link.cpp
    src/core/session.cpp
    src/core/appsettings.cpp
    src/core/traces.cpp
    src/core/csvio.cpp
    src/core/markers.cpp
)
target_include_directories(tinysa_core PUBLIC src/core)
target_link_libraries(tinysa_core PUBLIC Qt6::Core Qt6::Gui Qt6::SerialPort)

# --- QCustomPlot 2.1.1 (GPL), vendored unmodified ---
qt_add_library(qcustomplot STATIC vendor/qcustomplot/qcustomplot.cpp)
target_include_directories(qcustomplot PUBLIC vendor/qcustomplot)
target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)
if(MSVC)
    target_compile_options(qcustomplot PRIVATE /wd4996)  # deprecated Qt APIs in vendored code
endif()

# --- the application; screens are Qt Designer .ui files in src/ui ---
qt_add_executable(tinySA-App WIN32
    src/main.cpp
    src/ui/autotest.cpp
    src/ui/mainwindow.cpp
    src/ui/mainwindow.ui
    src/ui/mainfiles.cpp
    src/ui/maintest.cpp
    src/ui/spectrumview.cpp
    src/ui/spectrummouse.cpp
    src/ui/spectrummarkers.cpp
    src/ui/markermodel.cpp
    src/ui/mainmarkers.cpp
    src/ui/maintools.cpp
    src/ui/consolewindow.cpp
    src/ui/consolewindow.ui
    src/ui/batterywindow.cpp
    src/ui/batterywindow.ui
    res/tinySA-App.rc
)
target_include_directories(tinySA-App PRIVATE src/ui)
set_target_properties(tinySA-App PROPERTIES AUTOUIC_SEARCH_PATHS src/ui)
target_link_libraries(tinySA-App PRIVATE tinysa_core qcustomplot Qt6::Widgets Qt6::Test)

# --- unit tests for core ---
enable_testing()
qt_add_executable(tst_core test/tst_core.cpp)
target_link_libraries(tst_core PRIVATE tinysa_core Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)
