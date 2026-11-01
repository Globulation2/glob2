#include <string>
#include <iostream>
#include <iomanip>
#include <fstream>
struct BasePlayer { enum { MAX_NAME_LENGTH=32 }; };
struct Settings { std::string username; void setUsername(std::string); };
void Settings::setUsername(std::string s) { username.assign(s, 0, BasePlayer::MAX_NAME_LENGTH); }
int main(){ Settings settings; std::string input="ককককককককক্ষ"; settings.setUsername(input); std::cout << input.size()<<" "<<settings.username.size()<<"\n"; for(unsigned char b: settings.username)std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b); std::cout<<"\n"; std::ofstream("stored-username.bin",std::ios::binary).write(settings.username.data(),settings.username.size()); }
