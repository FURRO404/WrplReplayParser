#pragma once

#include <cstdint>
#include <optional>
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
  bool has_state = false; ///< kind 1: the record carries the values below the type byte
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

  // The meaning below was checked on 2.59 server and client replays of jet battles.

  /// Sensor kind: 1 radar, IRST or optical tracker (the transceiver tells which), 2 laser.
  uint8_t kind() const { return sensor_type_maybe >> 4; }
  /// Index of the sensor in the vehicle's `sensors { sensor {...} }` list (aircraft
  /// flightmodels/*.blk, ground units/tankmodels/*.blk).
  uint8_t slot() const { return sensor_type_maybe & 0xf; }
  bool on() const { return first_bool; }
  /// Indexes into the sensor blk (gamedata/sensors/*.blk), each in the key order of its
  /// block: `transivers`, `scanPatterns`, `signals`. None when the sensor has none.
  std::optional<uint8_t> transceiver() const {
    return kind1() && field136_0x88 != 0xf ? std::optional<uint8_t>(field136_0x88) : std::nullopt;
  }
  std::optional<uint8_t> scan_pattern() const {
    return kind1() && field137_0x89 != 0x3f ? std::optional<uint8_t>(field137_0x89) : std::nullopt;
  }
  std::optional<uint8_t> signal() const {
    return kind1() && field138_0x8a != 0xf ? std::optional<uint8_t>(field138_0x8a) : std::nullopt;
  }
  /// Battle time in seconds of the last mode change.
  std::optional<float> mode_time() const { return kind1() ? std::optional<float>(some_data_1) : std::nullopt; }
  /// Scan centre in radians, relative to the vehicle: yaw about +Y, then pitch about +Z
  /// (after the vehicle's own yaw, pitch and roll). In search, the zone the player set; in
  /// track, toward the target, up to the scan pattern's azimuth and elevation limits.
  std::optional<float> scan_az() const { return kind1() ? std::optional<float>(some_data_4) : std::nullopt; }
  std::optional<float> scan_el() const { return kind1() ? std::optional<float>(some_data_5) : std::nullopt; }
  /// Unit uids of the targets the sensor detects at this sync (search, TWS track files, the
  /// tracked target). A contact names a unit as 0xFFFF0000 | uid; other values are left out.
  std::vector<uint16_t> contact_uids() const {
    std::vector<uint16_t> out;
    for (uint32_t c: field4_0x4)
      if ((c >> 16) == 0xFFFF)
        out.push_back(c & 0xFFFF);
    return out;
  }

private:
  /// A kind-1 record that is on and has its state carries the values above.
  bool kind1() const { return first_bool && kind() == 1 && has_state; }
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

  /// Unit uid of the designated target (the radar's track, or its TWS designation). Only a
  /// kind-6 record whose v15 is 0xFFFF0000 | uid names one; other v15 values are not uids.
  std::optional<uint16_t> target_uid() const {
    return v1 == 6 && (v15 >> 16) == 0xFFFF ? std::optional<uint16_t>(v15 & 0xFFFF) : std::nullopt;
  }
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

  /// A seeker block, kept as the raw bits; DecodeSeeker reads the known fields.
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

  /// The decoded fields of a SeekerWeapon block. The block length tells the seeker:
  /// 607 bits radar, 639 bits radar at the change from search to track, 283 bits IR.
  /// Other lengths (seekers of stores still on an aircraft) are not decoded.
  struct SeekerState {
    bool decoded = false;
    /// True in track, false in search; empty for an IR seeker, whose lock bit is not known.
    std::optional<bool> tracking{};
    /// Unit line of sight from the missile, world axes.
    Point3 los{};
    /// Radar only: the seeker's range in metres. It reads 0 to 15% above the true distance.
    std::optional<float> range{};
    /// Radar only: the seeker's estimate of the target position, world axes.
    std::optional<Point3> target_pos{};
  };

  SeekerState DecodeSeeker(const SeekerEvent &ev);
} // namespace mpi
