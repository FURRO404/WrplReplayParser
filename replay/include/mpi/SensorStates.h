#pragma once

#include <cstdint>
#include <vector>
#include "danet/BitStream.h"
#include "math/dag_Point3.h"
#include "ecs/entityId.h"

#define COUNTER_MEASURES_COUNT 2
#define SENSORS_COUNT          4
#define TARGETS_NUM            8


struct SensorsControlStates {
  // a bunch of this is probably a union actually
  bool v1 = 0;
  bool v2 = 0;
  bool first_bool = false; // maybe is turned on?
  float unpacked_1 = 0;
  float unpacked_2 = 0;
  float unpacked_3 = 0;
  int field149_0xa4 = 0;
  int field150_0xa8 = 0;
  uint8_t field132_0x84 = 0;
  uint8_t field133_0x85 = 0;
  std::vector<uint32_t> field4_0x4{};
  uint8_t sensor_type_maybe = 0;
  uint8_t field136_0x88 = 0;
  uint8_t field137_0x89 = 0;
  uint8_t field138_0x8a = 0;
  float some_data_1{};
  float some_data_2{};
  float some_data_3{};
  float some_data_4{};
  float some_data_5{};
  char some_data_6[4]{};
  int some_data_7;
  float field147_0xa8;

  bool deserialize(BitStream &bs);
};

struct TargetDesignationControlState {
  uint8_t v1;
  uint8_t v2;
  bool v3;
  Point3 v4;
  bool write_compressed;
  float v5;
  Point3 v6;
  Point3 v7;
  bool v8;
  Point3 v9;
  uint8_t v10;
  float v11;
  bool v12;
  bool v13;
  uint8_t v14;
  uint32_t v15;

  bool deserialize(BitStream &bs);
};

struct CounterMeasuresControlState {
  uint8_t v1;
  uint8_t v2;

  bool deserialize(BitStream &bs);
};

namespace unit {
  class Unit;
}

namespace mpi {
  /// One sensor of one unit at one sync. An aircraft sends its sensors with every flight
  /// update; a ground vehicle with every vehicle update that carries its state.
  struct SensorEvent {
    uint32_t time_ms = 0;
    unit::Unit *unit = nullptr;
    uint8_t index = 0; ///< position of the sensor in the unit's list for this sync
    /// Byte that follows the sensor list of the sync, the same for every sensor in it.
    uint8_t list_tail = 0;
    SensorsControlStates state{};
  };

  /// One target designation of one unit at one sync.
  struct DesignationEvent {
    uint32_t time_ms = 0;
    unit::Unit *unit = nullptr;
    uint8_t index = 0;
    TargetDesignationControlState state{};
  };

  enum SeekerSource : uint8_t {
    SeekerWeapon = 0, ///< a guided store in flight, from WeaponSync
    SeekerAircraft = 1, ///< the seeker of a store still on an aircraft, from FMSync
    SeekerGround = 2, ///< the seeker of a ground vehicle's missile, from GMSync
  };

  /// A seeker block, kept as the raw bits. Its layout is not decoded yet.
  struct SeekerEvent {
    uint32_t time_ms = 0;
    SeekerSource source = SeekerWeapon;
    ecs::EntityId eid{}; ///< the store, for SeekerWeapon
    unit::Unit *unit = nullptr; ///< the carrier, for SeekerAircraft and SeekerGround
    float head_f = 0; ///< SeekerWeapon only: the float before the block
    bool head_b = false; ///< SeekerWeapon only: the bit before the block
    uint32_t bits = 0;
    /// The bits as BitStream::ReadBits gives them: whole bytes first, the last
    /// partial byte right-aligned.
    std::vector<uint8_t> data{};
  };
} // namespace mpi
