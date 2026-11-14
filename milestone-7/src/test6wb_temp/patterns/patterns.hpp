// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef TEST6WB_PATTERNS_HPP
#define TEST6WB_PATTERNS_HPP
#include "profile/surfaces.hpp"
#include <string>
namespace test6wb {
nbt::hudterm::Batch clearBatch();
bool validate(const nbt::hudterm::Batch& batch,std::string& error);
bool vectorFill(const Surface& s,nbt::hudterm::Batch& batch,std::string& error);
bool identifiedPattern(const Surface& s,unsigned trial,const std::string& variant,
                       const std::string& colour,nbt::hudterm::Batch& batch,std::string& error,bool border=false);
bool textPattern(unsigned handle,const std::string& text,nbt::hudterm::Batch& batch,std::string& error);
bool barPattern(unsigned value,nbt::hudterm::Batch& batch,std::string& error);
nbt::hudterm::Batch frameBatch(bool on);
}
#endif
