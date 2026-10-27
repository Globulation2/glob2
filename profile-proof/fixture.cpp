#include <cstdlib>
#include <string>
#include <vector>
template<class T> int classify(const T& values) {
    int total=0;
    for (const auto& value:values) total += value.size()>2 ? 2 : 1;
    return total;
}
int uncalled(int value) { return value>0 ? 1 : 0; }
int main(int argc,char** argv) {
    const int value=std::atoi(argv[1]);
    std::vector<std::string> words={"a",value%2 ? "long" : "b"};
    const int total=classify(words);
    return value==7 && total>0 ? 3 : 0;
}
