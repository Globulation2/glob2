// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "usl.h"
#include "native.h"
#include "position.h"
#include <fstream>
#include <sstream>

namespace
{
int evaluate(Usl& usl,const std::string& source)
{
    std::istringstream input("result := { "+source+" }");
    usl.includeScript("coverage",input);
    auto* value=dynamic_cast<Integer*>(usl.getConstant("result"));
    REQUIRE(value!=nullptr);
    return value->value;
}
}
TEST_SUITE("USLCoverage")
{
    TEST_CASE("hostile calls arithmetic and nesting fail without process termination") {
        for (const std::string source : {std::string("1(2)"), std::string("2147483647 + 1"),
                std::string("50000 * 50000"), std::string("9999999999999999999999999"),
                std::string(512, '(') + "1" + std::string(512, ')')}) {
            Usl usl(false);
            CHECK_THROWS_AS(evaluate(usl, source), Exception);
            usl.collectGarbage();
        }
        Usl usl(false);
        std::string chain = "1";
        for (int i=0; i<1024; ++i) chain += " + 1";
        CHECK_THROWS_AS(evaluate(usl, chain), Exception);
        CHECK_THROWS_AS(evaluate(usl, "def f(x) := 1 + f(x)\nf(0)"), Exception);
    }

    TEST_CASE("native argument type mismatch raises a script error") {
        Usl usl;
        CHECK_THROWS_AS(evaluate(usl, "1 + \"text\""), Exception);
    }

    TEST_CASE("released expression and recursion fixtures yield exact values")
    {
        struct Fixture { const char* name; int expected; };
        for (const auto& entry : {Fixture{"42",42},Fixture{"if",1},Fixture{"fib",36}})
        {
            INFO(std::string(entry.name));
            Usl usl;
            if (std::string(entry.name)=="fib")
            {
                std::ifstream prelude(glob2test::sourceRoot()/"libusl/test/if.usl");
                REQUIRE(prelude.good()); usl.includeScript("prelude",prelude);
            }
            std::ifstream source(glob2test::sourceRoot()/(std::string("libusl/test/")+entry.name+".usl"));
            REQUIRE(source.good());
            std::ostringstream text; text<<source.rdbuf();
            CHECK(evaluate(usl,text.str())==entry.expected);
            usl.collectGarbage(); usl.collectGarbage();
            auto* retained=dynamic_cast<Integer*>(usl.getConstant("result"));
            REQUIRE(retained!=nullptr); CHECK(retained->value==entry.expected);
        }
    }

    TEST_CASE("native arithmetic closures selection and strings survive garbage collection")
    {
        for (const auto& entry : {std::pair{"7 * 6 - 3",39},
                                 std::pair{"val x := 8\ndef f(y) := x + y\nf(5)",13},
                                 std::pair{"val v := [ def answer := 42 ]\nv.answer",42}})
        {
            Usl usl; INFO(std::string(entry.first));
            CHECK(evaluate(usl,entry.first)==entry.second);
            CHECK(usl.getConstant("missing")==nullptr);
            usl.setConstant("kept",new String(&usl.heap,"quoted text"));
            usl.collectGarbage(); usl.collectGarbage();
            auto* value=dynamic_cast<String*>(usl.getConstant("kept"));
            REQUIRE(value!=nullptr); CHECK(value->value=="quoted text");
        }
    }

    TEST_CASE("yielding threads respect execution budgets and resume independently")
    {
        Usl usl; std::vector<int> recorded;
        usl.prototype->addMethod(new NativeFunction<void(int)>("record",[&](int value){recorded.push_back(value);}));
        std::istringstream first("record(1)\n yield(0)\n record(3)");
        std::istringstream second("record(2)\n yield(0)\n record(4)");
        usl.createThread("first",first); usl.createThread("second",second);
        CHECK(usl.run(0)==0); CHECK(recorded.empty());
        usl.run(100);
        CHECK(recorded==std::vector<int>{1,2});
        CHECK(usl.threads[0].state==Thread::YIELD);
        usl.collectGarbage();
        usl.run(100);
        CHECK(recorded==std::vector<int>{1,2,3,4});
        CHECK(usl.threads[0].state==Thread::STOP);
        CHECK(usl.threads[1].state==Thread::STOP);
        CHECK(usl.run(100)==0);
    }

    TEST_CASE("malformed syntax and missing members produce source-located errors")
    {
        for (const char* source : {"val :=", "f := fun(x) =>", "x := [", "unknown(3)", "1 missingMember"})
        {
            Usl usl; bool rejected=false;
            INFO(std::string(source));
            try { evaluate(usl,source); }
            catch (const Exception& error)
            {
                rejected=true;
                CHECK(error.position.filename=="coverage");
                CHECK(std::string(error.what()).size()>0);
            }
            CHECK(rejected);
        }
    }
    TEST_CASE("root replacement and collection after compiler errors preserve live object methods")
    {
        Usl usl;
        std::istringstream source("kept := [ def answer := 42 ]");
        usl.includeScript("objects",source);
                for (int round=0;round<20;++round)
        {
            const auto before=usl.root->locals.size();
            usl.setConstant("scratch",new String(&usl.heap,std::string(4096,'x')));
            CHECK(usl.root->locals.size()==before+(round==0 ? 1 : 0));
            std::istringstream broken("val :=");
            CHECK_THROWS_AS(usl.includeScript("broken",broken),Exception);
            usl.collectGarbage(); CHECK(evaluate(usl,"kept.answer")==42);
        }
        usl.setConstant("scratch",new String(&usl.heap,"replacement"));
        usl.collectGarbage();
        auto* retained=dynamic_cast<String*>(usl.getConstant("scratch"));
        REQUIRE(retained); CHECK(retained->value=="replacement");
    }

    TEST_CASE("replacing included constants updates expression access and keeps live closures")
    {
        Usl usl;
        std::istringstream source("number := 7\nkept := [ def answer := 42 ]");
        usl.includeScript("constants",source);
        usl.setConstant("number",new Integer(&usl.heap,13));
        CHECK(evaluate(usl,"number")==13);
        usl.collectGarbage();
        CHECK(evaluate(usl,"kept.answer")==42);
    }

}
