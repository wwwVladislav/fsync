cmake_minimum_required(VERSION 3.1)
project(sqlite)

use_c99()

add_library(sqlite STATIC src/sqlite3.h src/sqlite3.c)
