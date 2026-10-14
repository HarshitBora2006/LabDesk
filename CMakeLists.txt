cmake_minimum_required(VERSION 3.19)

project(LabDesk
    VERSION 0.7.3
    DESCRIPTION "Smart Computer Lab Management and CPU Scheduling System"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LABDESK_BUILD_TESTS "Build scheduler_tests, labqueue_tests and database_tests" ON)

function(labdesk_set_warnings target)
    if(MSVC)
        # /utf-8: sources contain a few non-ASCII characters (check marks in logs)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# OS engine: pure C++17, no Qt. Created BEFORE AUTOMOC is switched on.
# ---------------------------------------------------------------------------
add_library(labdesk_os STATIC
    src/scheduler.cpp
    src/scheduler.h
    src/processstate.cpp
    src/processstate.h
    src/labqueue.cpp
    src/labqueue.h
)
target_include_directories(labdesk_os PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
labdesk_set_warnings(labdesk_os)

# ---------------------------------------------------------------------------
# Qt 6
# ---------------------------------------------------------------------------
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Sql)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Database + services: Qt Core/Sql only, no widgets (so tests need no GUI).
add_library(labdesk_core STATIC
    src/database.cpp            src/database.h
    src/studentservice.cpp      src/studentservice.h
    src/computerservice.cpp     src/computerservice.h
    src/sessionservice.cpp      src/sessionservice.h
    src/maintenanceservice.cpp  src/maintenanceservice.h
    src/processservice.cpp      src/processservice.h
    src/queueservice.cpp        src/queueservice.h
    src/passwordhasher.cpp      src/passwordhasher.h
    src/timeutil.cpp            src/timeutil.h
    src/models.h
    src/opresult.h
    src/services.h
)
target_include_directories(labdesk_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(labdesk_core PUBLIC Qt6::Core Qt6::Sql labdesk_os)
labdesk_set_warnings(labdesk_core)

# Desktop application.
set(LABDESK_UI_SOURCES
    src/mainwindow.cpp                 src/mainwindow.h
    src/ui/page.h
    src/ui/theme.cpp                   src/ui/theme.h
    src/ui/uihelpers.cpp               src/ui/uihelpers.h
    src/ui/layouthelpers.cpp           src/ui/layouthelpers.h
    src/ui/dashboardpage.cpp           src/ui/dashboardpage.h
    src/ui/studentspage.cpp            src/ui/studentspage.h
    src/ui/computerspage.cpp           src/ui/computerspage.h
    src/ui/maintenancepage.cpp         src/ui/maintenancepage.h
    src/ui/sessionspage.cpp            src/ui/sessionspage.h
    src/ui/labqueuepage.cpp            src/ui/labqueuepage.h
    src/ui/studentportalpage.cpp       src/ui/studentportalpage.h
    src/ui/schedulerpage.cpp           src/ui/schedulerpage.h
    src/ui/ganttchartwidget.cpp        src/ui/ganttchartwidget.h
    src/ui/processstatepage.cpp        src/ui/processstatepage.h
    src/ui/statediagramwidget.cpp      src/ui/statediagramwidget.h
)

add_executable(labdesk WIN32 MACOSX_BUNDLE
    src/main.cpp
    ${LABDESK_UI_SOURCES}
    resources/resources.qrc
)
target_include_directories(labdesk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)
target_link_libraries(labdesk PRIVATE labdesk_core Qt6::Gui Qt6::Widgets)
labdesk_set_warnings(labdesk)

install(TARGETS labdesk
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .)

# ---------------------------------------------------------------------------
# Tests (run with: ctest --test-dir build --output-on-failure)
# ---------------------------------------------------------------------------
if(LABDESK_BUILD_TESTS)
    enable_testing()

    add_executable(scheduler_tests tests/scheduler_tests.cpp)
    target_link_libraries(scheduler_tests PRIVATE labdesk_os)
    labdesk_set_warnings(scheduler_tests)
    add_test(NAME scheduler_tests COMMAND scheduler_tests)

    add_executable(labqueue_tests tests/labqueue_tests.cpp)
    target_link_libraries(labqueue_tests PRIVATE labdesk_os)
    labdesk_set_warnings(labqueue_tests)
    add_test(NAME labqueue_tests COMMAND labqueue_tests)

    # The schema is an embedded resource, so the test embeds it too.
    add_executable(database_tests tests/database_tests.cpp resources/resources.qrc)
    target_link_libraries(database_tests PRIVATE labdesk_core)
    labdesk_set_warnings(database_tests)
    add_test(NAME database_tests COMMAND database_tests)
endif()
