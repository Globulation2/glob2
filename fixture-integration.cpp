#include "EngineFixtures.h"
#include "FileImport.h"
#include <fstream>
#include <iterator>
TEST_CASE("current browser replay imports with its complete telemetry footer" * doctest::test_suite("RepairIntegration")) {
 glob2test::HeadlessGlobals globals;
 std::ifstream file("/home/bradley/.codex/worktrees/repair-browser-png16/glob2/browser/tests/fixtures/cross-replay.replay",std::ios::binary);
 std::string bytes((std::istreambuf_iterator<char>(file)),{}); REQUIRE(bytes.size()>1000000);
 auto validate=[](const std::string& bytes){
  FileImport operation({"Integration.replay",std::vector<unsigned char>(bytes.begin(),bytes.end())},"replay");
  for(unsigned i=0;i<100000&&(operation.state()==FileImport::State::Validating||operation.state()==FileImport::State::Persisting);++i)operation.advance();
  return operation.state();
 };
 REQUIRE(validate(bytes)==FileImport::State::Succeeded);
 REQUIRE(validate(bytes.substr(0,bytes.size()-1))==FileImport::State::Failed);
 REQUIRE(validate(bytes+"x")==FileImport::State::Failed);
}
