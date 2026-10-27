# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(STR_M2_Drone)

# Configuracao experimental restrita a este projeto. Nao altera o SDK global.
include(cmake/m2_scheduler.cmake)
