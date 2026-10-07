#include <string>

class DataBlock;
struct ParserState;

#pragma once
namespace unit {
  struct BallisticParams {
    float mass = 0.f;
    float area = 0.f;
    float cx = 0.f;
    float force = 0.f; // motor thrust, zero for a bomb
    float time_fire = 0.f; // burn time
    float mass_end = 0.f; // mass once the motor has burnt out

    // tries to load ballistic data from a given datablock and a section, usually called by lookupBallisticData
    bool tryLoad(const DataBlock *blk, const char *section);

    // tries to lookup ballistic data for a particular weapon, caches data in ParserState
    static const BallisticParams *lookupBallisticData(ParserState &state, const std::string &weapon_id);
  };
} // namespace unit
