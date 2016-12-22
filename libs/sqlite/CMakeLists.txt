cmake_minimum_required(VERSION 2.6)
project(sqlite)

use_c99()

add_library(sqlite STATIC src/sqlite3.h src/sqlite3.c)
