#include <cassert>
#include <thread>
#include <atomic>
#include <iostream>
extern "C" int glob2_gradient_worker_context();
extern "C" void glob2_gradient_worker_wait(int*);
int main(){
 const int owner=glob2_gradient_worker_context();
 std::atomic<int> worker{0};
 std::thread thread([&]{worker=glob2_gradient_worker_context();int complete=1;glob2_gradient_worker_wait(&complete);});
 thread.join();std::cout<<"owner="<<owner<<" worker="<<worker<<'\n';
 assert(!owner);assert(worker==1);
}
