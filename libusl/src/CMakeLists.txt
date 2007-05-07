set(libusl_SOURCES
bytecode.cpp
lexer.cpp
position.cpp
token.cpp
tokenizer.cpp
types.cpp
usl.cpp
)

include_directories(${libusl_SOURCE_DIR}/include)

add_executable(usl ${libusl_SOURCES})
