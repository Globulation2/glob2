// Prints Maxima's strategy schema or a resolved strategy as JSON, for
// MaximaStrategyConfigTest.py. Maxima needs no game-binary options: games read
// the same inputs from GLOB2_MAXIMA_* environment variables instead.
//
//   MaximaStrategyDump --dump-maxima-schema
//   MaximaStrategyDump --dump-maxima-strategy --maxima-format <format>
//       [--maxima-base <file>] [--maxima-layer <file>]... [--maxima-overrides <assignments>]
#include "../src/GlobalContainer.h"
#include "../src/AIMaximaStrategy.h"
#include <cstring>
#include <iostream>

GlobalContainer* globalContainer=nullptr;

int main(int argc, char* argv[])
{
    using namespace AIMaxima;
    bool dumpSchema=false;
    bool dumpStrategy=false;
    StrategyConfigOptions options;
    for(int i=1; i<argc; ++i)
    {
        const bool hasValue=i+1<argc;
        if(std::strcmp(argv[i], "--dump-maxima-schema")==0)
            dumpSchema=true;
        else if(std::strcmp(argv[i], "--dump-maxima-strategy")==0)
            dumpStrategy=true;
        else if(std::strcmp(argv[i], "--maxima-format")==0 && hasValue)
            options.explicitFormat=argv[++i];
        else if(std::strcmp(argv[i], "--maxima-base")==0 && hasValue)
            options.baseFile=argv[++i];
        else if(std::strcmp(argv[i], "--maxima-layer")==0 && hasValue)
            options.layerFiles.push_back(argv[++i]);
        else if(std::strcmp(argv[i], "--maxima-overrides")==0 && hasValue)
            options.inlineOverrides=argv[++i];
        else
        {
            std::cerr<<"unknown or incomplete argument: "<<argv[i]<<std::endl;
            return 2;
        }
    }
    if(dumpSchema)
    {
        std::cout<<StrategyResolver::schemaJson()<<std::endl;
        return 0;
    }
    if(!dumpStrategy)
    {
        std::cerr<<"pass --dump-maxima-schema or --dump-maxima-strategy"<<std::endl;
        return 2;
    }
    MatchFormat format;
    if(!StrategyResolver::parseFormat(options.explicitFormat, format))
    {
        std::cerr<<"--dump-maxima-strategy requires a valid --maxima-format"<<std::endl;
        return 2;
    }
    ResolvedStrategy strategy;
    std::string error;
    if(!StrategyResolver::resolveForFormat(options, format, strategy, error))
    {
        std::cerr<<"Maxima strategy error: "<<error<<std::endl;
        return 2;
    }
    std::cout<<StrategyResolver::resolvedJson(strategy)<<std::endl;
    return 0;
}
