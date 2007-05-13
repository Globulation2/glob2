set(libusl_SOURCES
interpreter.cpp
lexer.cpp
memory.cpp
position.cpp
token.cpp
tokenizer.cpp
tree.cpp
types.cpp
usl.cpp
)

include_directories(${libusl_SOURCE_DIR}/include)

add_executable(usl ${libusl_SOURCES})
