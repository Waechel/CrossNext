cmake_minimum_required(VERSION 3.16)
# The Plus bootloader is ESP-IDF release/v6.1 9a97f6c5; apps from any other IDF
# lock up in early startup. ci/check_idf.py enforces this before and after the build.
set(PROJECT_VER "0.1.0-smoke")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(CrossNext-Plus-Smoke)
