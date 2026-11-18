// `--romm-snapshots-test` (#346, romm.h's snapshotsTest): the snapshot
// client (romm_snapshots.cpp) and the heartbeat's channels line
// (romm_pairing.cpp) read against RomM's own answers, with no network.
//
// THE ANSWERS BELOW ARE RomM's, RECORDED from the write-test RomM,
// 5.5.0-alpha.2, on 2026-11-18. A person made for it, paired as a console
// pairs (so its device is a "cabinetos" one) and deleted after, pushed to one
// homebrew game, an SNES game whose channel file is file 8: a save it had
// not sent the bytes of, then sent; the same again; a save with a state and
// its picture; two pushes built on a current that had moved on, the second
// from an older snapshot; a folder save as a zip; a hardcore save, then a
// softcore one over it; a hash its bytes did not match; a file RomM does not
// have; then the game's channels. Only the heartbeat is cut, to the blocks
// read here; 5.4.1's below is RomM's HeartbeatResponse at that tag, written
// rather than recorded. Every byte pushed came from one rule, bytesOf, which
// the test repeats, so RomM's hashes can be held to contentHash's.

#include "romm.h"

#include "dirsave.h"
#include "fileio.h"
#include "romm_internal.h"

#include <json-c/json.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace romm {

namespace {

// RomM 5.4.1's heartbeat has no SAVE_SYNC block (HeartbeatResponse at 5.4.1).
const char* const kHeartbeat541 =
    R"({"SYSTEM":{"VERSION":"5.4.1","GIT_BRANCH":null,"SHOW_SETUP_WIZARD":false},)"
    R"("CONVERTO":{"ENABLED":false},"TASKS":{"ENABLE_SCHEDULED_RESCAN":false}})";

const char* const kHeartbeat =
    R"rec({"SYSTEM":{"VERSION":"5.5.0-alpha.2","GIT_BRANCH":null,"SHOW_SETUP_WIZARD":false},"CONVERTO":{"ENABLED":false},"SAVE_SYNC":{"SNAPSHOT_RETENTION":10,"SNAPSHOT_BRANCH_LIFETIME_DAYS":30}})rec";
const char* const kChannelsEmpty =
    R"rec([])rec";
const char* const kMissing =
    R"rec({"missing":["save"]})rec";
const char* const kMade =
    R"rec({"id":121,"digest":"sha256:7db67dfd70fb1afe52472b272d7b63a4377e16ae6614699b94329fd04e88e35d","kind":"channel","parent_snapshot_id":null,"channel_id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48.199227+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:48+00:00"}],"save":{"id":314,"file_name":"game [2026-11-18_22-42-48-168].srm","file_size_bytes":8192,"content_hash":"520861061656bf5a64eaadc710f5512f","identity_hash":"520861061656bf5a64eaadc710f5512f","shape":"SINGLE","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/314/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-168%5D.srm","screenshot":null},"states":{},"thumbnail":null})rec";
const char* const kUnchanged =
    R"rec({"id":121,"digest":"sha256:7db67dfd70fb1afe52472b272d7b63a4377e16ae6614699b94329fd04e88e35d","kind":"channel","parent_snapshot_id":null,"channel_id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:48+00:00"}],"save":{"id":314,"file_name":"game [2026-11-18_22-42-48-168].srm","file_size_bytes":8192,"content_hash":"520861061656bf5a64eaadc710f5512f","identity_hash":"520861061656bf5a64eaadc710f5512f","shape":"SINGLE","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/314/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-168%5D.srm","screenshot":null},"states":{},"thumbnail":null})rec";
const char* const kMadeWithState =
    R"rec({"id":122,"digest":"sha256:14fec9949eb20985afba01640b87e735873ace850bb39ca0ed94a2b618eec22c","kind":"channel","parent_snapshot_id":121,"channel_id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48.667063+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:48+00:00"}],"save":{"id":315,"file_name":"game [2026-11-18_22-42-48-649].srm","file_size_bytes":8192,"content_hash":"a6e8e336ecc7961005b883dfcb728461","identity_hash":"a6e8e336ecc7961005b883dfcb728461","shape":"SINGLE","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/315/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-649%5D.srm","screenshot":null},"states":{"snes9x-native":{"1":{"id":446,"file_name":"game [2026-11-18_22-42-48-655].state1","file_size_bytes":3000,"content_hash":"088352c0073bf7e95545460e8ca2bec8","emulator_version":null,"core_version":null,"download_path":"/api/states/446/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-655%5D.state1","screenshot":{"id":280,"download_path":"/api/screenshots/280/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-655%5D.png"}}}},"thumbnail":null})rec";
const char* const kMoved =
    R"rec({"current":{"id":122,"digest":"sha256:14fec9949eb20985afba01640b87e735873ace850bb39ca0ed94a2b618eec22c","kind":"channel","parent_snapshot_id":121,"channel_id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48+00:00","held_by":[],"save":{"id":315,"file_name":"game [2026-11-18_22-42-48-649].srm","file_size_bytes":8192,"content_hash":"a6e8e336ecc7961005b883dfcb728461","identity_hash":"a6e8e336ecc7961005b883dfcb728461","shape":"SINGLE","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/315/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-649%5D.srm","screenshot":null},"states":{"snes9x-native":{"1":{"id":446,"file_name":"game [2026-11-18_22-42-48-655].state1","file_size_bytes":3000,"content_hash":"088352c0073bf7e95545460e8ca2bec8","emulator_version":null,"core_version":null,"download_path":"/api/states/446/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-655%5D.state1","screenshot":{"id":280,"download_path":"/api/screenshots/280/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-655%5D.png"}}}},"thumbnail":null},"branch":{"id":123,"digest":"sha256:67c8db3577f7d17e2181dc45cd3996e737a8bd2a27e4ab2ac13c2aedc559d954","kind":"branch","parent_snapshot_id":121,"channel_id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48.804333+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:48+00:00"}],"save":{"id":316,"file_name":"game [2026-11-18_22-42-48-778].srm","file_size_bytes":8192,"content_hash":"ab792fb380593a7b574ec1e089e7ba96","identity_hash":"ab792fb380593a7b574ec1e089e7ba96","shape":"SINGLE","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/316/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-778%5D.srm","screenshot":null},"states":{},"thumbnail":null},"reason":"moved"})rec";
const char* const kMovedFromOlder =
    R"rec({"current":{"id":122,"digest":"sha256:14fec9949eb20985afba01640b87e735873ace850bb39ca0ed94a2b618eec22c","kind":"channel","parent_snapshot_id":121,"channel_id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48+00:00","held_by":[],"save":{"id":315,"file_name":"game [2026-11-18_22-42-48-649].srm","file_size_bytes":8192,"content_hash":"a6e8e336ecc7961005b883dfcb728461","identity_hash":"a6e8e336ecc7961005b883dfcb728461","shape":"SINGLE","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/315/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-649%5D.srm","screenshot":null},"states":{"snes9x-native":{"1":{"id":446,"file_name":"game [2026-11-18_22-42-48-655].state1","file_size_bytes":3000,"content_hash":"088352c0073bf7e95545460e8ca2bec8","emulator_version":null,"core_version":null,"download_path":"/api/states/446/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-655%5D.state1","screenshot":{"id":280,"download_path":"/api/screenshots/280/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-655%5D.png"}}}},"thumbnail":null},"branch":{"id":124,"digest":"sha256:e8214d481958e1a07108d97d86a368ecfa1ebd095241caadafab8444b403bcb0","kind":"branch","parent_snapshot_id":123,"channel_id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48.900798+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:48+00:00"}],"save":{"id":317,"file_name":"game [2026-11-18_22-42-48-893].srm","file_size_bytes":8192,"content_hash":"60867bd535deb312b122b08152016b91","identity_hash":"60867bd535deb312b122b08152016b91","shape":"SINGLE","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/317/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-893%5D.srm","screenshot":null},"states":{},"thumbnail":null},"reason":"moved_from_older"})rec";
const char* const kMadeZip =
    R"rec({"id":125,"digest":"sha256:cff68a3c4579abfc2118331ecc850b6fe7911961122564342fb315e3675efcd2","kind":"channel","parent_snapshot_id":null,"channel_id":"01a1f0d3-b619-75b7-9db9-335b34430280","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48.987263+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:48+00:00"}],"save":{"id":318,"file_name":"game [2026-11-18_22-42-48-979].zip","file_size_bytes":1247,"content_hash":"f6c63429ff085394a3077526b010ceea","identity_hash":"f6c63429ff085394a3077526b010ceea","shape":"FOLDER","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/318/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-979%5D.zip","screenshot":null},"states":{},"thumbnail":null})rec";
const char* const kMadeHardcore =
    R"rec({"id":126,"digest":"sha256:d783928b226557f919fd9bbe647f815f0c7fb49c168e3871f2a34df2b92183f8","kind":"channel","parent_snapshot_id":null,"channel_id":"01a1f0d3-b663-72c1-abe3-c0358cb485a9","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":true,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:49.061186+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:49+00:00"}],"save":{"id":319,"file_name":"game [2026-11-18_22-42-49-054].srm","file_size_bytes":8192,"content_hash":"7ff116322667610083dcc75b7199edcd","identity_hash":"7ff116322667610083dcc75b7199edcd","shape":"SINGLE","format":"native","emulator":"snes9x","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/319/content?timestamp=2026-11-18 22:42:49&name=game%20%5B2026-11-18_22-42-49-054%5D.srm","screenshot":null},"states":{},"thumbnail":null})rec";
const char* const kHardcore =
    R"rec({"hardcore_downgrade":true})rec";
const char* const kWrongHash =
    R"rec({"detail":{"manifest":"save does not hash to the manifest's value"}})rec";
const char* const kNoFile =
    R"rec({"detail":"ROM file not found"})rec";
const char* const kChannels =
    R"rec([{"id":"01a1f0d3-b663-72c1-abe3-c0358cb485a9","label":"hardcore test","is_public":false,"is_hardcore":true,"is_own":true,"owner_username":"step2_recorder","current_snapshot_id":126,"rom_id":9,"rom_file_id":8,"created_at":"2026-11-18T22:42:49+00:00","updated_at":"2026-11-18T22:42:49+00:00","current":{"id":126,"digest":"sha256:d783928b226557f919fd9bbe647f815f0c7fb49c168e3871f2a34df2b92183f8","kind":"channel","parent_snapshot_id":null,"channel_id":"01a1f0d3-b663-72c1-abe3-c0358cb485a9","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":true,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:49+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:49+00:00"}],"save":{"id":319,"file_name":"game [2026-11-18_22-42-49-054].srm","file_size_bytes":8192,"content_hash":"7ff116322667610083dcc75b7199edcd","identity_hash":"7ff116322667610083dcc75b7199edcd","shape":"SINGLE","format":"native","emulator":"snes9x","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/319/content?timestamp=2026-11-18 22:42:49&name=game%20%5B2026-11-18_22-42-49-054%5D.srm","screenshot":null},"states":{},"thumbnail":null},"snapshot_count":1},{"id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","label":"default","is_public":false,"is_hardcore":false,"is_own":true,"owner_username":"step2_recorder","current_snapshot_id":122,"rom_id":9,"rom_file_id":8,"created_at":"2026-11-18T22:42:48+00:00","updated_at":"2026-11-18T22:42:48+00:00","current":{"id":122,"digest":"sha256:14fec9949eb20985afba01640b87e735873ace850bb39ca0ed94a2b618eec22c","kind":"channel","parent_snapshot_id":121,"channel_id":"01a1f0d3-b303-75e3-86f9-d56256d5e32f","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48+00:00","held_by":[],"save":{"id":315,"file_name":"game [2026-11-18_22-42-48-649].srm","file_size_bytes":8192,"content_hash":"a6e8e336ecc7961005b883dfcb728461","identity_hash":"a6e8e336ecc7961005b883dfcb728461","shape":"SINGLE","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/315/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-649%5D.srm","screenshot":null},"states":{"snes9x-native":{"1":{"id":446,"file_name":"game [2026-11-18_22-42-48-655].state1","file_size_bytes":3000,"content_hash":"088352c0073bf7e95545460e8ca2bec8","emulator_version":null,"core_version":null,"download_path":"/api/states/446/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-655%5D.state1","screenshot":{"id":280,"download_path":"/api/screenshots/280/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-655%5D.png"}}}},"thumbnail":null},"snapshot_count":4},{"id":"01a1f0d3-b619-75b7-9db9-335b34430280","label":"zip test","is_public":false,"is_hardcore":false,"is_own":true,"owner_username":"step2_recorder","current_snapshot_id":125,"rom_id":9,"rom_file_id":8,"created_at":"2026-11-18T22:42:48+00:00","updated_at":"2026-11-18T22:42:48+00:00","current":{"id":125,"digest":"sha256:cff68a3c4579abfc2118331ecc850b6fe7911961122564342fb315e3675efcd2","kind":"channel","parent_snapshot_id":null,"channel_id":"01a1f0d3-b619-75b7-9db9-335b34430280","author_user_id":3,"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"emulator":"snes9x-native","rom_id":9,"rom_sha1":"25d9b822a7059ce6695d040b31877b9491c1ae7e","save_target":null,"is_hardcore":false,"is_pinned":false,"pin_count":0,"is_public":false,"created_at":"2026-11-18T22:42:48+00:00","held_by":[{"device":{"id":"9ce2d9d5-15c9-4cfa-bdbd-d58ea774bdb0","name":"CabinetOS","client":"cabinetos","is_own":true},"synced_at":"2026-11-18T22:42:48+00:00"}],"save":{"id":318,"file_name":"game [2026-11-18_22-42-48-979].zip","file_size_bytes":1247,"content_hash":"f6c63429ff085394a3077526b010ceea","identity_hash":"f6c63429ff085394a3077526b010ceea","shape":"FOLDER","format":"native","emulator":"snes9x-native","emulator_version":null,"core":"snes9x","core_version":null,"download_path":"/api/saves/318/content?timestamp=2026-11-18 22:42:48&name=game%20%5B2026-11-18_22-42-48-979%5D.zip","screenshot":null},"states":{},"thumbnail":null},"snapshot_count":1}])rec";
// The zip that answer was to, as Python's zipfile wrote it (1247 bytes).
const char* const kZipHex =
    "504b0304140000000800588d725d0000000002000000000000000900000053415645444154412f0300504b0304140000"
    "000800588d725d81b7e8ec3e010000881300001100000053415645444154412f444154412e42494e639733f549ae9ab8"
    "62ffb5b72cd2469e09e57d4bf75c7ec528a1ef165bd2bd68e785e7ff44759ca30a3be66f3bfbe4b790a643785eeb9ccd"
    "a71efee057b30dc96e9ab9e1f8bdaf3cca568119f5d3d61eb9fd8953c1dc2fb566f2aa8337deb3c99a7827554e58beef"
    "ea1b6629438ff8b2de25bb2fbd6410d7738d29ee5ab8e3fcb3bf22da4e9105edf3b69e79fc4b50c33e2cb765f6a6930f"
    "bef3a9da046735ce587fecee176e25cb80f4baa96b0edffac8216fe69b523d69e581ebef58658cbd122bfa97edbdf29a"
    "49d2c03daeb467f1ae8b2ffe8be9ba4417752ed87eeee91f612dc788fcb6b95b4e3ffa29a06e179ad33c6be389fbdf78"
    "55ac83321ba6af3b7ae73397a2857f5aed94d5876e7e601ff5ffa8ff47fd3feaff51ff8ffa7fd4ffa3fe1ff5ffa8ff47"
    "fd3feaff51ff8ffa7fd4ffa3fe1ff5ffa8ffe9e07f00504b0304140000000800588d725d51859d1b1401000090010000"
    "1200000053415645444154412f504152414d2e53464fe354734ea89db5fdca473e6d8fd4a6f97b6e7e1336f0cd6a5f72"
    "f0de6f09d3a0fc9e95c71e33c85a85974c5c77fa05ab927d4ce5b4cd17de72a9bb24d6cdde71f513bf8e675af382bdb7"
    "be8b18fa65772c3d74ff8fa4597041efaae34f18e5ac234a27ad3ff3924dd921b66afa968befb8355c93eae7ecbcf659"
    "40d72bbd65e1bedb3f448dfc733a971d7ef057ca3ca4b06ff589a74cf236916593379c7dc5aee218573d63eba5f73c9a"
    "6ec90d73775dff22a8e79dd1ba68ff9d9f62c601b95dcb8f3cfc276d115ad4bfe6e4336605dba8f2291bcfbde650758a"
    "af99b9edf2075e2df794c679bb6f7c15d2f7c96c5b7ce0ee2f7193c0bcee15471ffd97b10c2b9eb0f6d4731645bbe88a"
    "a99bcebfe11c64fe0700504b0304140000080800588d725d26bf750dcd000000c8000000100000004d61c3af732f6963"
    "c3b46e652e706e6701c80037ff0a29486786a5c4e30221405f7e9dbcdbfa1938577695b4d3f211304f6e8daccbea0928"
    "476685a4c3e201203f5e7d9cbbdaf91837567594b3d2f1102f4e6d8cabcae90827466584a3c2e1001f3e5d7c9bbad9f8"
    "1736557493b2d1f00f2e4d6c8baac9e80726456483a2c1e0ff1e3d5c7b9ab9d8f71635547392b1d0ef0e2d4c6b8aa9c8"
    "e70625446382a1c0dffe1d3c5b7a99b8d7f61534537291b0cfee0d2c4b6a89a8c7e60524436281a0bfdefd1c3b5a7998"
    "b7d6f51433527190afceed0c2b4a6988a7c6e50423504b01021403140000000800588d725d0000000002000000000000"
    "00090000000000000000001000fd410000000053415645444154412f504b01021403140000000800588d725d81b7e8ec"
    "3e0100008813000011000000000000000000000080012900000053415645444154412f444154412e42494e504b010214"
    "03140000000800588d725d51859d1b140100009001000012000000000000000000000080019601000053415645444154"
    "412f504152414d2e53464f504b01021403140000080800588d725d26bf750dcd000000c8000000100000000000000000"
    "0000008001da0200004d61c3af732f6963c3b46e652e706e67504b05060000000004000400f4000000d50300000000";

// The recorder's bytes: byte i of a run is (i * seed + seed / 3) & 0xFF.
std::vector<uint8_t> bytesOf(int seed, int n) {
    std::vector<uint8_t> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) out[static_cast<size_t>(i)] = static_cast<uint8_t>((i * seed + seed / 3) & 0xFF);
    return out;
}

std::vector<uint8_t> fromHex(const char* hex) {
    std::vector<uint8_t> out;
    for (const char* p = hex; p[0] && p[1]; p += 2) {
        const char two[3] = {p[0], p[1], 0};
        out.push_back(static_cast<uint8_t>(std::strtoul(two, nullptr, 16)));
    }
    return out;
}

const Channel* labelled(const std::vector<Channel>& list, const char* label) {
    for (const Channel& c : list)
        if (c.label == label) return &c;
    return nullptr;
}

// The manifest's field, as JSON text ("null" for a null, "" when it is not
// there), so a test can say what was sent without caring for the order.
std::string field(const std::string& manifest, const char* path) {
    json_object* o = json_tokener_parse(manifest.c_str());
    json_object* at = o;
    std::string out;
    bool found = o != nullptr;
    std::string p = path;
    size_t from = 0;
    while (found && from <= p.size()) {
        const size_t dot = p.find('.', from);
        const std::string key = p.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
        json_object* next = nullptr;
        found = json_object_get_type(at) == json_type_object &&
                json_object_object_get_ex(at, key.c_str(), &next);
        at = next;
        if (dot == std::string::npos) break;
        from = dot + 1;
    }
    if (found) out = at ? json_object_to_json_string_ext(at, JSON_C_TO_STRING_PLAIN) : "null";
    if (o) json_object_put(o);
    return out;
}

}  // namespace

int snapshotsTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    std::printf("What the heartbeat says\n");
    Heartbeat hb;
    check(readHeartbeat(kHeartbeat, &hb) && hb.hasSystem && hb.version == "5.5.0-alpha.2" &&
              hb.channels,
          "RomM 5.5.0-alpha.2's says it has save channels");
    check(readHeartbeat(kHeartbeat541, &hb) && hb.version == "5.4.1" && !hb.channels,
          "RomM 5.4.1's says it has none");
    check(readHeartbeat(R"({"SYSTEM":{"VERSION":"5.5.0"},"SAVE_SYNC":null})", &hb) && !hb.channels,
          "a SAVE_SYNC that is not a block is none");
    check(!readHeartbeat("<html>gateway</html>", &hb), "a page that is not JSON is no heartbeat");

    std::printf("RomM's content hash\n");
    check(contentHash({}) == "d41d8cd98f00b204e9800998ecf8427e" &&
              contentHash({'a', 'b', 'c'}) == "900150983cd24fb0d6963f7d28e17f72",
          "the MD5 of the bytes (RFC 1321's own examples)");
    check(contentHash(bytesOf(7, 8192)) == "520861061656bf5a64eaadc710f5512f" &&
              contentHash(bytesOf(11, 8192)) == "a6e8e336ecc7961005b883dfcb728461",
          "a save's, as RomM hashed it");
    check(contentHash(bytesOf(13, 3000)) == "088352c0073bf7e95545460e8ca2bec8", "a state's");
    check(contentHash(fromHex(kZipHex)) == "f6c63429ff085394a3077526b010ceea",
          "a zip's: each file's under its name, sorted, a folder and a name outside ASCII "
          "among them");
    {
        // Two of those files zipped the console's way (zipTree), hashed against
        // RomM's rule worked by hand for them: the hash is of names and
        // contents, not of how a zip packs them. ASCII names only: zipTree does
        // not mark a name as UTF-8, so RomM would read one outside ASCII as
        // CP437, and the hashes would differ.
        char tmpl[] = "/tmp/cabinetos-snapshots-XXXXXX";
        const char* dir = ::mkdtemp(tmpl);
        bool written = dir != nullptr;
        const std::string root = dir ? dir : "";
        const std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
            {"SAVEDATA/DATA.BIN", bytesOf(23, 5000)},
            {"SAVEDATA/PARAM.SFO", bytesOf(29, 400)},
        };
        if (dir) ::mkdir((root + "/SAVEDATA").c_str(), 0700);
        std::vector<std::string> names;
        for (const auto& [name, bytes] : files) {
            written = written && cab::writeBytes(root + "/" + name, bytes);
            names.push_back(name);
        }
        std::vector<uint8_t> zipped;
        std::string err;
        written = written && cab::zipTree(root, names, &zipped, &err);
        check(written && contentHash(zipped) == "5c13ff31dfd33f87d3d8caa371c458bb",
              "a zip the console makes (zipTree) hashes by RomM's rule");
        for (const auto& [name, bytes] : files) ::unlink((root + "/" + name).c_str());
        ::rmdir((root + "/SAVEDATA").c_str());
        if (dir) ::rmdir(dir);
    }
    {
        const std::vector<uint8_t> notZip = {'P', 'K', 3, 4, 'n', 'o', 't', ' ', 'a', ' ', 'z', 'i', 'p'};
        check(contentHash(notZip) == "b293f4d7bb3247fa4e93cd269ee26b9a",
              "bytes that only begin like a zip are hashed as bytes");
    }

    std::printf("A game's channels\n");
    std::vector<Channel> list;
    std::string err;
    check(readChannels(kChannelsEmpty, &list, &err) && list.empty(), "none is an empty list");
    list.clear();
    check(readChannels(kChannels, &list, &err) && list.size() == 3, "three, read whole");
    const Channel* def = labelled(list, "default");
    check(def && !def->hardcore && def->romId == 9 && def->romFileId == 8 &&
              def->snapshotCount == 4 && def->currentId == 122 && def->current.id == 122,
          "RomM's \"default\": its file, its four snapshots and its current");
    check(def && def->current.hasSave &&
              def->current.save.contentHash == contentHash(bytesOf(11, 8192)) &&
              def->current.save.shape == "SINGLE" && def->current.save.format == "native" &&
              def->current.save.emulator == "snes9x-native" && def->current.save.core == "snes9x",
          "its current's save, by hash, with the names it was pushed with");
    check(def && def->current.states.size() == 1 &&
              def->current.states[0].core == "snes9x-native" &&
              def->current.states[0].slot == "1" &&
              def->current.states[0].file.contentHash == contentHash(bytesOf(13, 3000)) &&
              !def->current.states[0].file.picturePath.empty(),
          "and its state, with its picture");
    check(def && def->current.deviceOwn && def->current.deviceClient == "cabinetos" &&
              def->current.heldBy.empty(),
          "pushed from this person's CabinetOS device, which no longer holds it (it took the "
          "branch)");
    const Channel* hard = labelled(list, "hardcore test");
    check(hard && hard->hardcore && hard->current.hardcore && hard->current.states.empty() &&
              hard->current.heldBy.size() == 1 && hard->current.heldBy[0].client == "cabinetos",
          "a hardcore channel says so");
    const Channel* folder = labelled(list, "zip test");
    check(folder && folder->current.save.shape == "FOLDER", "a zip travels as a FOLDER");
    list.clear();
    check(!readChannels(R"({"detail":"Not found"})", &list, &err) && list.empty(),
          "an answer that is not a list is refused");

    std::printf("What RomM answers a push\n");
    PushAnswer a;
    readPushAnswer(400, kMissing, &a);
    check(a.kind == PushAnswer::Kind::BytesMissing && a.missing == std::vector<std::string>{"save"},
          "400: the bytes it lacks, by part");
    readPushAnswer(201, kMade, &a);
    check(a.kind == PushAnswer::Kind::Made && a.snapshot.id == 121 && a.snapshot.kind == "channel" &&
              a.snapshot.parentId == 0 && a.snapshot.hasSave &&
              a.snapshot.save.contentHash == contentHash(bytesOf(7, 8192)) &&
              !a.snapshot.channelId.empty(),
          "201: made, the channel's new current");
    check(a.snapshot.deviceOwn && a.snapshot.deviceClient == "cabinetos" &&
              a.snapshot.heldBy.size() == 1 && a.snapshot.heldBy[0].client == "cabinetos" &&
              a.snapshot.heldBy[0].deviceId == a.snapshot.deviceId,
          "and this device holds it");
    readPushAnswer(200, kUnchanged, &a);
    check(a.kind == PushAnswer::Kind::Unchanged && a.snapshot.id == 121,
          "200: unchanged, the current it already was");
    readPushAnswer(201, kMadeWithState, &a);
    check(a.kind == PushAnswer::Kind::Made && a.snapshot.id == 122 && a.snapshot.parentId == 121 &&
              a.snapshot.states.size() == 1 && !a.snapshot.states[0].file.picturePath.empty(),
          "201 for a save with a state, made from the one before");
    readPushAnswer(409, kMoved, &a);
    check(a.kind == PushAnswer::Kind::MovedOn && a.snapshot.id == 123 &&
              a.snapshot.kind == "branch" && a.snapshot.parentId == 121 && a.hasCurrent &&
              a.current.id == 122 && !a.fromOlder,
          "409, moved on: kept as a branch, with the current it missed");
    readPushAnswer(409, kMovedFromOlder, &a);
    check(a.kind == PushAnswer::Kind::MovedOn && a.snapshot.id == 124 &&
              a.snapshot.parentId == 123 && a.current.id == 122 && a.fromOlder,
          "409, moved on from an older snapshot than the one expected");
    readPushAnswer(201, kMadeZip, &a);
    check(a.kind == PushAnswer::Kind::Made && a.snapshot.save.shape == "FOLDER" &&
              a.snapshot.save.contentHash == contentHash(fromHex(kZipHex)),
          "201 for a zip, which RomM hashed as the console does");
    readPushAnswer(201, kMadeHardcore, &a);
    check(a.kind == PushAnswer::Kind::Made && a.snapshot.hardcore, "201 for a hardcore save");
    readPushAnswer(409, kHardcore, &a);
    check(a.kind == PushAnswer::Kind::HardcoreChannel,
          "409: a softcore save refused over a hardcore current");
    readPushAnswer(422, kWrongHash, &a);
    check(a.kind == PushAnswer::Kind::Refused && a.status == 422 &&
              a.detail.find("does not hash") != std::string::npos,
          "422: a hash the bytes do not match is refused");
    readPushAnswer(404, kNoFile, &a);
    check(a.kind == PushAnswer::Kind::Refused && a.status == 404, "404: a file RomM does not have");
    readPushAnswer(400, R"({"detail":"A manifest part is required"})", &a);
    check(a.kind == PushAnswer::Kind::Refused && a.missing.empty(),
          "a 400 that names no missing bytes is refused");
    readPushAnswer(201, "<html>proxy</html>", &a);
    check(a.kind == PushAnswer::Kind::Refused && a.snapshot.id == 0,
          "a 201 that is not a snapshot is not taken for one");
    readPushAnswer(409, "{}", &a);
    check(a.kind == PushAnswer::Kind::Refused, "a 409 RomM never sends is refused");

    std::printf("The manifest a push sends\n");
    SnapshotPush first;
    first.romFileId = 8;
    first.label = "default";
    first.hasSave = true;
    first.save.hash = contentHash(bytesOf(7, 8192));
    first.emulator = "snes9x-native";
    first.core = "snes9x";
    const std::string m1 = pushManifest(first);
    check(field(m1, "rom_file_id") == "8" && field(m1, "label") == "\"default\"" &&
              field(m1, "channel_id").empty() && field(m1, "parent_snapshot_id").empty(),
          "a new channel: its file and its name, no id and no parent");
    check(field(m1, "expected_current_id") == "null",
          "an empty channel expected, said as null and never left out");
    check(field(m1, "save.hash") == "\"520861061656bf5a64eaadc710f5512f\"" &&
              field(m1, "save.shape") == "\"SINGLE\"" && field(m1, "save.format") == "\"native\"" &&
              field(m1, "emulator") == "\"snes9x-native\"" && field(m1, "core") == "\"snes9x\"",
          "the save by hash, shape and format, with the emulator's and the core's names");
    check(field(m1, "states").empty() && field(m1, "is_hardcore").empty(),
          "no states named, so the parent's are carried; never hardcore");
    SnapshotPush next;
    next.romFileId = 8;
    next.channelId = "01a1f0d3-b303-75e3-86f9-d56256d5e32f";
    next.expectedCurrentId = 122;
    next.parentId = 121;
    next.states = {{"snes9x-native", "1", {"aa", "game.state1", {}, "", {}}},
                   {"other", "1", {"bb", "game.state1", {}, "", {}}},
                   {"snes9x-native", "2", {"cc", "game.state2", {}, "", {}}}};
    const std::string m2 = pushManifest(next);
    check(field(m2, "channel_id") == "\"01a1f0d3-b303-75e3-86f9-d56256d5e32f\"" &&
              field(m2, "label").empty() && field(m2, "expected_current_id") == "122" &&
              field(m2, "parent_snapshot_id") == "121",
          "into a channel: the current expected, and an older parent named");
    check(field(m2, "save").empty(), "no save: the parent's is carried");
    check(field(m2, "states.snes9x-native.1") == "\"aa\"" &&
              field(m2, "states.snes9x-native.2") == "\"cc\"" &&
              field(m2, "states.other.1") == "\"bb\"",
          "the states a core at a time, every slot it keeps named");

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace romm
