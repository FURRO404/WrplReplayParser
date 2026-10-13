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

  /// One engine of an aircraft at one flight-model sync. The engine count equals the EngineN blocks of
  /// the flight model, rocket boosters included.
  struct EngineSync {
    /// 7 runs; 8 destroyed (health 0); a start on the ground goes 0, 1, 2, 3, 4, 5, 7; 6 is probably a
    /// shut-down; a rocket booster reads 0 while not in use. At a kill it goes to 8 on jets, mostly 0 on props.
    uint8_t state = 0;
    /// Engine throttle with afterburner, 1.0 to 1.1, sent only while above 1.0 and only by engines with
    /// Afterburner.Type 3 and AfterburnerBoost > 1.0. Never by props (also at WEP), jets without
    /// afterburner, water injection or boosters. The sync of a dead aircraft never has it.
    std::optional<float> afterburner{};
    /// Engine health, sent only below 1.0; it starts within 5 s of a hit and is 0 at state 8.
    std::optional<float> health{};
    /// Radiator positions 0 to 255, 0 on a jet. Equal with a common radiator; water and oil differ
    /// otherwise (which is which is not known). 0 for the other aircraft of a client replay.
    uint8_t radiators[2]{};
  };

  /// The pilot's controls and the engines of one aircraft at one flight-model sync.
  struct ControlEvent {
    uint32_t time_ms = 0;
    unit::Unit *unit = nullptr;
    bool on_ground = false;
    /// The seven control bytes as sent. Byte 6 is always 0. Probable: byte 3 is 15 with the bomb bay open,
    /// the high nibble of byte 5 is the wing sweep.
    uint8_t controls[7]{};
    std::vector<EngineSync> engines{};
    /// Gun triggers, sent only while a trigger is held: the round counters of the guns on
    /// that trigger fall in the same sync. On a P-63 the four .50s are on the machine-gun
    /// trigger and the 37 mm on the cannon trigger; a jet's gun is on the cannon trigger.
    bool fire_mg = false;
    bool fire_cannon = false;
    /// Trigger of the additional guns (flight-model trigger "additional gun": gun pods, guns below the wings).
    bool fire_additional = false;

    /// Stick and pedals, -1 to 1 in steps of 1/7, sign as sent. Pitch is positive for a pull.
    float pitch() const { return ((controls[0] & 15) - 8) / 7.f; }
    float roll() const { return ((controls[1] >> 4) - 8) / 7.f; }
    float rudder() const { return ((controls[1] & 15) - 8) / 7.f; }
    /// 0 to 1 in steps of 1/15.
    float flaps() const { return (controls[0] >> 4) / 15.f; }
    /// 0 to 1 in steps of 1/15.
    float airbrake() const { return (controls[2] & 15) / 15.f; }
    float wheel_brake() const { return (controls[2] >> 4) / 15.f; }
    /// The throttle lever, 0 to 1 in steps of 1/15. Above 100% it stays at 1: see
    /// EngineSync::afterburner.
    float throttle() const { return (controls[5] & 15) / 15.f; }
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
    /// SeekerWeapon only: seconds since the seeker lost its target, 0 while it holds a lock
    /// and before its first lock. A lock on a flare counts as a lock.
    float lost_for = 0;
    /// SeekerWeapon only: seconds since launch, in steps of 1/48 s.
    std::optional<float> flight_time{};
    bool head_b = false; ///< SeekerWeapon only: the bit before the block
    uint32_t bits = 0;
    /// The bits as BitStream::ReadBits gives them: whole bytes first, the last
    /// partial byte right-aligned.
    std::vector<uint8_t> data{};
  };

  /// One store (missile or rocket) at one 0xF11A store sync, beside its position in the store's history.
  /// 0xF11A carries no gun shells.
  struct StoreSyncEvent {
    uint32_t time_ms = 0;
    ecs::EntityId eid{};
    /// Server time in steps of 1/48 s; 0 to 42 ms after the packet time.
    float server_time = 0;
    /// World velocity, m/s (int16 / 16).
    Point3 velocity{};
    /// Seconds since launch, in steps of 1/48 s.
    std::optional<float> flight_time{};
  };

  /// One weapon of a ground vehicle at one sync. A part of the values comes from the delta-coded
  /// state, the rest from the ammunition list after it; either can be missing.
  struct GroundWeaponSync {
    /// Probable: the weapon is disabled. It rises within 1 s of a hit and falls without one (repair).
    std::optional<bool> broken{};
    /// Probable: barrel heat, 0 to 1. A main gun gains about 0.2 per shot and loses about 0.016 per
    /// second; it reaches 1 on autocannons. Not sent for machine guns. It is not the reload progress.
    std::optional<float> heat{};
    /// Turret yaw and gun elevation relative to the hull, radians. The yaw takes the sign of the hull
    /// heading: the F0CC gun yaw minus the heading equals it (median 0.31 deg).
    std::optional<float> turret_yaw{};
    std::optional<float> turret_pitch{};
    /// Ammunition type in the gun and the type that comes next, indexes into rounds.
    std::optional<uint8_t> loaded_type{};
    std::optional<uint8_t> selected_type{};
    /// Battle time in seconds of the weapon's last shot, and of the moment it can fire again.
    std::optional<float> last_shot{};
    std::optional<float> ready{};
    /// Rounds left per ammunition type; 65535 is unlimited.
    std::vector<uint16_t> rounds{};
  };

  /// One ground vehicle (or ship, or AI gun) at one 0xF074 sync. The motion values are there
  /// only when has_motion is set.
  struct GroundSyncEvent {
    uint32_t time_ms = 0;
    uint16_t uid = 0;
    unit::Unit *unit = nullptr; ///< null for an AI gun
    /// Two 4-bit spawn counters of the player, constant for a vehicle's life; both rise at each
    /// new spawn. Which nibble counts what is not known.
    uint8_t spawn_counter = 0;
    bool has_motion = false;
    bool is_ship = false; ///< the ship block was sent (some AI ships send the tank layout)
    /// Ship block. Throttle 127 stop, 254 full ahead, 0 full reverse; rudder 127 centre.
    std::optional<uint8_t> ship_throttle{};
    std::optional<uint8_t> ship_rudder{};
    /// Probable: flooding, and three water levels that rise with it; s0 also rises at the kill while the ship sinks.
    bool ship_flooding = false;
    uint8_t ship_water[3]{};
    /// One per transmission_NN_dm part (propeller shaft), 255 intact, falls with hits.
    std::vector<uint8_t> ship_shafts{};
    Point3 velocity{}; ///< world axes, m/s
    /// Rad/s about the body axes, in the order of the unit's euler: roll, yaw, pitch.
    Point3 angular_velocity{};
    /// Probable: the physics body is not at rest. Never 0 while the vehicle moves.
    bool awake = false;
    /// Track (or wheel side) speeds in m/s, by the measured scale of 0.1266 m/s per step.
    std::optional<float> track_left{};
    std::optional<float> track_right{};
    /// Probable: steering input, -1 to 1.
    std::optional<float> steer{};
    /// Drive input: 1 forward, 0, -1 reverse.
    std::optional<float> throttle{};
    /// Probable: engine speed, 0 to 255; 0 when the engine stops, also at the kill.
    uint8_t engine = 0;
    /// Probable: the vehicle is alive; falls within 100 ms of the kill.
    bool alive = false;
    /// A player sits in the vehicle. Falls about 10 s after the kill; 0 on AI guns.
    bool player_controlled = false;
    /// The point under the player's aim, while player_controlled: heading in radians (the sign of
    /// the hull heading), and probable: pitch in radians and the distance in metres (10 km at most).
    std::optional<float> aim_yaw{};
    std::optional<float> aim_pitch{};
    std::optional<float> aim_distance{};
    std::vector<GroundWeaponSync> weapons{};
  };

  /// The decoded fields of a seeker block. The block length tells the seeker. Of a store in
  /// flight: 607 bits radar, 639 bits radar at the change from search to track, 283 bits IR.
  /// Of a store still on an aircraft: 518 bits (IR, or no target), 599 bits (radar, no
  /// target) and 895 bits (radar target); only some of their fields are known.
  struct SeekerState {
    bool decoded = false;
    /// True in track, false in search; empty for an IR seeker, whose lock bit is not known.
    std::optional<bool> tracking{};
    /// Unit line of sight from the missile, world axes.
    Point3 los{};
    /// Radar only: the seeker's range in metres. It reads 0 to 15% above the true distance.
    std::optional<float> range{};
    /// Radar only: the seeker's estimate of the target position, world axes. In the 895-bit
    /// block of an aircraft, the target the missile gets at launch.
    std::optional<Point3> target_pos{};
    /// 518-bit aircraft block: 0 no lock (also after each launch), 2 seeker on at the boresight, 4 the seeker
    /// axis on an aircraft (median 0.32 deg), 3 probable: a target, but the axis not on an aircraft. 6 also
    /// occurs, with another layout.
    std::optional<uint8_t> ir_state{};
    /// 518-bit aircraft block: battle time in seconds of the last change out of state 0 or 2; a change between
    /// 3 and 4 keeps it. Empty before the first.
    std::optional<float> lock_time{};
    /// 518-bit aircraft block: the seeker axis, a unit vector in world axes; in states 0 and 2 the aircraft's
    /// forward axis about 2 deg down.
    std::optional<Point3> seeker_dir{};
    /// 518-bit aircraft block: gimbal angles in radians relative to the aircraft, azimuth positive to the left
    /// and elevation positive up; 0 while caged, they stop at the missile's angleMax.
    std::optional<float> gimbal_az{};
    std::optional<float> gimbal_el{};
    /// 518-bit aircraft block: half the seeker fov in radians (0.0314 for an AIM-9M).
    std::optional<float> fov_half{};
  };

  SeekerState DecodeSeeker(const SeekerEvent &ev);
} // namespace mpi
