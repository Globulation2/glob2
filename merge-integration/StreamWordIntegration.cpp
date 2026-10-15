#include <ChecksumStream.h>
#include <WordChecksum.h>
#include <array>
#include <cassert>
#include <iostream>
#include <string_view>
int main() {
    GAGCore::ChecksumStream stream;
    GAGCore::OutputStream& output=stream;
    const std::string_view backing="prefix-field-suffix";
    const auto name=backing.substr(7,5);
    GAGCore::WordChecksum word;
    for (auto value : std::array<std::uint32_t,5>{0,1,0xffffffffu,0x80000000u,0x12345678u}) {
        output.writeUint32(value,name);
        word.addUint32(value);
    }
    output.writeOffset32(123,name);
    assert(stream.value == 0x53ff7653u);
    assert(stream.getPosition() == 24);
    assert(word.value() == 0xbe4aafacu);
    std::cout << "Stream/word integration passed: canonical bytes, borrowed field-name view, offset dispatch, word reference vector\n";
}
