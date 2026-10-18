// Offline experiment tree shared with the native inference harness.
#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
namespace ResourceExperiment {
struct Tree {
 struct Node {int feature,threshold,left,right,value;}; std::vector<Node> nodes;
 Tree(const char* env) { if(const char* p=std::getenv(env)) {std::ifstream in(p);Node n;while(in>>n.feature>>n.threshold>>n.left>>n.right>>n.value)nodes.push_back(n);if(nodes.empty())throw std::runtime_error("Empty experimental tree");} }
 int predict(const std::vector<int>& features) const {size_t i=0;for(size_t k=0;k<=nodes.size();++k){const auto& n=nodes.at(i);if(n.feature<0)return n.value;i=features.at(n.feature)<=n.threshold?n.left:n.right;}throw std::runtime_error("Cyclic experimental tree");}
 bool active()const{return !nodes.empty();}
};
}
