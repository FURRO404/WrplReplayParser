#include "mpi/codegen/mpiUiStubs.h"
#include "init/initialize.h"
#include "state/ParserState.h"
#include "mpi/PositionSync.h"
#include "mpi/GeneralObject.h"
#include "idFieldSerializer.h"
#include "Unit.h"
#include "ecs/ComponentTypesDefs.h"
#include "Replay/ReplayReader.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>

std::vector<Rocket *> collect_all_rockets(ParserState &state);

static void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

static BitStream detection(unsigned a, unsigned b, bool short_fields, bool include_second = true) {
  BitStream bs;
  IdFieldSerializer32 sizes;
  bs.Write(uint16_t(0));
  bs.WriteCompressed(uint32_t(include_second ? 6 : 2));
  if (short_fields) { bs.Write(uint8_t(a)); sizes.setFieldSize(8); }
  else { bs.Write(uint16_t(a)); sizes.setFieldSize(16); }
  if (include_second) {
    if (short_fields) { bs.Write(uint8_t(b)); sizes.setFieldSize(8); }
    else { bs.Write(uint16_t(b)); sizes.setFieldSize(16); }
  }
  sizes.writeFieldsSize(bs);
  return bs;
}

static BitStream ground(bool truncate = false) {
  BitStream bs;
  bs.Write(uint16_t(0)); bs.Write(uint16_t(1));
  bs.Write(float(1)); bs.Write(uint16_t(0));
  bs.Write(true); bs.Write(false); bs.Write(false); // present, flag, extra
  bs.Write(false); bs.Write(false); // val2, val3
  bs.Write(false); bs.Write(false); bs.Write(uint8_t(1)); bs.Write(false); // uncompressed
  bs.AlignWriteToByteBoundary();
  bs.Write(uint8_t(9)); bs.Write(false); // spawn, no motion
  bs.Write(false); bs.Write(false); // no turret angles, no player
  bs.Write(false); bs.Write(uint8_t(0)); bs.Write(false); bs.Write(false); bs.Write(false);
  bs.Write(uint8_t(0)); bs.Write(uint8_t(0)); // sensors and countermeasures
  uint8_t zero = 0; bs.WriteBits(&zero, 4);
  if (truncate) return bs;
  bs.Write(false); // no aim
  bs.Write(true); bs.Write(true); // ammo list and weapon record
  bs.Write(uint8_t(0x21)); bs.Write(float(4)); bs.Write(float(5));
  bs.Write(uint8_t(2)); bs.Write(uint16_t(7)); bs.Write(uint16_t(8));
  bs.Write(false); // no seeker
  bs.AlignWriteToByteBoundary();
  return bs;
}

static BitStream aircraft(bool fail_second) {
  BitStream bs;
  bs.Write(false); bs.Write(false); bs.Write(false); bs.Write(false); // uid 1, data, flags
  bs.Write(false); bs.Write(uint32_t(0)); bs.Write(false); bs.Write(false);
  bs.WriteZigZag(1); bs.WriteZigZag(10); bs.WriteZigZag(0);
  bs.Write(Point3{}); bs.Write(uint32_t(0)); bs.Write(uint32_t(0));
  bs.AlignWriteToByteBoundary();
  uint8_t controls[7]{}; bs.Write(controls); bs.Write(uint8_t(0));
  bs.Write(uint8_t(0)); bs.Write(uint8_t(0));
  uint8_t zero = 0; bs.WriteBits(&zero, 4);
  bs.Write(false); bs.Write(false);
  if (fail_second) {
    bs.Write(false); bs.Write(false); bs.Write(false); bs.Write(false); // uid 2 is missing
  } else {
    bs.Write(true); bs.WriteCompressed(uint16_t(0x7ff));
    float tail[3]{}; bs.Write(tail);
  }
  return bs;
}

static void check_store_owner(const char *path) {
  ServerReplay replay(path);
  ParserState state(&replay);
  std::unique_ptr<IReplayReader> reader(replay.getReplayReader());
  ReplayPacket pkt{};
  while (reader->getNextPacket(pkt)) {
    if (!state.ParsePacket(pkt)) {
      require(pkt.type == ReplayPacketType::EndMarker, "aircraft failure in store test replay");
      break;
    }
  }
  for (auto *store : collect_all_rockets(state)) {
    auto *owner = store->owned_by;
    auto *weapon = store->weapon_obj;
    if (!owner || !weapon || !state.g_entity_mgr.doesEntityExist(owner->curr_eid))
      continue;
    const std::string name = weapon->weapon_name;
    require(state.g_entity_mgr.destroyEntity(owner->curr_eid, false, true), "owner destruction");
    require(store->owned_by == owner && store->weapon_obj == weapon && weapon->weapon_name == name,
            "store weapon did not survive owner destruction");
    std::cout << "store retained weapon after owner destruction\n";
    return; // The state and remaining stores are destroyed on exit from this function.
  }
  throw std::runtime_error("no suitable store and owner in test replay");
}

int main() {
  const char *game_path = std::getenv("WRPL_TEST_GAME_PATH");
  if (!game_path || !*game_path) {
    std::cerr << "Set WRPL_TEST_GAME_PATH to a War Thunder installation to run this test\n";
    return 77;
  }
  initialize(game_path, "", "");
  ParserState state(uint32_t(0));
  for (bool invalid : {false, true}) {
    mpi::UnitDetectedMessage msg(nullptr);
    msg.payload = detection(17, 29, invalid);
    bool ok = static_cast<mpi::Message &>(msg).readPayload(&state);
    require(ok != invalid, "detection width acceptance");
    if (ok) require(msg.spot.spotter_id == 17 && msg.spot.spotted_id == 29, "detection values");
  }
  mpi::UnitDetectedMessage absent(nullptr);
  absent.payload = detection(17, 0, false, false);
  require(!static_cast<mpi::Message &>(absent).readPayload(&state), "detection missing field");
  mpi::UnitDetectedMessage truncated(nullptr);
  truncated.payload = detection(17, 29, false);
  truncated.payload.SetWriteOffset(truncated.payload.GetWriteOffset() - 8);
  require(!static_cast<mpi::Message &>(truncated).readPayload(&state), "truncated detection table");
  mpi::UnitDetectedMessage oversized(nullptr);
  oversized.payload.Write(uint16_t(0));
  oversized.payload.WriteCompressed(uint32_t(6));
  oversized.payload.Write(uint8_t(17)); oversized.payload.Write(uint8_t(29));
  IdFieldSerializer32 oversized_sizes;
  oversized_sizes.setFieldSize(16); oversized_sizes.setFieldSize(16);
  oversized_sizes.writeFieldsSize(oversized.payload);
  require(!static_cast<mpi::Message &>(oversized).readPayload(&state), "oversized detection fields");
  mpi::UnitDetectedMessage bad_flags(nullptr);
  bad_flags.payload.Write(uint16_t(0));
  bad_flags.payload.Write(uint8_t(0x86));
  for (int i = 0; i < 4; ++i) bad_flags.payload.Write(uint8_t(0x80));
  bad_flags.payload.Write(uint16_t(17)); bad_flags.payload.Write(uint16_t(29));
  IdFieldSerializer32 bad_flags_sizes;
  bad_flags_sizes.setFieldSize(16); bad_flags_sizes.setFieldSize(16);
  bad_flags_sizes.writeFieldsSize(bad_flags.payload);
  require(!static_cast<mpi::Message &>(bad_flags).readPayload(&state), "unterminated detection field mask");
  state.keep.ground_syncs = true;
  auto gs = ground();
  require(GMSync(state, gs), "valid uncompressed ground");
  require(state.GroundSyncEvents.size() == 1 && state.GroundWeapons.size() == 1 && state.GroundRounds.size() == 2, "ground pools");
  require(state.GroundRounds[0] == 7 && state.GroundRounds[1] == 8, "ground ammo");
  auto bad_gs = ground(true);
  require(!GMSync(state, bad_gs), "truncated ground accepted");
  require(state.GroundSyncEvents.size() == 1 && state.GroundWeapons.size() == 1 && state.GroundRounds.size() == 2, "ground rollback");
  auto *plane = new unit::Aircraft(&state, 1);
  state.ownUnit(plane);
  state.setUnitData(1, plane, ecs::INVALID_ENTITY_ID);
  auto bad_fm = aircraft(true);
  ReplayPacket pkt{}; pkt.type = ReplayPacketType::AircraftSmall; pkt.timestamp_ms = 10; pkt.stream = bad_fm;
  require(!state.ParsePacket(pkt), "aircraft failure was hidden");
  require(state.AmmoEvents.empty() && state.ControlEvents.empty() && plane->positions.history().empty(), "aircraft rollback");
  auto good_fm = aircraft(false);
  pkt.timestamp_ms = 20; pkt.stream = good_fm;
  require(state.ParsePacket(pkt), "valid aircraft rejected");
  require(state.AmmoEvents.size() == 1 && state.ControlEvents.size() == 1 && plane->positions.history().size() == 1, "aircraft commit");
  std::cout << "detection, ground and aircraft checks passed\n";
  if (const char *replay_path = std::getenv("WRPL_TEST_REPLAY_PATH"); replay_path && *replay_path)
    check_store_owner(replay_path);
}
