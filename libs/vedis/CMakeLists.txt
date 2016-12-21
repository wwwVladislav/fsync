cmake_minimum_required(VERSION 2.6)
project(vedis)

use_c99()

add_library(vedis STATIC src/vedis.h src/vedis.c)
