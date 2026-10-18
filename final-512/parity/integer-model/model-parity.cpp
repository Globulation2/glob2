#include "ResourceGradientModel.h"
#include <iostream>
int main(){ std::array<std::int64_t,7> v;while(std::cin>>v[0]){for(int i=1;i<7;++i)std::cin>>v[i];std::cout<<ResourceGradientModel::predict(v)<<"\n";} }
