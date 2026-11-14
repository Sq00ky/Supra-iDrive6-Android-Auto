// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef TEST6WB_PLAN_HPP
#define TEST6WB_PLAN_HPP
#include "patterns/patterns.hpp"
namespace test6wb {
struct Trial {
    unsigned id,testId,handle;bool isolate,clear;
    std::string description,parameters;
    nbt::hudterm::Batch batch;
};
struct TestDefinition {unsigned id,handle;const char* description;};
const TestDefinition* catalog(size_t& count);
bool number(const std::string& text,unsigned max,unsigned& value);
bool plan(const std::vector<std::string>& args,const std::string& variant,
          std::vector<Trial>& trials,std::string& error);
}
#endif
