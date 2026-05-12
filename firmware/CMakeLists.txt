cmake_minimum_required(VERSION 3.16)

# Inclui o ESP-IDF via variável de ambiente IDF_PATH
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(vitalkare-firmware VERSION 1.0.0 LANGUAGES C CXX)

# Metadados do projeto
set(PROJECT_VER "1.0.0")
set(PROJECT_NAME "vitalkare-firmware")
