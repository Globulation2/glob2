#include "/home/bradley/glob2-gradient-unified-h128/src/map/gradient/AdaptiveGradientModel.h"
#include <iostream>
int main(){AdaptiveGradientModel::BaseFeatures b;AdaptiveGradientModel::MovementCounts c;while(std::cin>>b[0]){for(unsigned i=1;i<b.size();++i)std::cin>>b[i];for(auto&v:c)std::cin>>v;std::cout<<AdaptiveGradientModel::predict(b,c)<<"\n";}}
