set(libusl_SOURCES
lexer.cpp
position.cpp
token.cpp
tokenizer.cpp
tree.cpp
usl.cpp
interpreter.cpp
)

include_directories(${libusl_SOURCE_DIR}/include)

add_executable(usl ${libusl_SOURCES})
