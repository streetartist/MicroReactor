# MicroReactor Multi-Entity Example
# Demonstrates middleware, mixins, and entity-to-entity communication

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(example_multi_entity)
