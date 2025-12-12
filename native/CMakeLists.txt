cmake_minimum_required(VERSION 3.20)
project(SNSGuardianBrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
  message(FATAL_ERROR "This project now targets Linux only (GTK + WebKit2GTK).")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(WEBKIT2GTK QUIET IMPORTED_TARGET webkit2gtk-4.1)
if (NOT WEBKIT2GTK_FOUND)
  pkg_check_modules(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.0)
endif()

add_executable(sns_guardian_browser main_linux.cpp)
target_link_libraries(sns_guardian_browser PRIVATE PkgConfig::GTK3 PkgConfig::WEBKIT2GTK)
