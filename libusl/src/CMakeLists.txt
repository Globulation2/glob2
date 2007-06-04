set(libusl_SOURCES
code.cpp
code.h
interpreter.cpp
interpreter.h
lexer.cpp
lexer.h
memory.cpp
memory.h
position.cpp
position.h
token.cpp
token.h
tokenizer.cpp
tokenizer.h
tree.cpp
tree.h
types.cpp
types.h
usl.cpp
)

include_directories(${libusl_SOURCE_DIR}/include)

add_executable(usl ${libusl_SOURCES})
