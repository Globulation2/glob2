cmake_minimum_required(VERSION 3.13)

add_library(usl
    src/code.cpp
    src/debug.cpp
    src/interpreter.cpp
    src/lexer.cpp
    src/memory.cpp
    src/parser.cpp
    src/position.cpp
    src/token.cpp
    src/tokenizer.cpp
    src/tree.cpp
    src/types.cpp
    src/usl.cpp
    )
