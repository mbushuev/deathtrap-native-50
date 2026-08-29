#include "deathtrap_native_render_patch.h"

#ifndef DEATHTRAP_NATIVE_VERSION
#define DEATHTRAP_NATIVE_VERSION "development"
#endif

#include <windows.h>

#include <Xinput.h>

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "camera_spring_arm.h"
#include "camera_room_collision.h"
#include "deathtrap_music_route.h"
#include "immersive_first_person.h"
#include "mouse_combat_routing.h"

namespace {

constexpr uint32_t kExpectedTimestamp = 0x35752434u;
constexpr uint32_t kExpectedImageSize = 0x00367000u;
constexpr uintptr_t kRateConsumerSignatureRva = 0x0007F015u;
constexpr uintptr_t kRenderPresentWaitRva = 0x00080600u;
constexpr uintptr_t kRendererRva = 0x0001B320u;
constexpr uintptr_t kSceneCacheUpdateRva = 0x00039160u;
constexpr uintptr_t kCameraCacheUpdateRva = 0x00003860u;
// These two leaves are the narrow transform portion of the broad 0x3860
// camera-cache transaction.  0x3A980 builds a node's local transform from
// its position/angles; 0x3AC00 composes its world transform.  The owned-camera
// publication audit invokes them only on a detached camera-node copy.  In
// particular, it never replays the live resource/bounds/callback recursion
// that made the rejected 0.0.142 build shake the player.
constexpr uintptr_t kCameraNodeLocalUpdateRva = 0x0003A980u;
constexpr uintptr_t kCameraNodeWorldUpdateRva = 0x0003AC00u;
constexpr uintptr_t kBackendFlipPointerRva = 0x000FFA50u;
constexpr uintptr_t kUiFrameStampRva = 0x000FB96Cu;
constexpr uintptr_t kUiRenderStateRva = 0x0022B5B0u;
constexpr uintptr_t kUiSelectorModeRva = 0x001086FCu;
constexpr uintptr_t kUiMessageStateRva = 0x001D41C8u;
constexpr uintptr_t kUiMessageEntriesRva = 0x001D4360u;
constexpr uintptr_t kUiMessageLifetimeImmediateRva = 0x0008F6AFu;
constexpr uintptr_t kPstMessageStateRva = 0x001F0A90u;
constexpr uintptr_t kPstMessageLifetimeImmediateRva = 0x000781C8u;
constexpr uintptr_t kUiCountdownStateRva = 0x001D89F0u;
constexpr uintptr_t kUiOwnerPointerRva = 0x0034F9D0u;
constexpr uintptr_t kEngineFrameCounterRva = 0x001D24DCu;
// The retail music selector stores the currently requested Redbook track here
// before passing that track's start/end pair to AIL_redbook_play.  Steam's
// legacy MP3 wrapper discards the pair, so this engine-owned value is the only
// reliable track identity left at playback time.
constexpr uintptr_t kCurrentRedbookTrackRva = 0x00104C10u;
constexpr uintptr_t kPublishedCameraMatrixRva = 0x001D4110u;
constexpr uintptr_t kRetailCameraManagerPointerRva = 0x001F11C0u;
// Global room manager used by the retail point-sector resolver. manager+0x20
// points at the contiguous 0x3C-byte sector collection.
constexpr uintptr_t kRetailRoomManagerPointerRva = 0x001F11BCu;
// Renderer resource registry used by Dungeon.dll+0x39900 and 0x3AC00.
// A scene node stores a positive resource index at node+0x3C. The registry
// entry points at the original Asylum mesh, including its polygon list and
// local-space vertices. This is intentionally read-only: several visible
// props are omitted from the room BSP used by the retail camera resolver.
constexpr uintptr_t kRenderResourceCountRva = 0x00236F90u;
constexpr uintptr_t kRenderResourceTableRva = 0x00237130u;
// Dungeon.dll+0x30E30 dispatches the retail camera state machine through this
// global controller. Unlike the small context camera owner above, this object
// contains the active mode, target and cached camera values used by the
// first-person and third-person paths.
constexpr uintptr_t kCameraControllerRva = 0x001044A0u;
constexpr uintptr_t kCameraControllerFlagsRva = 0x00104620u;
constexpr uintptr_t kCameraControllerCallback0Rva = 0x00104640u;
constexpr uintptr_t kCameraControllerCallback1Rva = 0x00104644u;
constexpr uintptr_t kCameraControllerModeRva = 0x00104679u;
// The mode-3 dispatcher normally chooses between the rail/fixed-camera path
// and the free desired-position path.  Orbit must take ownership before that
// decision; otherwise the rail pre-check can keep feeding the old endpoint to
// the resolver forever.  The orbit hook calls 0x2F380 exactly once so the
// retail collision, room clipping and smoothing pipeline remains downstream.
constexpr uintptr_t kMode3CameraRva = 0x0002F310u;
constexpr uintptr_t kConfigureCameraRva = 0x0002F380u;
// 0x2EDC0 calls this generic ring-adder at 0x2EFFF for the final native
// camera position. The returned average is copied directly to the camera node
// at 0x2F015, before orientation and presentation-cache publication.
constexpr uintptr_t kCameraHistoryAddRva = 0x0002DE30u;
constexpr uintptr_t kCameraLookAtRva = 0x00030730u;
// Retail 0x30790 calls this leaf to derive the camera node's pitch/yaw/roll
// from its final translation and look target. Exact scene-mesh contractions
// must use the same writer; otherwise translation belongs to the collision
// solution while orientation still belongs to 0x2F380's earlier publication.
// The retail mode-3 branch resolves the room containing controller+0x264 and
// controller+0x1F4 with 0x06130, then asks 0x30910 whether the volume between
// those points is obstructed. 0x30910 performs the centre trace plus six
// offset traces through the game's room/portal geometry. This is the native
// spring-arm authority; 0x2E800 is only a later vector/offset shaping stage.
constexpr uintptr_t kResolveCameraSectorRva = 0x00006130u;
// Returns non-zero when the focus-to-endpoint camera volume is traversable.
// The original name used while reverse engineering was "blocked", but the
// retail mode-3 dispatcher branches to its fallback camera only when this
// routine returns zero.
constexpr uintptr_t kCameraVolumeVisibleRva = 0x00030910u;
// 0x30910 returns clear as soon as any one of its centre-plus-six room traces
// reaches the focus sector. That is the retail fixed-camera visibility rule,
// not sufficient proof that a modern camera volume is wholly outside a wall.
// The underlying room trace is used read-only by the strict exact-endpoint
// validator, which requires all focus samples and all points of the camera's
// own cardinal endpoint footprint to succeed.
constexpr uintptr_t kCameraRoomTraceVisibleRva = 0x0004E760u;
constexpr size_t kCameraControllerPlayerXPointerOffset = 0xFCu;
constexpr size_t kCameraControllerPlayerYPointerOffset = 0x100u;
constexpr size_t kCameraControllerPlayerZPointerOffset = 0x104u;
constexpr size_t kCameraControllerRoomPointerOffset = 0x108u;
constexpr size_t kCameraControllerOwnerOffset = 0x19Cu;
constexpr size_t kCameraControllerScriptOwnerOffset = 0x1B8u;
constexpr size_t kCameraControllerDesiredPositionOffset = 0x1F4u;
constexpr size_t kCameraControllerEndpointSectorOffset = 0x200u;
// Retail 0x2F028 resolves the final camera translation with 0x06130 and
// publishes the result on the camera scene node before rendering.  A modern
// exact or synthetic translation must keep this field coherent with the
// matrix; otherwise room traversal/culling still belongs to a different
// camera position.
constexpr size_t kCameraNodeSectorOffset = 0x100u;
constexpr size_t kCameraControllerPreviousFocusOffset = 0x258u;
constexpr size_t kCameraControllerFocusOffset = 0x264u;
constexpr size_t kCameraControllerFocusDeltaOffset = 0x270u;
constexpr size_t kCameraControllerResolvedPositionOffset = 0x1DCu;
// Dungeon.dll+0x2DC80 initializes a four-sample position history at +0x204.
// +0x20C is its cached average and +0x218 is the first of four Vec3 samples.
// The retail resolver normally advances this history gradually.  A newly
// detected close obstruction is different: keeping the old samples for one
// more render publishes the camera inside the prop that caused the clip.
constexpr size_t kCameraControllerPositionHistoryOffset = 0x204u;
constexpr size_t kCameraControllerPositionHistoryAverageOffset = 0x20Cu;
constexpr size_t kCameraControllerPositionHistorySamplesOffset = 0x218u;
constexpr size_t kCameraControllerPositionHistorySampleStride = 0x0Cu;
constexpr size_t kCameraControllerPositionHistorySampleCount = 4u;
constexpr size_t kCameraControllerActiveModeOffset = 0x27Cu;
constexpr uint8_t kCameraScriptOwnerActiveMask = 0x80u;
// Dungeon.dll+0x2E950 sets controller+0x180 bit 0x20 only while an active
// camera owner has converged to within 110 world units of its requested
// target; the no-owner branch clears it. This is a convergence signal, not a
// camera-mode flag, so it is useful only inside explicit interaction
// arbitration.
constexpr uint32_t kCameraOwnedTargetConvergedMask = 0x20u;
constexpr uintptr_t kActiveCloseCombatWeaponRva = 0x001D8A68u;
constexpr uintptr_t kActiveSpellRva = 0x001D8A6Cu;
constexpr uintptr_t kInventoryLookupRva = 0x0007BD30u;
constexpr uintptr_t kSelectCloseCombatWeaponRva = 0x00090610u;
constexpr uintptr_t kSelectRangedWeaponRva = 0x00090740u;
constexpr uintptr_t kSelectSpellRva = 0x0007BAF0u;
constexpr uintptr_t kUseConsumableRva = 0x0007B9C0u;
constexpr uintptr_t kUseChalkRva = 0x000458B0u;
constexpr uintptr_t kInventorySlotDrawRva = 0x000772A0u;
constexpr uintptr_t kGameRootPointerRva = 0x00235EA4u;
// The live gameplay entity owns its movement controller at +0x114. Native
// locomotion callbacks receive that controller, not the outer entity. Both
// the lean-producing 0x44EA0 wrapper and the simpler 0x44E90 locomotion
// wrapper converge on 0x44DD0. Dedicated turn-in-place states instead update
// heading directly in 0x68970/0x68C20 and intentionally bypass this gateway.
// 0x44DD0 reads the selected source through controller+0x154, adds that Q10
// delta to [controller+0x10]->+0x1C, and mirrors the result into the
// engine-owned render/collision orientation.
constexpr uintptr_t kPlayerTurnRva = 0x00044DD0u;
// Every ordinary ground-movement state installs 0x82750 as controller+0x2EC.
// It refreshes actions and only then invokes the active +0x2F0 state callback,
// so it is the one recurring boundary shared by forward locomotion and both
// direct turn-in-place implementations.
constexpr uintptr_t kPlayerStateDispatcherRva = 0x00082750u;
// 0x32430 is the common local-to-world root transform. Runtime evidence from
// v0.0.207 proves that ordinary locomotion states reach it through many more
// paths than the three initially identified callsites. The player-only hook
// is therefore active only for the complete 0x810A0 movement stage and only
// when its node matches the live player's render root.
constexpr uintptr_t kPlayerRootMotionTransformRva = 0x00032430u;
constexpr uintptr_t kPlayerMovementStageRva = 0x000810A0u;
// The retail DirectInput joystick boundary. 0x51500 returns the configured
// 0..0x4000 X/Y axes and 16 packed buttons; 0x32CD0 reports whether joystick
// input is enabled. Feeding XInput here keeps movement inside the game's
// JOY_* action path instead of synthesizing keyboard W/A/S/D states.
constexpr uintptr_t kNativeJoystickPollRva = 0x00051500u;
constexpr uintptr_t kNativeJoystickEnabledRva = 0x00032CD0u;
// This is the recurring forward-locomotion callback observed on every source
// tick with root motion. It selects controller+0x130 or +0x138 as the turn
// source before calling the canonical 0x44EA0 -> 0x44DD0 writer.
constexpr uintptr_t kPlayerLocomotionRva = 0x0007E530u;
constexpr size_t kPlayerMovementControllerOffset = 0x114u;
constexpr size_t kPlayerRenderLinkOffset = 0x10u;
constexpr size_t kPlayerTurnSourcePointerOffset = 0x154u;
constexpr size_t kPlayerWalkTurnSourceOffset = 0x130u;
constexpr size_t kPlayerRunTurnSourceOffset = 0x138u;
constexpr size_t kPlayerTurnLimitOffset = 0x148u;
constexpr size_t kPlayerIdleTurnOffset = 0x14Cu;
constexpr size_t kPlayerHeadingOffset = 0x1Cu;
constexpr size_t kPlayerRootBasisXxOffset = 0xD0u;
constexpr size_t kPlayerRootBasisZxOffset = 0xD8u;
constexpr size_t kPlayerRootBasisXzOffset = 0xE8u;
constexpr size_t kPlayerRootBasisZzOffset = 0xF0u;
constexpr int32_t kPlayerHeadingUnitsPerTurn = 1024;
constexpr uintptr_t kDamageHandlerRva = 0x0001C130u;
constexpr uintptr_t kMeleeAttackWindowRva = 0x0001D620u;
// These are post-validation gameplay events, not input actions. 0x834F0 is
// reached only after collision code has verified that the struck actor is in
// one of the retail block states, and switches that actor to block-impact
// animation 0x61. 0x1D210 creates the selected offensive spell projectile and
// returns null when launch preconditions are not satisfied.
constexpr uintptr_t kSuccessfulBlockImpactRva = 0x000834F0u;
constexpr uintptr_t kOffensiveSpellLaunchRva = 0x0001D210u;
constexpr uintptr_t kRangedWeaponLaunchRva = 0x0001CEC0u;
constexpr int32_t kFirstOffensiveSpellId = 15;
constexpr int32_t kLastOffensiveSpellId = 21;
constexpr uintptr_t kEntityDataOffset = 0x2Cu;
constexpr uintptr_t kEntityHealthOffset = 0x1030u;
constexpr int32_t kHealthFixedScale = 16384;
constexpr size_t kMovementStageProbeCount = 95u;
constexpr size_t kMovementCallbackProbeCount = 64u;
constexpr std::array<uintptr_t, 5> kMovementDynamicCallbackRvas = {
    0x000810CFu, 0x00090427u, 0x00082770u, 0x00082780u,
    0x000827D0u};
constexpr std::array<uintptr_t, 3> kMovementResolverCallbackRvas = {
    0x00082770u, 0x00082780u, 0x000827D0u};
constexpr uintptr_t kMovementVtableCallbackRva = 0x0005778Au;
constexpr size_t kUiRenderStateSize = 0x40u;
constexpr size_t kUiMessageStateSize = 0x20u;
constexpr size_t kUiMessageEntrySize = 0x50u;
constexpr size_t kUiMessageEntryCount = 3u;
constexpr size_t kUiMessageEntriesSize =
    kUiMessageEntrySize * kUiMessageEntryCount;
constexpr size_t kPstMessageStateSize = 0x620u;
constexpr size_t kUiCountdownStateSize = 0x20u;
constexpr uint32_t kOriginalUiMessageLifetimeTicks = 50u;
constexpr uint32_t kOriginalPstMessageLifetimeTicks = 27u;
constexpr uint32_t kOriginalGameplayRate = 16u;
constexpr uint32_t kOriginalPeriodMilliseconds = 60u;
constexpr double kMatrixFixedScale = 16384.0;
constexpr double kOrbitPi = 3.14159265358979323846;
constexpr size_t kMatrixOffset = 0x9Cu;
constexpr size_t kNodeFlagsOffset = 0x24u;
constexpr size_t kParentOffset = 0x2Cu;
constexpr size_t kChildOffset = 0x30u;
constexpr size_t kSiblingOffset = 0x34u;
constexpr size_t kRenderResourceHandleOffset = 0x3Cu;
constexpr size_t kWorldBoundsOffset = 0x80u;
constexpr size_t kLocalMatrixOffset = 0xD0u;
constexpr size_t kSceneRootOffset = 0x1Cu;
constexpr size_t kContextCameraOwnerOffset = 0x28u;
constexpr size_t kContextSceneOwnerOffset = 0x2Cu;
constexpr size_t kCameraNodeOffset = 0x10u;
constexpr size_t kCameraOwnerCallbackOffset = 0x18u;
constexpr size_t kCameraOwnerProbeDwords = 64u;
constexpr size_t kCameraManagerProbeDwords = 32u;
constexpr size_t kCameraNodeProbeDwords = 80u;
constexpr size_t kCameraControllerProbeDwords = 168u;
constexpr size_t kMaximumSceneNodes = 8192u;
constexpr size_t kContextPrimaryCallbackOffset = 0x50u;
constexpr size_t kContextSecondaryCallbackOffset = 0x54u;
constexpr double kMaximumNodeTranslation = 8.0;
constexpr double kMaximumNodeRotationDegrees = 100.0;
constexpr double kMaximumScaleRatio = 1.25;
constexpr double kMaximumBasisDot = 0.025;
constexpr double kCameraCollisionSphereRadius = 96.0;
constexpr double kCameraCollisionMaximumObjectRadius = 6000.0;
constexpr double kCameraCollisionRadiusMotionTolerance = 8.0;
// Dungeon.dll+0x3B93F tests this bit before dispatching the node's render
// resource. When it is set, the renderer skips the node's own draw call while
// still traversing its children. A resource hidden this way must not remain an
// invisible camera obstacle merely because its transform and bounds are still
// present in the scene tree.
constexpr uint32_t kNodeSkipOwnRenderFlag = 0x02000000u;

constexpr std::array<uint8_t, 16> kRateConsumerSignature = {
    0x8B, 0x83, 0x08, 0x08, 0x00, 0x00, 0x85, 0xC0,
    0x7E, 0x09, 0x50, 0xE8, 0x6B, 0x13, 0xFD, 0xFF};

struct Matrix3x4 {
  std::array<int32_t, 12> values{};
};

struct NodeTransform {
  Matrix3x4 world;
  Matrix3x4 local;
  // Complete source block consumed by 0x3A980 before it writes local matrix
  // state: position/pivot values at +0x00..+0x14 and angles at +0x18..+0x20.
  std::array<uint32_t, 9> local_transform_source{};
  std::array<int32_t, 3> position{};
  std::array<int32_t, 3> angles{};
  uint32_t flags = 0;
  uintptr_t parent = 0;
  uintptr_t render_resource_handle = 0;
  std::array<int32_t, 3> bounds_center{};
  int32_t bounds_radius = 0;
  bool bounds_valid = false;
  bool pose_fields_valid = false;
};

struct SceneSnapshot {
  uintptr_t root = 0;
  uintptr_t camera = 0;
  bool camera_in_scene_tree = false;
  uintptr_t camera_sector = 0;
  bool camera_sector_valid = false;
  std::array<int32_t, 3> camera_focus{};
  bool camera_focus_valid = false;
  uintptr_t player = 0;
  uintptr_t player_object = 0;
  std::array<int32_t, 3> player_position{};
  std::array<int32_t, 3> player_cached_position{};
  std::array<int32_t, 3> player_bounds_a{};
  std::array<int32_t, 3> player_bounds_b{};
  // Collision projection written by Dungeon.dll+0x54D00, or by a qualified
  // large correction from Dungeon.dll+0x68390, while producing this exact
  // simulation endpoint. It is captured at the original callsite and
  // consumed by the next render snapshot only; it is never written back to
  // gameplay state.
  std::array<int32_t, 3> player_contact_projection{};
  // Render-only correction for an exact endpoint that lies behind the last
  // confirmed persistent-contact plane. Unlike the resolver projection
  // above, this is inferred prospectively from an already confirmed A-B-A
  // contact manifold. It is never written to simulation-owned state.
  std::array<int32_t, 3> player_contact_manifold_projection{};
  uint64_t player_contact_projection_sequence = 0;
  uint8_t player_contact_count = 0;
  bool player_position_valid = false;
  bool player_cached_position_valid = false;
  bool player_bounds_a_valid = false;
  bool player_bounds_b_valid = false;
  bool player_contact_count_valid = false;
  bool player_contact_projection_valid = false;
  bool player_contact_manifold_projection_valid = false;
  std::unordered_map<uintptr_t, NodeTransform> nodes;
};

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct CameraMeshTriangle {
  std::array<int32_t, 3> a{};
  std::array<int32_t, 3> b{};
  std::array<int32_t, 3> c{};
};

struct CameraCollisionMesh {
  uintptr_t resource = 0;
  uintptr_t polygon_table = 0;
  uint32_t polygon_count = 0;
  bool parsed = false;
  bool local_bounds_valid = false;
  std::array<int32_t, 3> local_min{};
  std::array<int32_t, 3> local_max{};
  std::vector<CameraMeshTriangle> triangles;
};

// Debug-only evidence for the exact render triangle selected by the
// supplemental camera sweep.  Keeping this separate from the collision result
// lets diagnostic builds prove which prop was hit without changing the
// spring-arm decision.
struct CameraMeshHitDiagnostic {
  uintptr_t node = 0;
  uintptr_t resource = 0;
  size_t triangle_index = std::numeric_limits<size_t>::max();
  Vec3 a{};
  Vec3 b{};
  Vec3 c{};
  Matrix3x4 world{};
  std::array<int32_t, 3> bounds_center{};
  int32_t bounds_radius = 0;
  double bounds_motion = 0.0;
  bool initial_overlap = false;
  bool overlap_pushout = false;
  bool near_pivot_escape = false;
  size_t pushout_axis = std::numeric_limits<size_t>::max();
  bool valid = false;
};

struct OwnedCameraSceneSweepResult {
  std::array<int32_t, 3> position{};
  CameraMeshHitDiagnostic diagnostic{};
  double requested_distance = 0.0;
  double contact_distance = 0.0;
  double safe_distance = 0.0;
  bool valid = false;
  bool blocked = false;
  bool initial_overlap = false;
};

struct OwnedCameraCombinedCandidate {
  OwnedCameraSceneSweepResult scene{};
  double safe_distance = 0.0;
  bool evaluated = false;
};

struct OwnedCameraCombinedPlan {
  std::vector<OwnedCameraCombinedCandidate> candidates;
  size_t selected_index = std::numeric_limits<size_t>::max();
  bool valid = false;
  bool retained_previous = false;
};

struct CameraMeshContactPreference {
  uintptr_t node = 0;
  uintptr_t resource = 0;
  size_t axis = std::numeric_limits<size_t>::max();
  bool valid = false;
};

struct Quaternion {
  double w = 1.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct RigidTransform {
  std::array<Vec3, 3> rows{};
  Vec3 translation;
  Vec3 scale{1.0, 1.0, 1.0};
  Quaternion rotation;
};

struct InterpolationStats {
  uint64_t applied = 0;
  uint64_t hierarchical = 0;
  uint64_t world_fallback = 0;
  uint64_t exact = 0;
  uint64_t invalid = 0;
  uint64_t parent_mismatch = 0;
  uint64_t contact_reversal = 0;
  uint64_t temporal_pose_guard = 0;
  uint64_t player_root_found = 0;
  uint64_t player_contact_settle = 0;
  uint64_t player_raw_contact = 0;
  uint64_t player_exact_subtree_nodes = 0;
  uint64_t player_bounds_guided_nodes = 0;
  uint64_t player_contact_projection_nodes = 0;
  uint64_t player_contact_projection_events = 0;
  uint64_t player_contact_bridge_nodes = 0;
  uint64_t player_contact_bridge_events = 0;
  uint64_t player_contact_manifold_midpoint_nodes = 0;
  uint64_t player_contact_manifold_events = 0;
  uint64_t player_contact_normal_flip_rejections = 0;
  uint64_t player_root_coherence_nodes = 0;
  uint64_t camera_temporal_chord_guard = 0;
  std::array<int32_t, 3> player_root_coherence_offset{};
  double player_contact_manifold_depth = 0.0;
  uint32_t player_bounds_axis_mask = 0;
  uint64_t player_directional_release = 0;
  double max_translation = 0.0;
  double max_rotation_degrees = 0.0;
};

struct ActivePresentationTrace {
  uint64_t tick = 0;
  uintptr_t player = 0;
  const SceneSnapshot* exact_scene = nullptr;
  std::array<int32_t, 3> exact_projection{};
  std::array<int32_t, 3> midpoint_seen{};
  std::array<int32_t, 3> exact_before_projection{};
  std::array<int32_t, 3> exact_seen{};
  uint64_t exact_projection_nodes = 0;
  uint32_t midpoint_calls = 0;
  uint32_t exact_calls = 0;
  DeathtrapNativePresentationStage stage =
      DeathtrapNativePresentationStage::kNone;
  bool exact_projection_valid = false;
  bool midpoint_seen_valid = false;
  bool exact_before_projection_valid = false;
  bool exact_seen_valid = false;
};

struct PresentationTraceSample {
  uint64_t tick = 0;
  std::array<int32_t, 3> previous{};
  std::array<int32_t, 3> current{};
  std::array<int32_t, 3> midpoint_seen{};
  std::array<int32_t, 3> exact_before_projection{};
  std::array<int32_t, 3> exact_seen{};
  std::array<int32_t, 3> projection{};
  std::array<int32_t, 3> root_coherence{};
  uint64_t exact_projection_nodes = 0;
  uint64_t root_coherence_nodes = 0;
  uint32_t midpoint_calls = 0;
  uint32_t exact_calls = 0;
  uint32_t flags = 0;
};

struct UiRenderStateSnapshot {
  std::array<uint8_t, kUiRenderStateSize> control{};
  std::array<uint8_t, kUiMessageStateSize> messages{};
  // Dungeon.dll+0x8F4C0 decrements the lifetime at +0x4C of every active
  // 0x50-byte message entry whenever the renderer is invoked. Preserve the
  // complete three-entry queue around synthetic render passes so only the
  // exact simulation render is allowed to age on-screen text.
  std::array<uint8_t, kUiMessageEntriesSize> message_entries{};
  // Level-script (PST) notifications use a separate six-entry ring at
  // 0x101F0A90. Its per-entry lifetime at +0x100 was previously aged by all
  // three renderer calls, making messages such as "You need the silver key"
  // disappear roughly three times faster than retail.
  std::array<uint8_t, kPstMessageStateSize> pst_messages{};
  std::array<uint8_t, kUiCountdownStateSize> countdowns{};
  int32_t frame_stamp = 0;
  uint8_t selector_mode = 0;
  bool control_valid = false;
  bool messages_valid = false;
  bool message_entries_valid = false;
  bool pst_messages_valid = false;
  bool countdowns_valid = false;
  bool frame_stamp_valid = false;
  bool selector_mode_valid = false;
};

struct PlayerMutableStateRollbackStats {
  uint32_t changed_mask = 0;
  uint32_t restored_mask = 0;
  uint64_t changed_fields = 0;
  int64_t maximum_delta = 0;
};

struct TransitionStats {
  size_t matched = 0;
  size_t entered = 0;
  size_t departed = 0;
  double overlap = 0.0;
  double camera_translation = 0.0;
  double camera_rotation_degrees = 0.0;
  uintptr_t primary_callback = 0;
  uintptr_t secondary_callback = 0;
  bool camera_valid = false;
  bool camera_in_scene_tree = false;
  bool suppress_midpoint = false;
  const char* reason = "none";
};

struct CameraProbeSnapshot {
  uintptr_t context_owner = 0;
  uintptr_t manager = 0;
  uintptr_t manager_node = 0;
  uintptr_t node = 0;
  uintptr_t callback = 0;
  uintptr_t controller_callback0 = 0;
  uintptr_t controller_callback1 = 0;
  uint32_t controller_flags = 0;
  uint8_t controller_mode = 0;
  std::array<uint32_t, kCameraOwnerProbeDwords> owner_fields{};
  std::array<uint32_t, kCameraManagerProbeDwords> manager_fields{};
  std::array<uint32_t, kCameraNodeProbeDwords> node_fields{};
  std::array<uint32_t, kCameraControllerProbeDwords> controller_fields{};
  Matrix3x4 published_matrix{};
  bool owner_valid = false;
  bool manager_valid = false;
  bool node_valid = false;
  bool controller_valid = false;
  bool published_valid = false;
  bool initialized = false;
};

std::once_flag g_patch_once;
std::atomic<DeathtrapNativeRenderPatchState> g_state{
    DeathtrapNativeRenderPatchState::kNotAttempted};
std::atomic<uint32_t> g_subframes{0};
std::atomic<uint64_t> g_source_ticks{0};
std::atomic<uint64_t> g_interpolated_frames{0};
std::atomic<uint64_t> g_interpolated_nodes{0};
std::atomic<bool> g_render_hook_installed{false};
std::atomic<bool> g_damage_hook_installed{false};
std::atomic<bool> g_melee_attack_window_hook_installed{false};
std::atomic<bool> g_combat_impact_hook_installed{false};
std::atomic<bool> g_spell_cast_hook_installed{false};
std::atomic<bool> g_ranged_weapon_hook_installed{false};
std::atomic<bool> g_consumable_hook_installed{false};
std::atomic<bool> g_camera_orbit_hook_installed{false};
std::atomic<bool> g_music_track_fix_installed{false};
std::atomic<bool> g_movement_stage_probes_installed{false};
std::atomic<bool> g_movement_callback_probe_installed{false};
std::atomic<int32_t> g_pending_weapon_wheel_detents{0};
std::atomic<uint64_t> g_last_weapon_wheel_event_ms{0};
std::atomic<uint32_t> g_controller_selector_overlay{0};
std::mutex g_contact_projection_mutex;
std::array<int64_t, 3> g_pending_contact_projection{};
uint64_t g_pending_contact_projection_sequence = 0;
uint64_t g_contact_projection_captures = 0;
uint64_t g_contact_projection_consumes = 0;
uint64_t g_contact_projection_54d00_captures = 0;
uint64_t g_contact_projection_68390_captures = 0;
uint64_t g_contact_projection_68390_small_rejections = 0;
uint8_t* g_dungeon_base = nullptr;
SceneSnapshot g_previous_snapshot;
SceneSnapshot g_older_snapshot;
LARGE_INTEGER g_qpc_frequency{};
bool g_debug_log = false;
bool g_music_track_fix_enabled = true;
std::mutex g_native_log_mutex;
HANDLE g_native_log_file = INVALID_HANDLE_VALUE;
bool g_camera_probe_enabled = false;
bool g_head_joint_probe_enabled = false;
CameraProbeSnapshot g_camera_probe_previous;
std::string g_camera_probe_log_buffer;
bool g_third_person_orbit_enabled = false;
bool g_immersive_first_person_enabled = true;
std::atomic<int32_t> g_immersive_keyboard_movement_x{0};
std::atomic<int32_t> g_immersive_keyboard_movement_y{0};
std::atomic<int32_t> g_immersive_xinput_movement_x_milli{0};
std::atomic<int32_t> g_immersive_xinput_movement_y_milli{0};
bool g_third_person_orbit_invert_x = false;
bool g_third_person_orbit_invert_y = false;
double g_third_person_orbit_horizontal_radians = 0.0;
double g_third_person_orbit_vertical_radians = 0.0;
double g_third_person_stick_speed_scale = 1.4;
double g_third_person_orbit_min_pitch_radians = 0.0;
double g_third_person_orbit_max_pitch_radians = 0.0;
double g_custom_head_min_pitch_radians = 0.0;
double g_custom_head_max_pitch_radians = 0.0;
int32_t g_custom_head_height = 10;
int32_t g_custom_head_forward_offset = 55;
double g_third_person_orbit_min_radius = 650.0;
double g_third_person_orbit_max_radius = 1800.0;
double g_third_person_orbit_preferred_radius = 1400.0;
double g_third_person_orbit_response_seconds = 0.05;
// The configured orbit radius must remain large enough to frame Lara. Runtime
// collision may retract the spring arm below this value, but only after the
// complete pivot-to-camera volume query has verified an obstruction.
constexpr double kThirdPersonMinimumCameraDistance = 120.0;
std::atomic<int32_t> g_third_person_orbit_input_x{0};
std::atomic<int32_t> g_third_person_orbit_input_y{0};
std::atomic<bool> g_third_person_orbit_input_active{false};
std::atomic<uint64_t> g_third_person_orbit_input_sequence{0};
std::atomic<int32_t> g_third_person_mouse_delta_x{0};
std::atomic<int32_t> g_third_person_mouse_delta_y{0};
std::atomic<uint64_t> g_last_controller_interaction_ms{0};
std::atomic<uint64_t> g_controller_interaction_sequence{0};
std::atomic<uint64_t> g_last_mode3_source_tick_ms{0};
double g_third_person_mouse_horizontal_radians = 0.0;
double g_third_person_mouse_vertical_radians = 0.0;

struct ThirdPersonOrbitState {
  void* controller = nullptr;
  bool engaged = false;
  double yaw = 0.0;
  double pitch = 0.0;
  double radius = 0.0;
  // Actual spring-arm length submitted to the retail camera. The complete
  // desired arm is collision-tested every source tick; collision retracts
  // immediately, while recovery is delayed and rate-limited.
  double collision_radius = 0.0;
  double filtered_input_x = 0.0;
  double filtered_input_y = 0.0;
  // Native mode-3 focus (controller+0x264). The retail controller itself
  // maintains the previous focus at +0x258 and its source-tick delta at
  // +0x270; using the same anchor prevents actor/prop geometry around the
  // feet from being mistaken for a camera obstruction.
  std::array<int32_t, 3> previous_player{};
  // Arkham-style chase target. The character target is filtered before the
  // desired orbit is built; collision endpoints themselves are never
  // presentation-smoothed or carried as an independent world-space anchor.
  std::array<double, 3> chase_focus{};
  std::array<double, 3> chase_velocity{};
  bool chase_initialized = false;
  std::array<int32_t, 3> requested_position{};
  bool requested_position_valid = false;
  // Ignore a single missing render snapshot before releasing the arm. This
  // never owns a stale contact because the full segment is re-tested.
  uint32_t collision_clear_ticks = 0;
  // A still-blocked native boundary must move outward for several consecutive
  // source ticks before the arm follows it. This rejects alternating
  // portal/small-prop samples without delaying hard inward contraction.
  uint32_t collision_blocked_release_ticks = 0;
  // Outward recovery evidence belongs to one obstruction identity and one
  // monotonically improving safe-distance sequence. It must never be carried
  // across a native/scene-mesh contact change.
  double collision_blocked_candidate_distance = 0.0;
  uint64_t collision_blocker_key = 0;
  uint64_t last_orbit_activity_ms = 0;
  bool motion_active_this_tick = false;
  bool orbit_input_active_this_tick = false;
  uint64_t last_input_sequence = 0;
  uint64_t last_input_time_ms = 0;
  uint64_t applications = 0;
  bool suspended = false;
  // The current native endpoint is contracted or otherwise collision-owned.
  // Presentation follow must not lag behind it and later snap back from an
  // invalid carried point.
  bool collision_constrained_this_tick = false;
  // Supporting-face identity for a contained/near-pivot scene-mesh contact.
  // The endpoint is never retained: it is rebuilt from the current focus,
  // requested ray and current object transform every source tick.
  CameraMeshContactPreference mesh_contact_preference{};
  // Diagnostic state for the full-owned room solver. This is an angular
  // candidate identity, never a retained world-space camera endpoint.
  size_t owned_room_shadow_candidate_index =
      std::numeric_limits<size_t>::max();
  // Once collision selects an alternate angular shot, keep that side until it
  // becomes physically unusable or the direct shot stays fully clear. This
  // prevents a corner from re-authoring the camera direction every tick.
  uint32_t owned_room_direct_clear_ticks = 0;
  // A collapsed direct arm cannot provide both a nonzero look-at vector and a
  // third-person position behind the player. The owned camera temporarily
  // moves in front of the pivot without changing user control rotation.
  bool owned_near_pivot_view_active = false;
  uint32_t owned_near_pivot_direct_clear_ticks = 0;
  double owned_room_shadow_applied_yaw = 0.0;
  double owned_room_shadow_applied_pitch = 0.0;
  bool owned_room_shadow_offset_initialized = false;
  // Full-owned radial response is deliberately independent from the legacy
  // hybrid spring. Mixing those histories would make the first owned sample
  // inherit a radius selected by a different collision authority.
  double owned_collision_radius = 0.0;
  uint32_t owned_collision_clear_ticks = 0;
  uint32_t owned_collision_blocked_release_ticks = 0;
  double owned_collision_blocked_candidate_distance = 0.0;
  uint64_t owned_collision_blocker_key = 0;
  bool owned_publication_active = false;
};

ThirdPersonOrbitState g_third_person_orbit_state;
std::mutex g_camera_collision_mesh_mutex;
std::unordered_map<uintptr_t, CameraCollisionMesh>
    g_camera_collision_meshes;
std::unordered_set<uintptr_t> g_camera_nonblocking_meshes_logged;
std::unordered_set<uintptr_t> g_camera_renderer_skipped_nodes_logged;
std::unordered_set<uint64_t> g_camera_room_sectors_logged;
uintptr_t g_camera_room_collection_base = 0;

struct RuntimeRoomGraphCache {
  uintptr_t collection = 0;
  uintptr_t sector_base = 0;
  int32_t sector_count = 0;
  std::vector<deathtrap_camera::RoomSector> sectors;
  std::vector<std::vector<uintptr_t>> portal_addresses;
  bool attempted = false;
  bool valid = false;
};

RuntimeRoomGraphCache g_runtime_room_graph;
uint64_t g_camera_mesh_cache_hits = 0;
uint64_t g_camera_mesh_cache_misses = 0;
uint64_t g_camera_mesh_sweeps = 0;
uint64_t g_camera_temporal_chord_guards = 0;

enum class CustomCameraViewMode : uint32_t {
  kModernThirdPerson = 0,
  kHead = 1,
  kRetail = 2,
};

std::atomic<uint32_t> g_custom_camera_view_mode{
    static_cast<uint32_t>(CustomCameraViewMode::kModernThirdPerson)};
std::atomic<bool> g_custom_head_publication_active{false};
std::atomic<uint64_t> g_custom_head_last_publication_ms{0};
std::atomic<bool> g_scripted_camera_override_active{false};
// A full-owned source state may jump between disconnected safe shot regions.
// The source hook sets this once; the presentation scheduler consumes it and
// suppresses interpolation across that one transition.
std::atomic<bool> g_owned_camera_presentation_cut_pending{false};

struct RetailCameraArbitrationState {
  bool raw_owner_active = false;
  bool takeover_latched = false;
  bool previous_player_valid = false;
  bool previous_native_candidate_valid = false;
  uintptr_t owner = 0;
  std::array<int32_t, 3> previous_player{};
  std::array<int32_t, 3> previous_native_candidate{};
  double stationary_native_motion = 0.0;
  uint32_t moving_ticks = 0;
  uint32_t settled_ticks = 0;
  uint32_t owner_release_ticks = 0;
  uint32_t candidate_motion_ticks = 0;
  uint32_t idle_native_quiet_ticks = 0;
  bool owner_seen_during_takeover = false;
  bool ownerless_interaction_armed = false;
  bool interaction_scripted_view_transition_seen = false;
  bool previous_scripted_view_active = false;
  bool interaction_consumed = false;
  uintptr_t takeover_owner = 0;
  uint64_t last_interaction_sequence = 0;
  uint64_t takeover_started_ms = 0;
  uint64_t cooldown_until_ms = 0;
};

RetailCameraArbitrationState g_retail_camera_arbitration;
std::array<uintptr_t, 16> g_completed_script_camera_owners{};
size_t g_completed_script_camera_owner_count = 0;

struct Mode3SourceTickState {
  void* controller = nullptr;
  int32_t engine_frame = 0;
  bool valid = false;
  uint64_t duplicate_calls = 0;
};

Mode3SourceTickState g_mode3_source_tick;

// The first retail mode-3 call is a probe used only for authored-camera
// arbitration. Roll its mutable resolver/history block back when the probe did
// not take ownership, otherwise the final orbit pass integrates the camera
// history twice and produces periodic running jolts.
constexpr size_t kCameraProbeStateBeginOffset =
    kCameraControllerResolvedPositionOffset;
constexpr size_t kCameraProbeStateEndOffset =
    kCameraControllerPositionHistorySamplesOffset +
    kCameraControllerPositionHistorySampleCount *
        kCameraControllerPositionHistorySampleStride;
constexpr size_t kCameraProbeStateSize =
    kCameraProbeStateEndOffset - kCameraProbeStateBeginOffset;

struct RetailCameraProbeSnapshot {
  std::array<uint8_t, kCameraProbeStateSize> bytes{};
  bool valid = false;
};

CustomCameraViewMode CurrentCustomCameraViewMode() {
  const uint32_t raw = g_custom_camera_view_mode.load(
      std::memory_order_acquire);
  return raw <= static_cast<uint32_t>(CustomCameraViewMode::kRetail)
             ? static_cast<CustomCameraViewMode>(raw)
             : CustomCameraViewMode::kModernThirdPerson;
}

const char* CustomCameraViewModeName(CustomCameraViewMode mode) {
  switch (mode) {
    case CustomCameraViewMode::kModernThirdPerson:
      return "MODERN_THIRD_PERSON";
    case CustomCameraViewMode::kHead:
      return "IMMERSIVE_FIRST_PERSON";
    case CustomCameraViewMode::kRetail:
      return "RETAIL";
  }
  return "UNKNOWN";
}

bool CustomHeadViewSelected() {
  return g_immersive_first_person_enabled &&
      CurrentCustomCameraViewMode() == CustomCameraViewMode::kHead;
}

bool CustomCameraOwnsMode3() {
  // Runtime 0.0.61 deliberately exposes one gameplay camera only.  Keeping a
  // single persistent owner avoids retail/head transitions resetting the
  // orbit state, mouse routing and collision spring independently.
  return true;
}

struct CustomCameraTransitionState {
  bool initialized = false;
  CustomCameraViewMode previous_mode =
      CustomCameraViewMode::kModernThirdPerson;
};

CustomCameraTransitionState g_custom_camera_transition;
bool g_weapon_wheel_enabled = false;
bool g_weapon_wheel_invert = false;
bool g_xinput_enabled = false;
bool g_xinput_base_bindings = false;
uint32_t g_xinput_controller_index = 0;
uint32_t g_xinput_selector_hold_ms = 225;
int32_t g_xinput_left_deadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
int32_t g_xinput_right_deadzone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
int32_t g_xinput_trigger_threshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
int32_t g_xinput_first_person_pixels = 12;
int32_t g_xinput_menu_mouse_pixels = 6;
double g_xinput_right_stick_curve = 1.35;
double g_xinput_right_stick_axis_lock_ratio = 0.25;
bool g_xinput_invert_right_y = false;
uint32_t g_ui_message_lifetime_percent = 300u;
uint32_t g_ui_message_lifetime_ticks = 150u;
uint32_t g_pst_message_lifetime_ticks = 81u;
bool g_ui_message_lifetime_patched = false;
bool g_pst_message_lifetime_patched = false;
int32_t g_xinput_selector_radius = 104;
int32_t g_xinput_selector_center_y = 316;
double g_xinput_movement_threshold = 0.14;
double g_xinput_run_threshold = 0.50;
double g_xinput_run_release_threshold = 0.30;
bool g_xinput_camera_relative_movement = true;
bool g_xinput_camera_relative_invert_y = true;
double g_xinput_movement_turn_degrees_per_tick = 12.0;
bool g_xinput_run_active = false;
uint64_t g_xinput_chalk_actions_asserted = 0;
uint64_t g_weapon_wheel_switches = 0;
uint64_t g_weapon_wheel_rejections = 0;
uint64_t g_suppressed_midpoints = 0;
uint64_t g_last_transition_log_tick = 0;
uint64_t g_ui_state_restores = 0;
uint64_t g_ui_state_changes = 0;
uint64_t g_ui_logic_rollbacks = 0;
uint64_t g_player_state_rollback_events = 0;
uint64_t g_player_state_rollback_fields = 0;
uint64_t g_player_state_restore_failures = 0;
uint64_t g_last_player_state_rollback_log_tick = 0;
int64_t g_player_state_rollback_maximum_delta = 0;
uint64_t g_player_contact_settles = 0;
uint64_t g_player_raw_contact_detections = 0;
uint64_t g_player_matrix_contact_candidates = 0;
uint64_t g_player_raw_contact_candidates = 0;
uint32_t g_player_contact_exact_cooldown = 0;
uint32_t g_player_contact_hit_window = 0;
uint32_t g_player_contact_hits_in_window = 0;
uint32_t g_player_persistent_contact_cooldown = 0;
Vec3 g_player_contact_outward_normal{};
Vec3 g_player_contact_anchor{};
bool g_player_contact_normal_valid = false;
bool g_player_contact_anchor_valid = false;
uint32_t g_player_contact_release_streak = 0;
uint32_t g_player_contact_quiet_ticks = 0;
double g_player_contact_release_alignment = 0.0;
double g_player_contact_outward_distance = 0.0;
double g_player_contact_correction_length = 0.0;
uint64_t g_player_contact_directional_releases = 0;
uint64_t g_player_contact_manifold_events = 0;
uint64_t g_player_contact_manifold_midpoint_nodes = 0;
uint64_t g_player_contact_manifold_endpoint_nodes = 0;
uint64_t g_player_contact_normal_flip_rejections = 0;
double g_player_contact_manifold_max_depth = 0.0;
uint64_t g_last_player_probe_log_tick = 0;
uint64_t g_last_bounds_apply_log_tick = 0;
using AxisReturnBins = std::array<std::array<uint64_t, 4>, 3>;
AxisReturnBins g_matrix_axis_return_bins{};
AxisReturnBins g_raw_axis_return_bins{};
uint32_t g_transition_guard_cooldown = 0;
uint64_t g_page_restore_flips = 0;
uint64_t g_ui_owner_ticks = 0;
uint64_t g_ui_object_ticks = 0;
uint64_t g_ui_gate_open_ticks = 0;
uint64_t g_ui_gate_closed_ticks = 0;
int64_t g_last_ui_frame_delta = 0;

using RenderPresentWaitFn = void(__cdecl*)(void* context, int wait);
using RendererFn = void(__cdecl*)(void* context);
using InventorySlotDrawFn = void(__cdecl*)(void* slot);
using RenderCacheUpdateFn = void(__cdecl*)(void* owner);
using BackendFlipFn = void(__cdecl*)();
using DamageHandlerFn = int(__cdecl*)(void* target, int32_t requested_damage,
                                     uintptr_t damage_flags,
                                     uintptr_t impact_event,
                                     uintptr_t source);
using MeleeAttackWindowFn = int(__cdecl*)(void* actor);
using SuccessfulBlockImpactFn = void(__cdecl*)(void* actor);
using OffensiveSpellLaunchFn = void*(__cdecl*)(void* actor,
                                                void* launch_context,
                                                void* launch_output);
using RangedWeaponLaunchFn = void*(__cdecl*)(void* actor,
                                             void* launch_context,
                                             void* launch_output);
using UseConsumableFn = void(__cdecl*)(int32_t item_id);
using PlayerTurnFn = void(__cdecl*)(void* player);
using PlayerLocomotionFn = void(__cdecl*)(void* controller);
using PlayerStateDispatcherFn = void(__cdecl*)(void* outer_player);
using PlayerRootMotionTransformFn = void(__cdecl*)(
    void* node, int32_t local_x, int32_t local_y, int32_t local_z);
using PlayerMovementStageFn = uintptr_t(__cdecl*)();
using NativeJoystickPollFn = int(__cdecl*)(int32_t* x, int32_t* y,
                                            uint32_t* buttons);
using NativeJoystickEnabledFn = int(__cdecl*)();
using Mode3CameraFn = void(__cdecl*)(void* controller);
using ConfigureCameraFn = void(__cdecl*)(void* controller, int32_t x,
                                         int32_t y, int32_t z,
                                         uintptr_t room_or_sector,
                                         int32_t update_flags);
using ResolveCameraSectorFn = uintptr_t(__cdecl*)(
    const int32_t* position, uintptr_t seed_sector);
using CameraVolumeVisibleFn = int(__cdecl*)(
    const int32_t* endpoint, uintptr_t endpoint_sector,
    const int32_t* focus, uintptr_t focus_sector);
using CameraRoomTraceVisibleFn = int(__cdecl*)(
    uintptr_t endpoint_sector, const int32_t* endpoint,
    uintptr_t focus_sector, const int32_t* focus);
using CameraLookAtFn = void(__cdecl*)(const int32_t* camera_position,
                                      const int32_t* look_target,
                                      int32_t* angles);
using CameraHistoryAddFn = int32_t*(__cdecl*)(void* history,
                                              const int32_t* position);
using RedbookTracksFn = uint32_t(__stdcall*)(void* handle);
using RedbookPlayFn = int32_t(__stdcall*)(void* handle, uint32_t start,
                                          uint32_t end);

RenderPresentWaitFn g_original_render_present_wait = nullptr;
RendererFn g_renderer = nullptr;
RendererFn g_original_renderer = nullptr;
InventorySlotDrawFn g_original_inventory_slot_draw = nullptr;
RenderCacheUpdateFn g_scene_cache_update = nullptr;
RenderCacheUpdateFn g_camera_cache_update = nullptr;
RenderCacheUpdateFn g_camera_node_local_update = nullptr;
RenderCacheUpdateFn g_camera_node_world_update = nullptr;
DamageHandlerFn g_original_damage_handler = nullptr;
MeleeAttackWindowFn g_original_melee_attack_window = nullptr;
SuccessfulBlockImpactFn g_original_successful_block_impact = nullptr;
OffensiveSpellLaunchFn g_original_offensive_spell_launch = nullptr;
RangedWeaponLaunchFn g_original_ranged_weapon_launch = nullptr;
UseConsumableFn g_original_use_consumable = nullptr;
PlayerTurnFn g_original_player_turn = nullptr;
PlayerLocomotionFn g_original_player_locomotion = nullptr;
PlayerTurnFn g_player_turn_writer = nullptr;
PlayerStateDispatcherFn g_original_player_state_dispatcher = nullptr;
PlayerRootMotionTransformFn g_player_root_motion_transform = nullptr;
PlayerMovementStageFn g_original_player_movement_stage = nullptr;
NativeJoystickPollFn g_original_native_joystick_poll = nullptr;
NativeJoystickEnabledFn g_original_native_joystick_enabled = nullptr;
Mode3CameraFn g_original_mode3_camera = nullptr;
ConfigureCameraFn g_configure_camera = nullptr;
ResolveCameraSectorFn g_resolve_camera_sector = nullptr;
CameraVolumeVisibleFn g_camera_volume_visible = nullptr;
CameraRoomTraceVisibleFn g_camera_room_trace_visible = nullptr;
CameraLookAtFn g_original_camera_look_at = nullptr;
CameraHistoryAddFn g_original_camera_history_add = nullptr;
RedbookTracksFn g_original_redbook_tracks = nullptr;
RedbookPlayFn g_original_redbook_play = nullptr;
uint32_t* g_steam_mss_mp3_track_index = nullptr;
std::atomic<int32_t> g_last_routed_music_track{-1};

struct CameraLookAtCapture {
  bool active = false;
  bool valid = false;
  std::array<int32_t, 3> camera_position{};
  std::array<int32_t, 3> look_target{};
};

thread_local CameraLookAtCapture g_camera_look_at_capture;

struct CameraPreHistoryMeshVetoScope {
  bool active = false;
  bool candidate_seen = false;
  bool mesh_contact = false;
  bool replacement_valid = false;
  bool replacement_applied = false;
  bool used_submitted_fallback = false;
  bool submitted_validation_attempted = false;
  bool submitted_validation_valid = false;
  bool submitted_validation_unchanged = false;
  void* controller = nullptr;
  std::array<int32_t, 3> focus{};
  std::array<int32_t, 3> submitted{};
  std::array<int32_t, 3> submitted_validation_result{};
  std::array<int32_t, 3> candidate{};
  std::array<int32_t, 3> replacement{};
  CameraMeshHitDiagnostic diagnostic;
};

thread_local CameraPreHistoryMeshVetoScope
    g_camera_pre_history_mesh_veto;

struct RawCameraCollisionCapture {
  bool render_mesh_contact = false;
  double render_mesh_radius = 0.0;
  void* controller = nullptr;
  std::array<int32_t, 3> player{};
  std::array<int32_t, 3> requested{};
};

thread_local RawCameraCollisionCapture g_raw_camera_collision_capture;
thread_local ActivePresentationTrace g_active_presentation_trace;
std::vector<PresentationTraceSample> g_presentation_trace_buffer;

bool SafeRead(const void* source, void* destination, size_t size) {
  __try {
    std::memcpy(destination, source, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool SafeWrite(void* destination, const void* source, size_t size) {
  __try {
    std::memcpy(destination, source, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

template <typename T>
bool SafeReadValue(const void* address, T* value) {
  return SafeRead(address, value, sizeof(*value));
}

bool RetailFirstPersonActive() {
  if (!g_dungeon_base) {
    return false;
  }
  uint8_t mode = 0;
  return SafeReadValue(
             g_dungeon_base + kCameraControllerRva +
                 kCameraControllerActiveModeOffset,
             &mode) &&
         mode == 4u;
}

UiRenderStateSnapshot CaptureUiRenderState() {
  UiRenderStateSnapshot snapshot;
  if (!g_dungeon_base) {
    return snapshot;
  }
  snapshot.control_valid =
      SafeRead(g_dungeon_base + kUiRenderStateRva,
               snapshot.control.data(), snapshot.control.size());
  snapshot.messages_valid =
      SafeRead(g_dungeon_base + kUiMessageStateRva,
               snapshot.messages.data(), snapshot.messages.size());
  snapshot.message_entries_valid =
      SafeRead(g_dungeon_base + kUiMessageEntriesRva,
               snapshot.message_entries.data(),
               snapshot.message_entries.size());
  snapshot.pst_messages_valid =
      SafeRead(g_dungeon_base + kPstMessageStateRva,
               snapshot.pst_messages.data(), snapshot.pst_messages.size());
  snapshot.countdowns_valid =
      SafeRead(g_dungeon_base + kUiCountdownStateRva,
               snapshot.countdowns.data(), snapshot.countdowns.size());
  snapshot.frame_stamp_valid = SafeReadValue(
      g_dungeon_base + kUiFrameStampRva, &snapshot.frame_stamp);
  snapshot.selector_mode_valid = SafeReadValue(
      g_dungeon_base + kUiSelectorModeRva, &snapshot.selector_mode);
  return snapshot;
}

bool RestoreUiRenderState(const UiRenderStateSnapshot& snapshot) {
  bool restored = false;
  if (snapshot.control_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiRenderStateRva,
                          snapshot.control.data(), snapshot.control.size());
  }
  if (snapshot.messages_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiMessageStateRva,
                          snapshot.messages.data(), snapshot.messages.size());
  }
  if (snapshot.message_entries_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiMessageEntriesRva,
                          snapshot.message_entries.data(),
                          snapshot.message_entries.size());
  }
  if (snapshot.pst_messages_valid) {
    restored |= SafeWrite(g_dungeon_base + kPstMessageStateRva,
                          snapshot.pst_messages.data(),
                          snapshot.pst_messages.size());
  }
  if (snapshot.countdowns_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiCountdownStateRva,
                          snapshot.countdowns.data(),
                          snapshot.countdowns.size());
  }
  if (snapshot.frame_stamp_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiFrameStampRva,
                          &snapshot.frame_stamp, sizeof(snapshot.frame_stamp));
  }
  if (snapshot.selector_mode_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiSelectorModeRva,
                          &snapshot.selector_mode,
                          sizeof(snapshot.selector_mode));
  }
  return restored;
}

bool UiRenderStateChanged(const UiRenderStateSnapshot& before,
                          const UiRenderStateSnapshot& after) {
  return (before.control_valid && after.control_valid &&
          before.control != after.control) ||
         (before.messages_valid && after.messages_valid &&
          before.messages != after.messages) ||
         (before.message_entries_valid && after.message_entries_valid &&
          before.message_entries != after.message_entries) ||
         (before.pst_messages_valid && after.pst_messages_valid &&
          before.pst_messages != after.pst_messages) ||
         (before.countdowns_valid && after.countdowns_valid &&
          before.countdowns != after.countdowns) ||
         (before.frame_stamp_valid && after.frame_stamp_valid &&
          before.frame_stamp != after.frame_stamp) ||
         (before.selector_mode_valid && after.selector_mode_valid &&
          before.selector_mode != after.selector_mode);
}

bool UiTransientLogicChanged(const UiRenderStateSnapshot& before,
                             const UiRenderStateSnapshot& after) {
  return (before.messages_valid && after.messages_valid &&
          before.messages != after.messages) ||
         (before.message_entries_valid && after.message_entries_valid &&
          before.message_entries != after.message_entries) ||
         (before.pst_messages_valid && after.pst_messages_valid &&
          before.pst_messages != after.pst_messages) ||
         (before.countdowns_valid && after.countdowns_valid &&
          before.countdowns != after.countdowns) ||
         (before.selector_mode_valid && after.selector_mode_valid &&
          before.selector_mode != after.selector_mode);
}

void SampleUiEligibility() {
  uintptr_t owner = 0;
  uintptr_t ui_context = 0;
  uintptr_t ui_object = 0;
  int32_t engine_frame = 0;
  int32_t frame_stamp = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &owner) || !owner) {
    return;
  }
  ++g_ui_owner_ticks;
  SafeReadValue(reinterpret_cast<const void*>(owner + 0x2Cu), &ui_context);
  if (ui_context) {
    SafeReadValue(reinterpret_cast<const void*>(ui_context + 0x1030u),
                  &ui_object);
  }
  if (ui_object) {
    ++g_ui_object_ticks;
  }
  if (!SafeReadValue(g_dungeon_base + kEngineFrameCounterRva,
                     &engine_frame) ||
      !SafeReadValue(g_dungeon_base + kUiFrameStampRva, &frame_stamp)) {
    return;
  }
  g_last_ui_frame_delta = std::llabs(
      static_cast<long long>(frame_stamp) -
      static_cast<long long>(engine_frame));
  if (g_last_ui_frame_delta >= 2) {
    ++g_ui_gate_open_ticks;
  } else {
    ++g_ui_gate_closed_ticks;
  }
}

std::wstring ModuleDirectory() {
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&InitializeDeathtrapNativeRenderPatch),
          &self)) {
    return L".";
  }
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
  if (!length || length >= MAX_PATH) {
    return L".";
  }
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash) {
    *slash = L'\0';
  }
  return path;
}

std::wstring ConfigurationPath() {
  return ModuleDirectory() + L"\\deathtrap_native.ini";
}

bool IsExpectedDungeonImage(uint8_t* base) {
  if (!base) {
    return false;
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    return false;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      base + static_cast<uintptr_t>(dos->e_lfanew));
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
      nt->FileHeader.TimeDateStamp != kExpectedTimestamp ||
      nt->OptionalHeader.SizeOfImage != kExpectedImageSize) {
    return false;
  }
  return std::memcmp(base + kRateConsumerSignatureRva,
                     kRateConsumerSignature.data(),
                     kRateConsumerSignature.size()) == 0;
}

uint32_t ConfiguredSubframes() {
  const std::wstring ini = ConfigurationPath();
  if (GetPrivateProfileIntW(L"NativeRender", L"Enabled", 0,
                            ini.c_str()) == 0) {
    return 0;
  }
  // Integer x3 renders two clock-isolated phases plus the exact native frame.
  // Integer x2 remains selectable as a conservative fallback.
  const int configured = GetPrivateProfileIntW(
      L"NativeRender", L"Subframes", 3, ini.c_str());
  if (configured >= 3) {
    return 3u;
  }
  return configured == 2 ? 2u : 0u;
}

bool ConfiguredDebugLog() {
  const std::wstring ini = ConfigurationPath();
  return GetPrivateProfileIntW(L"Diagnostics", L"DebugLog", 0,
                               ini.c_str()) != 0;
}

bool ConfiguredWeaponWheelEnabled() {
  const std::wstring ini = ConfigurationPath();
  return GetPrivateProfileIntW(L"WeaponWheel", L"Enabled", 1,
                               ini.c_str()) != 0;
}

bool ConfiguredWeaponWheelInvert() {
  const std::wstring ini = ConfigurationPath();
  return GetPrivateProfileIntW(L"WeaponWheel", L"Invert", 0,
                               ini.c_str()) != 0;
}

int ConfiguredInteger(const wchar_t* section, const wchar_t* key,
                      int default_value) {
  const std::wstring ini = ConfigurationPath();
  return GetPrivateProfileIntW(section, key, default_value, ini.c_str());
}

bool PatchMessageLifetimeImmediate(uintptr_t rva, uint32_t expected,
                                   uint32_t ticks) {
  if (!g_dungeon_base || ticks == 0u) {
    return false;
  }
  uint32_t original = 0;
  uint8_t* immediate = g_dungeon_base + rva;
  if (!SafeReadValue(immediate, &original) || original != expected) {
    return false;
  }
  DWORD old_protection = 0;
  if (!VirtualProtect(immediate, sizeof(ticks), PAGE_EXECUTE_READWRITE,
                      &old_protection)) {
    return false;
  }
  const bool written = SafeWrite(immediate, &ticks, sizeof(ticks));
  FlushInstructionCache(GetCurrentProcess(), immediate, sizeof(ticks));
  DWORD ignored = 0;
  VirtualProtect(immediate, sizeof(ticks), old_protection, &ignored);
  uint32_t verified = 0;
  return written && SafeReadValue(immediate, &verified) && verified == ticks;
}

bool PatchUiMessageLifetime(uint32_t ticks) {
  return PatchMessageLifetimeImmediate(kUiMessageLifetimeImmediateRva,
                                       kOriginalUiMessageLifetimeTicks,
                                       ticks);
}

bool PatchPstMessageLifetime(uint32_t ticks) {
  return PatchMessageLifetimeImmediate(kPstMessageLifetimeImmediateRva,
                                       kOriginalPstMessageLifetimeTicks,
                                       ticks);
}

void WriteNativeLogBytes(const void* data, size_t size) {
  if (!data || size == 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_native_log_mutex);
  if (g_native_log_file == INVALID_HANDLE_VALUE) {
    g_native_log_file = CreateFileW(
        GetDeathtrapSessionLogPath(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_native_log_file == INVALID_HANDLE_VALUE) {
      return;
    }
  }
  const auto* bytes = static_cast<const uint8_t*>(data);
  size_t remaining = size;
  while (remaining != 0) {
    const DWORD chunk = static_cast<DWORD>(std::min<size_t>(
        remaining, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
    DWORD written = 0;
    if (!WriteFile(g_native_log_file, bytes, chunk, &written, nullptr) ||
        written == 0) {
      CloseHandle(g_native_log_file);
      g_native_log_file = INVALID_HANDLE_VALUE;
      return;
    }
    bytes += written;
    remaining -= written;
  }
}

void AppendNativeLog(const char* format, ...) {
  if (!g_debug_log) {
    return;
  }
  // Periodic resolver telemetry is intentionally dense. A 1 KiB buffer
  // caused vsnprintf's truncation terminator to hide the counters at the end
  // of each line.
  char line[4096] = {};
  va_list args;
  va_start(args, format);
  const int length = std::vsnprintf(line, sizeof(line) - 2, format, args);
  va_end(args);
  if (length <= 0) {
    return;
  }
  const size_t used = std::min<size_t>(static_cast<size_t>(length),
                                       sizeof(line) - 2);
  line[used] = '\r';
  line[used + 1] = '\n';
  WriteNativeLogBytes(line, used + 2);
}

void AppendSupportLogV(const char* format, va_list args) {
  if (!format) {
    return;
  }
  char line[2048] = {};
  const int length = std::vsnprintf(line, sizeof(line) - 2, format, args);
  if (length <= 0) {
    return;
  }
  const size_t used = std::min<size_t>(static_cast<size_t>(length),
                                       sizeof(line) - 2);
  line[used] = '\r';
  line[used + 1] = '\n';
  WriteNativeLogBytes(line, used + 2);
}

std::string SupportIniValue(const std::wstring& path,
                            const wchar_t* section, const wchar_t* key,
                            const wchar_t* fallback = L"missing") {
  wchar_t value[160] = {};
  GetPrivateProfileStringW(section, key, fallback, value,
                           static_cast<DWORD>(std::size(value)), path.c_str());
  std::string result;
  result.reserve(wcslen(value));
  for (const wchar_t character : std::wstring(value)) {
    result.push_back(character >= 0x20 && character <= 0x7e
                         ? static_cast<char>(character)
                         : '?');
  }
  return result;
}

std::wstring FileNameOnly(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::string SupportDgVoodooConfigVersion(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return "missing";
  }
  char bytes[4097] = {};
  DWORD read = 0;
  const bool loaded = ReadFile(file, bytes, sizeof(bytes) - 1, &read, nullptr)
      != FALSE;
  CloseHandle(file);
  if (!loaded || read == 0) {
    return "unknown";
  }
  const std::string text(bytes, read);
  size_t line_start = 0;
  while (line_start < text.size()) {
    const size_t line_end = text.find_first_of("\r\n", line_start);
    const std::string line = text.substr(
        line_start, line_end == std::string::npos
                        ? std::string::npos
                        : line_end - line_start);
    const size_t equals = line.find('=');
    if (equals != std::string::npos) {
      std::string key = line.substr(0, equals);
      std::string value = line.substr(equals + 1);
      auto trim = [](std::string* item) {
        const size_t first = item->find_first_not_of(" \t");
        const size_t last = item->find_last_not_of(" \t");
        *item = first == std::string::npos
            ? std::string()
            : item->substr(first, last - first + 1);
      };
      trim(&key);
      trim(&value);
      if (key == "Version") {
        return value.empty() ? "unknown" : value;
      }
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_start = text.find_first_not_of("\r\n", line_end);
    if (line_start == std::string::npos) {
      break;
    }
  }
  return "unknown";
}

void LogCompactSupportEnvironment(uint32_t subframes) {
  wchar_t executable_path[MAX_PATH] = {};
  const DWORD executable_length =
      GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
  const std::wstring executable = executable_length &&
          executable_length < MAX_PATH
      ? FileNameOnly(executable_path)
      : L"unknown";

  DWORD os_major = 0;
  DWORD os_minor = 0;
  DWORD os_build = 0;
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtl_get_version) {
      OSVERSIONINFOW version = {};
      version.dwOSVersionInfoSize = sizeof(version);
      if (rtl_get_version(&version) == 0) {
        os_major = version.dwMajorVersion;
        os_minor = version.dwMinorVersion;
        os_build = version.dwBuildNumber;
      }
    }
  }

  UINT system_dpi = 96;
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    using GetDpiForSystemFn = UINT(WINAPI*)();
    const auto get_dpi_for_system = reinterpret_cast<GetDpiForSystemFn>(
        GetProcAddress(user32, "GetDpiForSystem"));
    if (get_dpi_for_system) {
      system_dpi = get_dpi_for_system();
    }
  }

  const std::wstring dgvoodoo = ModuleDirectory() + L"\\dgVoodoo.conf";
  const bool dgvoodoo_config_present =
      GetFileAttributesW(dgvoodoo.c_str()) != INVALID_FILE_ATTRIBUTES;
  const auto dgvoodoo_version = SupportDgVoodooConfigVersion(dgvoodoo);
  const auto output_api = SupportIniValue(dgvoodoo, L"General", L"OutputAPI");
  const auto fullscreen =
      SupportIniValue(dgvoodoo, L"General", L"FullScreenMode");
  const auto scaling =
      SupportIniValue(dgvoodoo, L"General", L"ScalingMode");
  const auto capture =
      SupportIniValue(dgvoodoo, L"General", L"CaptureMouse");
  const auto cursor_scale =
      SupportIniValue(dgvoodoo, L"GeneralExt", L"CursorScaleFactor");
  const auto free_mouse =
      SupportIniValue(dgvoodoo, L"GeneralExt", L"FreeMouse");
  const auto directx_resolution =
      SupportIniValue(dgvoodoo, L"DirectX", L"Resolution");
  const auto antialiasing =
      SupportIniValue(dgvoodoo, L"DirectX", L"Antialiasing");

  AppendDeathtrapSupportLog(
      "support_start version=" DEATHTRAP_NATIVE_VERSION
      " executable=%ls pid=%lu os=%lu.%lu.%lu dpi=%u scale=%u%% "
      "desktop=%dx%d virtual=%d,%d,%dx%d subframes=%u debug=%u steam=%u",
      executable.c_str(), static_cast<unsigned long>(GetCurrentProcessId()),
      static_cast<unsigned long>(os_major),
      static_cast<unsigned long>(os_minor),
      static_cast<unsigned long>(os_build), system_dpi,
      static_cast<unsigned>((system_dpi * 100u + 48u) / 96u),
      GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
      GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
      GetSystemMetrics(SM_CXVIRTUALSCREEN),
      GetSystemMetrics(SM_CYVIRTUALSCREEN), subframes,
      g_debug_log ? 1u : 0u,
      GetModuleHandleW(L"gameoverlayrenderer.dll") ? 1u : 0u);
  AppendDeathtrapSupportLog(
      "support_dgvoodoo config=%u version=%s ddraw=%u d3dimm=%u output=%s "
      "fullscreen=%s scaling=%s capture_mouse=%s cursor_scale=%s "
      "free_mouse=%s directx_resolution=%s antialiasing=%s",
      dgvoodoo_config_present ? 1u : 0u,
      dgvoodoo_version.c_str(),
      GetModuleHandleW(L"ddraw.dll") ? 1u : 0u,
      GetModuleHandleW(L"D3DImm.dll") ? 1u : 0u, output_api.c_str(),
      fullscreen.c_str(), scaling.c_str(), capture.c_str(),
      cursor_scale.c_str(), free_mouse.c_str(), directx_resolution.c_str(),
      antialiasing.c_str());
}

void AppendNativeLogBlock(const std::string& block) {
  if (!g_debug_log || block.empty()) {
    return;
  }
  WriteNativeLogBytes(block.data(), block.size());
}

uint32_t __stdcall HookRedbookTracks(void*) {
  // Track 1 is the retail data track.  Steam ships all fifteen audio tracks
  // as MP3 files, so the CD-compatible count is 16, not the wrapper's
  // hard-coded 9.
  return deathtrap_music::kRedbookTrackCount;
}

int32_t __stdcall HookRedbookPlay(void* handle, uint32_t start,
                                  uint32_t end) {
  uint32_t selected_cd_track = 0;
  const bool selected_read =
      g_dungeon_base &&
      SafeReadValue(g_dungeon_base + kCurrentRedbookTrackRva,
                    &selected_cd_track);
  const deathtrap_music::TrackRoute route =
      deathtrap_music::RouteCdTrackToSteamMp3(selected_cd_track);
  bool routed = false;
  if (selected_read && route.valid && g_steam_mss_mp3_track_index) {
    routed = SafeWrite(g_steam_mss_mp3_track_index, &route.mp3_index,
                       sizeof(route.mp3_index));
  }

  const int32_t previous = g_last_routed_music_track.exchange(
      routed ? static_cast<int32_t>(selected_cd_track) : -1,
      std::memory_order_acq_rel);
  if (g_debug_log &&
      previous != (routed ? static_cast<int32_t>(selected_cd_track) : -1)) {
    AppendNativeLog(
        "music_redbook route=%s cd_track=%u mp3=%u start=%u end=%u",
        routed ? "STEAM_MP3" : "ORIGINAL", selected_cd_track,
        route.valid ? route.mp3_index : 0u, start, end);
  }

  return g_original_redbook_play
             ? g_original_redbook_play(handle, start, end)
             : 0;
}

bool IsKnownSteamMssWrapper(HMODULE module, uint8_t* tracks,
                            uint8_t* play, uint8_t* track_info,
                            uint32_t** track_index) {
  if (!module || !tracks || !play || !track_info || !track_index) {
    return false;
  }

  wchar_t path[MAX_PATH] = {};
  WIN32_FILE_ATTRIBUTE_DATA attributes = {};
  const DWORD path_length = GetModuleFileNameW(module, path, MAX_PATH);
  if (!path_length || path_length >= MAX_PATH ||
      !GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) ||
      attributes.nFileSizeHigh != 0u || attributes.nFileSizeLow != 25600u) {
    return false;
  }

  constexpr std::array<uint8_t, 12> kExpectedTracks = {
      0x55, 0x8B, 0xEC, 0xB8, 0x09, 0x00,
      0x00, 0x00, 0x5D, 0xC2, 0x04, 0x00};
  std::array<uint8_t, kExpectedTracks.size()> tracks_bytes{};
  if (!SafeRead(tracks, tracks_bytes.data(), tracks_bytes.size()) ||
      tracks_bytes != kExpectedTracks) {
    return false;
  }

  constexpr std::array<uint8_t, 5> kExpectedPlayPrefix = {
      0x55, 0x8B, 0xEC, 0xC7, 0x05};
  std::array<uint8_t, kExpectedPlayPrefix.size()> play_bytes{};
  if (!SafeRead(play, play_bytes.data(), play_bytes.size()) ||
      play_bytes != kExpectedPlayPrefix) {
    return false;
  }

  std::array<uint8_t, 12> track_info_bytes{};
  if (!SafeRead(track_info, track_info_bytes.data(),
                track_info_bytes.size()) ||
      track_info_bytes[0] != 0x55 || track_info_bytes[1] != 0x8B ||
      track_info_bytes[2] != 0xEC || track_info_bytes[3] != 0x51 ||
      track_info_bytes[4] != 0xA1 || track_info_bytes[9] != 0x3B ||
      track_info_bytes[10] != 0x45 || track_info_bytes[11] != 0x0C) {
    return false;
  }

  uint32_t storage_address = 0;
  std::memcpy(&storage_address, track_info_bytes.data() + 5,
              sizeof(storage_address));
  auto* const storage = reinterpret_cast<uint32_t*>(
      static_cast<uintptr_t>(storage_address));

  const auto* const base = reinterpret_cast<const uint8_t*>(module);
  IMAGE_DOS_HEADER dos = {};
  if (!SafeRead(base, &dos, sizeof(dos)) ||
      dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
    return false;
  }
  IMAGE_NT_HEADERS32 nt = {};
  if (!SafeRead(base + static_cast<uintptr_t>(dos.e_lfanew), &nt,
                sizeof(nt)) ||
      nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    return false;
  }
  const uintptr_t module_begin = reinterpret_cast<uintptr_t>(base);
  const uintptr_t module_end = module_begin + nt.OptionalHeader.SizeOfImage;
  const uintptr_t storage_value = reinterpret_cast<uintptr_t>(storage);
  if (storage_value < module_begin ||
      storage_value + sizeof(uint32_t) > module_end) {
    return false;
  }
  MEMORY_BASIC_INFORMATION memory = {};
  if (VirtualQuery(storage, &memory, sizeof(memory)) != sizeof(memory) ||
      memory.State != MEM_COMMIT ||
      (memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READWRITE |
                         PAGE_EXECUTE_WRITECOPY)) == 0u) {
    return false;
  }
  *track_index = storage;
  return true;
}

bool InstallDeathtrapMusicTrackFix() {
  if (!g_music_track_fix_enabled) {
    AppendNativeLog("music_redbook fix=disabled");
    return true;
  }
  if (g_music_track_fix_installed.load(std::memory_order_acquire)) {
    return true;
  }

  HMODULE const mss = GetModuleHandleW(L"MSS32.DLL");
  auto* const tracks = reinterpret_cast<uint8_t*>(
      mss ? GetProcAddress(mss, "_AIL_redbook_tracks@4") : nullptr);
  auto* const play = reinterpret_cast<uint8_t*>(
      mss ? GetProcAddress(mss, "_AIL_redbook_play@12") : nullptr);
  auto* const track_info = reinterpret_cast<uint8_t*>(
      mss ? GetProcAddress(mss, "_AIL_redbook_track_info@16") : nullptr);
  uint32_t* track_index = nullptr;
  if (!IsKnownSteamMssWrapper(mss, tracks, play, track_info, &track_index)) {
    AppendNativeLog(
        "music_redbook fix=skipped reason=unsupported_mss_wrapper");
    return false;
  }

  const MH_STATUS create_play = MH_CreateHook(
      play, reinterpret_cast<void*>(&HookRedbookPlay),
      reinterpret_cast<void**>(&g_original_redbook_play));
  const MH_STATUS create_tracks = MH_CreateHook(
      tracks, reinterpret_cast<void*>(&HookRedbookTracks),
      reinterpret_cast<void**>(&g_original_redbook_tracks));
  const bool play_created =
      create_play == MH_OK || create_play == MH_ERROR_ALREADY_CREATED;
  const bool tracks_created =
      create_tracks == MH_OK || create_tracks == MH_ERROR_ALREADY_CREATED;
  if (!play_created || !tracks_created) {
    AppendNativeLog(
        "music_redbook fix=failed stage=create play=%d tracks=%d",
        static_cast<int>(create_play), static_cast<int>(create_tracks));
    return false;
  }

  g_steam_mss_mp3_track_index = track_index;
  // Playback routing must become active before the expanded track count. If
  // either enable fails, remove both hooks so the stock wrapper remains
  // internally consistent rather than exposing tracks it cannot route.
  const MH_STATUS enable_play = MH_EnableHook(play);
  const bool play_enabled =
      enable_play == MH_OK || enable_play == MH_ERROR_ENABLED;
  const MH_STATUS enable_tracks =
      play_enabled ? MH_EnableHook(tracks) : enable_play;
  const bool tracks_enabled =
      enable_tracks == MH_OK || enable_tracks == MH_ERROR_ENABLED;
  if (!play_enabled || !tracks_enabled) {
    MH_DisableHook(tracks);
    MH_DisableHook(play);
    g_steam_mss_mp3_track_index = nullptr;
    AppendNativeLog(
        "music_redbook fix=failed stage=enable play=%d tracks=%d",
        static_cast<int>(enable_play), static_cast<int>(enable_tracks));
    return false;
  }

  g_music_track_fix_installed.store(true, std::memory_order_release);
  AppendNativeLog(
      "music_redbook fix=active cd_tracks=%u mp3_files=%u "
      "mapping=cd_track_minus_2",
      deathtrap_music::kRedbookTrackCount,
      deathtrap_music::kMp3TrackCount);
  return true;
}

bool ReadRoomNeighbor(uintptr_t portal, uintptr_t* neighbor) {
  if (!portal || !neighbor) {
    return false;
  }
  uintptr_t link = 0;
  return SafeReadValue(reinterpret_cast<const void*>(portal + 0x08u), &link) &&
         link &&
         SafeReadValue(reinterpret_cast<const void*>(link + 0x0Cu), neighbor) &&
         *neighbor;
}

bool RoomSectorHasReciprocalPortal(uintptr_t sector, uintptr_t expected) {
  int32_t portal_count = 0;
  uintptr_t portals = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(sector + 0x20u),
                     &portal_count) ||
      !SafeReadValue(reinterpret_cast<const void*>(sector + 0x24u),
                     &portals) ||
      portal_count < 0 || portal_count > 128 ||
      (portal_count != 0 && !portals)) {
    return false;
  }
  for (int32_t index = 0; index < portal_count; ++index) {
    uintptr_t neighbor = 0;
    if (ReadRoomNeighbor(portals + static_cast<uintptr_t>(index) * 0x44u,
                         &neighbor) &&
        neighbor == expected) {
      return true;
    }
  }
  return false;
}

void LogCameraRoomSectorSnapshot(const std::array<int32_t, 3>& focus,
                                 uintptr_t seed_sector) {
  if (!g_debug_log || !g_dungeon_base || !g_resolve_camera_sector ||
      !seed_sector) {
    return;
  }

  uintptr_t manager = 0;
  uintptr_t collection = 0;
  int32_t sector_count = 0;
  uintptr_t sector_base = 0;
  if (!SafeReadValue(g_dungeon_base + kRetailRoomManagerPointerRva,
                     &manager) ||
      !manager ||
      !SafeReadValue(reinterpret_cast<const void*>(manager + 0x20u),
                     &collection) ||
      !collection ||
      !SafeReadValue(reinterpret_cast<const void*>(collection),
                     &sector_count) ||
      !SafeReadValue(reinterpret_cast<const void*>(collection + 0x04u),
                     &sector_base) ||
      sector_count <= 0 || sector_count > 8192 || !sector_base) {
    AppendNativeLog(
        "camera_room_contract result=COLLECTION_INVALID manager=%08llX "
        "collection=%08llX count=%d base=%08llX",
        static_cast<unsigned long long>(manager),
        static_cast<unsigned long long>(collection), sector_count,
        static_cast<unsigned long long>(sector_base));
    return;
  }

  if (sector_base != g_camera_room_collection_base) {
    g_camera_room_collection_base = sector_base;
    g_camera_room_sectors_logged.clear();
    AppendNativeLog(
        "camera_room_collection count=%d base=%08llX stride=60",
        sector_count, static_cast<unsigned long long>(sector_base));
  }

  uintptr_t sector = 0;
  __try {
    sector = g_resolve_camera_sector(focus.data(), seed_sector);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    sector = 0;
  }
  const uintptr_t sector_bytes =
      static_cast<uintptr_t>(sector_count) * 0x3Cu;
  const bool sector_in_collection = sector >= sector_base &&
      sector < sector_base + sector_bytes &&
      ((sector - sector_base) % 0x3Cu) == 0u;
  if (!sector || !sector_in_collection) {
    AppendNativeLog(
        "camera_room_contract result=SECTOR_INVALID focus=%d/%d/%d "
        "seed=%08llX resolved=%08llX base=%08llX count=%d",
        focus[0], focus[1], focus[2],
        static_cast<unsigned long long>(seed_sector),
        static_cast<unsigned long long>(sector),
        static_cast<unsigned long long>(sector_base), sector_count);
    return;
  }

  int32_t solid_count = 0;
  int32_t clip_count = 0;
  uintptr_t planes = 0;
  int32_t portal_count = 0;
  uintptr_t portals = 0;
  const bool header_valid =
      SafeReadValue(reinterpret_cast<const void*>(sector + 0x14u),
                    &solid_count) &&
      SafeReadValue(reinterpret_cast<const void*>(sector + 0x18u),
                    &clip_count) &&
      SafeReadValue(reinterpret_cast<const void*>(sector + 0x1Cu),
                    &planes) &&
      SafeReadValue(reinterpret_cast<const void*>(sector + 0x20u),
                    &portal_count) &&
      SafeReadValue(reinterpret_cast<const void*>(sector + 0x24u),
                    &portals) &&
      solid_count >= 0 && solid_count <= 128 && clip_count >= solid_count &&
      clip_count <= 256 && portal_count >= 0 && portal_count <= 128 &&
      (clip_count == 0 || planes) && (portal_count == 0 || portals);
  if (!header_valid) {
    AppendNativeLog(
        "camera_room_contract result=HEADER_INVALID sector=%08llX "
        "solid=%d clip=%d planes=%08llX portals=%d/%08llX",
        static_cast<unsigned long long>(sector), solid_count, clip_count,
        static_cast<unsigned long long>(planes), portal_count,
        static_cast<unsigned long long>(portals));
    return;
  }

  const uint64_t sector_identity =
      static_cast<uint64_t>(sector) ^
      (static_cast<uint64_t>(planes) << 1u) ^
      (static_cast<uint64_t>(portals) << 7u);
  if (!g_camera_room_sectors_logged.insert(sector_identity).second) {
    return;
  }

  const size_t sector_index = (sector - sector_base) / 0x3Cu;
  AppendNativeLog(
      "camera_room_sector index=%llu address=%08llX focus=%d/%d/%d "
      "solid=%d clip=%d planes=%08llX portals=%d/%08llX",
      static_cast<unsigned long long>(sector_index),
      static_cast<unsigned long long>(sector), focus[0], focus[1], focus[2],
      solid_count, clip_count, static_cast<unsigned long long>(planes),
      portal_count, static_cast<unsigned long long>(portals));

  for (int32_t index = 0; index < clip_count; ++index) {
    const uintptr_t plane = planes + static_cast<uintptr_t>(index) * 0x34u;
    std::array<int32_t, 3> point{};
    std::array<int32_t, 3> normal{};
    if (!SafeRead(reinterpret_cast<const void*>(plane), point.data(),
                  sizeof(point)) ||
        !SafeRead(reinterpret_cast<const void*>(plane + 0x0Cu), normal.data(),
                  sizeof(normal))) {
      AppendNativeLog(
          "camera_room_plane sector=%llu index=%d result=READ_FAILED",
          static_cast<unsigned long long>(sector_index), index);
      continue;
    }
    const double length = std::sqrt(
        static_cast<double>(normal[0]) * normal[0] +
        static_cast<double>(normal[1]) * normal[1] +
        static_cast<double>(normal[2]) * normal[2]);
    AppendNativeLog(
        "camera_room_plane sector=%llu index=%d solid=%u "
        "point=%d/%d/%d normal=%d/%d/%d length=%.1f",
        static_cast<unsigned long long>(sector_index), index,
        index < solid_count ? 1u : 0u, point[0], point[1], point[2],
        normal[0], normal[1], normal[2], length);
  }

  for (int32_t index = 0; index < portal_count; ++index) {
    const uintptr_t portal = portals + static_cast<uintptr_t>(index) * 0x44u;
    uint32_t flags = 0;
    uintptr_t neighbor = 0;
    std::array<int32_t, 3> point{};
    std::array<int32_t, 3> normal{};
    const bool portal_valid =
        SafeReadValue(reinterpret_cast<const void*>(portal + 0x04u),
                      &flags) &&
        ReadRoomNeighbor(portal, &neighbor) &&
        SafeRead(reinterpret_cast<const void*>(portal + 0x10u), point.data(),
                 sizeof(point)) &&
        SafeRead(reinterpret_cast<const void*>(portal + 0x1Cu), normal.data(),
                 sizeof(normal));
    const bool neighbor_in_collection = portal_valid &&
        neighbor >= sector_base && neighbor < sector_base + sector_bytes &&
        ((neighbor - sector_base) % 0x3Cu) == 0u;
    const size_t neighbor_index = neighbor_in_collection
        ? (neighbor - sector_base) / 0x3Cu
        : std::numeric_limits<size_t>::max();
    const bool reciprocal = neighbor_in_collection &&
        RoomSectorHasReciprocalPortal(neighbor, sector);
    AppendNativeLog(
        "camera_room_portal sector=%llu index=%d result=%s flags=%08X "
        "traversable=%u trace_skip=%u neighbor=%08llX/%llu reciprocal=%u "
        "point=%d/%d/%d normal=%d/%d/%d",
        static_cast<unsigned long long>(sector_index), index,
        portal_valid && neighbor_in_collection ? "OK" : "INVALID", flags,
        (flags & 0x08u) != 0u ? 1u : 0u,
        (flags & 0x20u) != 0u ? 1u : 0u,
        static_cast<unsigned long long>(neighbor),
        static_cast<unsigned long long>(neighbor_index),
        reciprocal ? 1u : 0u, point[0], point[1], point[2], normal[0],
        normal[1], normal[2]);
  }
}

bool ReadRuntimeRoomCollection(uintptr_t* collection, int32_t* sector_count,
                               uintptr_t* sector_base) {
  if (!g_dungeon_base || !collection || !sector_count || !sector_base) {
    return false;
  }
  uintptr_t manager = 0;
  return SafeReadValue(g_dungeon_base + kRetailRoomManagerPointerRva,
                       &manager) &&
         manager &&
         SafeReadValue(reinterpret_cast<const void*>(manager + 0x20u),
                       collection) &&
         *collection &&
         SafeReadValue(reinterpret_cast<const void*>(*collection),
                       sector_count) &&
         *sector_count > 0 && *sector_count <= 8192 &&
         SafeReadValue(reinterpret_cast<const void*>(*collection + 0x04u),
                       sector_base) &&
         *sector_base;
}

bool ReadRuntimeRoomPlane(uintptr_t address,
                          deathtrap_camera::RoomPlane* plane,
                          double* normal_length = nullptr) {
  if (!address || !plane) {
    return false;
  }
  std::array<int32_t, 3> point{};
  std::array<int32_t, 3> normal{};
  if (!SafeRead(reinterpret_cast<const void*>(address), point.data(),
                sizeof(point)) ||
      !SafeRead(reinterpret_cast<const void*>(address + 0x0Cu), normal.data(),
                sizeof(normal))) {
    return false;
  }
  const double length = std::sqrt(
      static_cast<double>(normal[0]) * normal[0] +
      static_cast<double>(normal[1]) * normal[1] +
      static_cast<double>(normal[2]) * normal[2]);
  if (!std::isfinite(length) || length < 1024.0 || length > 32768.0) {
    return false;
  }
  plane->point = {static_cast<double>(point[0]),
                  static_cast<double>(point[1]),
                  static_cast<double>(point[2])};
  plane->inward_normal = {
      static_cast<double>(normal[0]) / length,
      static_cast<double>(normal[1]) / length,
      static_cast<double>(normal[2]) / length};
  if (normal_length) {
    *normal_length = length;
  }
  return true;
}

bool RefreshRuntimeRoomPortalFlags() {
  RuntimeRoomGraphCache& cache = g_runtime_room_graph;
  if (!cache.valid || cache.portal_addresses.size() != cache.sectors.size()) {
    return false;
  }
  for (size_t sector = 0; sector < cache.sectors.size(); ++sector) {
    if (cache.portal_addresses[sector].size() !=
        cache.sectors[sector].portals.size()) {
      return false;
    }
    for (size_t portal = 0;
         portal < cache.portal_addresses[sector].size(); ++portal) {
      uint32_t flags = 0;
      if (!SafeReadValue(reinterpret_cast<const void*>(
                             cache.portal_addresses[sector][portal] + 0x04u),
                         &flags)) {
        return false;
      }
      cache.sectors[sector].portals[portal].open = (flags & 0x08u) != 0u;
    }
  }
  return true;
}

bool BuildRuntimeRoomGraph() {
  uintptr_t collection = 0;
  int32_t sector_count = 0;
  uintptr_t sector_base = 0;
  if (!ReadRuntimeRoomCollection(&collection, &sector_count, &sector_base)) {
    g_runtime_room_graph = {};
    return false;
  }

  RuntimeRoomGraphCache& cache = g_runtime_room_graph;
  if (cache.attempted && cache.collection == collection &&
      cache.sector_base == sector_base && cache.sector_count == sector_count) {
    return cache.valid && RefreshRuntimeRoomPortalFlags();
  }

  RuntimeRoomGraphCache rebuilt;
  rebuilt.collection = collection;
  rebuilt.sector_base = sector_base;
  rebuilt.sector_count = sector_count;
  rebuilt.attempted = true;
  rebuilt.sectors.resize(static_cast<size_t>(sector_count));
  rebuilt.portal_addresses.resize(static_cast<size_t>(sector_count));
  size_t plane_total = 0;
  size_t portal_total = 0;
  double minimum_normal_length = std::numeric_limits<double>::infinity();
  double maximum_normal_length = 0.0;

  for (int32_t index = 0; index < sector_count; ++index) {
    const uintptr_t sector_address =
        sector_base + static_cast<uintptr_t>(index) * 0x3Cu;
    int32_t solid_count = 0;
    uintptr_t planes = 0;
    int32_t portal_count = 0;
    uintptr_t portals = 0;
    if (!SafeReadValue(reinterpret_cast<const void*>(sector_address + 0x14u),
                       &solid_count) ||
        !SafeReadValue(reinterpret_cast<const void*>(sector_address + 0x1Cu),
                       &planes) ||
        !SafeReadValue(reinterpret_cast<const void*>(sector_address + 0x20u),
                       &portal_count) ||
        !SafeReadValue(reinterpret_cast<const void*>(sector_address + 0x24u),
                       &portals) ||
        solid_count < 0 || solid_count > 128 || portal_count < 0 ||
        portal_count > 128 || (solid_count != 0 && !planes) ||
        (portal_count != 0 && !portals)) {
      AppendNativeLog(
          "camera_owned_room_graph result=SECTOR_HEADER_INVALID index=%d",
          index);
      cache = std::move(rebuilt);
      return false;
    }

    deathtrap_camera::RoomSector& destination =
        rebuilt.sectors[static_cast<size_t>(index)];
    destination.key = sector_address;
    destination.solid_planes.reserve(static_cast<size_t>(solid_count));
    for (int32_t plane_index = 0; plane_index < solid_count; ++plane_index) {
      deathtrap_camera::RoomPlane plane;
      double normal_length = 0.0;
      if (!ReadRuntimeRoomPlane(
              planes + static_cast<uintptr_t>(plane_index) * 0x34u,
              &plane, &normal_length)) {
        AppendNativeLog(
            "camera_owned_room_graph result=PLANE_INVALID sector=%d plane=%d",
            index, plane_index);
        cache = std::move(rebuilt);
        return false;
      }
      destination.solid_planes.push_back(plane);
      minimum_normal_length = std::min(minimum_normal_length, normal_length);
      maximum_normal_length = std::max(maximum_normal_length, normal_length);
      ++plane_total;
    }

    destination.portals.reserve(static_cast<size_t>(portal_count));
    rebuilt.portal_addresses[static_cast<size_t>(index)].reserve(
        static_cast<size_t>(portal_count));
    for (int32_t portal_index = 0; portal_index < portal_count;
         ++portal_index) {
      const uintptr_t portal_address =
          portals + static_cast<uintptr_t>(portal_index) * 0x44u;
      uint32_t flags = 0;
      uintptr_t neighbor = 0;
      deathtrap_camera::RoomPlane plane;
      if (!SafeReadValue(reinterpret_cast<const void*>(portal_address + 0x04u),
                         &flags) ||
          !ReadRoomNeighbor(portal_address, &neighbor) ||
          neighbor < sector_base ||
          neighbor >= sector_base +
              static_cast<uintptr_t>(sector_count) * 0x3Cu ||
          ((neighbor - sector_base) % 0x3Cu) != 0u ||
          !ReadRuntimeRoomPlane(portal_address + 0x10u, &plane)) {
        AppendNativeLog(
            "camera_owned_room_graph result=PORTAL_INVALID sector=%d "
            "portal=%d",
            index, portal_index);
        cache = std::move(rebuilt);
        return false;
      }
      deathtrap_camera::RoomPortal portal;
      portal.plane = plane;
      portal.neighbor = (neighbor - sector_base) / 0x3Cu;
      portal.open = (flags & 0x08u) != 0u;
      portal.key = portal_address;
      destination.portals.push_back(portal);
      rebuilt.portal_addresses[static_cast<size_t>(index)].push_back(
          portal_address);
      ++portal_total;
    }
  }

  rebuilt.valid = true;
  cache = std::move(rebuilt);
  AppendNativeLog(
      "camera_owned_room_graph result=READY sectors=%d planes=%llu "
      "portals=%llu normal=%.1f..%.1f",
      sector_count, static_cast<unsigned long long>(plane_total),
      static_cast<unsigned long long>(portal_total), minimum_normal_length,
      maximum_normal_length);
  return true;
}

uintptr_t ResolveOwnedCameraStartSector(
    const std::array<int32_t, 3>& focus, uintptr_t seed_sector) {
  if (!g_resolve_camera_sector) {
    return 0;
  }
  uintptr_t start_sector = 0;
  __try {
    start_sector = g_resolve_camera_sector(focus.data(), seed_sector);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    start_sector = 0;
  }
  return start_sector;
}

bool SweepOwnedCameraAgainstRooms(
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& requested, uintptr_t seed_sector,
    deathtrap_camera::RoomSweepResult* sweep,
    std::array<int32_t, 3>* clipped,
    deathtrap_camera::RoomOrbitPlan* orbit_plan = nullptr,
    size_t previous_candidate_index = std::numeric_limits<size_t>::max(),
    size_t* resolved_start_index = nullptr) {
  if (!sweep || !clipped || !BuildRuntimeRoomGraph()) {
    return false;
  }
  const uintptr_t start_sector =
      ResolveOwnedCameraStartSector(focus, seed_sector);
  RuntimeRoomGraphCache& cache = g_runtime_room_graph;
  if (!start_sector || start_sector < cache.sector_base ||
      start_sector >= cache.sector_base +
          static_cast<uintptr_t>(cache.sector_count) * 0x3Cu ||
      ((start_sector - cache.sector_base) % 0x3Cu) != 0u) {
    return false;
  }
  const size_t start_index = (start_sector - cache.sector_base) / 0x3Cu;
  if (resolved_start_index) {
    *resolved_start_index = start_index;
  }
  const deathtrap_camera::RoomVec3 room_focus{
      static_cast<double>(focus[0]), static_cast<double>(focus[1]),
      static_cast<double>(focus[2])};
  const deathtrap_camera::RoomVec3 room_requested{
      static_cast<double>(requested[0]), static_cast<double>(requested[1]),
      static_cast<double>(requested[2])};
  if (orbit_plan) {
    static const std::vector<deathtrap_camera::RoomOrbitCandidateOffset>
        kCandidateOffsets = {
            {0.0, 0.0},
        };
    *orbit_plan = deathtrap_camera::SelectRoomOrbitPlan(
        cache.sectors, start_index, room_focus, room_requested,
        kCameraCollisionSphereRadius, g_third_person_orbit_min_radius,
        kCandidateOffsets, previous_candidate_index,
        kCameraCollisionSphereRadius, 8.0, 32u, false);
    if (!orbit_plan->valid || orbit_plan->candidates.empty()) {
      return false;
    }
    *sweep = orbit_plan->candidates[0].sweep;
  } else {
    *sweep = deathtrap_camera::SweepSphereThroughRooms(
        cache.sectors, start_index, room_focus, room_requested,
        kCameraCollisionSphereRadius, 8.0, 32u);
  }
  if (!sweep->valid) {
    return false;
  }
  *clipped = {
      static_cast<int32_t>(std::lround(sweep->position.x)),
      static_cast<int32_t>(std::lround(sweep->position.y)),
      static_cast<int32_t>(std::lround(sweep->position.z))};
  return true;
}

bool DeathtrapGameplayReady(bool require_selector_closed) {
  if (!g_dungeon_base) {
    return false;
  }

  uint8_t selector_mode = 0;
  uintptr_t player = 0;
  uintptr_t game_root = 0;
  uintptr_t gameplay_context = 0;
  uintptr_t gameplay_object = 0;
  uintptr_t root_vtable = 0;
  if (!SafeReadValue(g_dungeon_base + kUiSelectorModeRva, &selector_mode) ||
      (require_selector_closed && selector_mode != 0) ||
      !SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player) ||
      !player ||
      !SafeReadValue(g_dungeon_base + kGameRootPointerRva, &game_root) ||
      !game_root ||
      !SafeReadValue(reinterpret_cast<const void*>(game_root), &root_vtable) ||
      root_vtable == reinterpret_cast<uintptr_t>(g_dungeon_base) +
                         0x00083F20u ||
      root_vtable == reinterpret_cast<uintptr_t>(g_dungeon_base) +
                         0x0004EBF0u ||
      !SafeReadValue(reinterpret_cast<const void*>(game_root + 0x2Cu),
                     &gameplay_context) ||
      !gameplay_context ||
      !SafeReadValue(
          reinterpret_cast<const void*>(gameplay_context + 0x1030u),
          &gameplay_object) ||
      !gameplay_object) {
    return false;
  }
  return true;
}

bool WeaponWheelGameplayReady() {
  return g_weapon_wheel_enabled && DeathtrapGameplayReady(true);
}

bool NativeWeaponAvailable(int32_t weapon_id) {
  if (weapon_id == 0 || weapon_id == 7) {
    return true;
  }
  if (weapon_id < 1 || weapon_id > 6) {
    return false;
  }
  using InventoryLookupFn = void*(__cdecl*)(int32_t);
  void* item = nullptr;
  __try {
    item = reinterpret_cast<InventoryLookupFn>(
        g_dungeon_base + kInventoryLookupRva)(weapon_id);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    item = nullptr;
  }
  return item != nullptr;
}

bool SelectNativeWeapon(int32_t action_code) {
  using SelectWeaponFn = void(__cdecl*)(int32_t, int32_t);
  __try {
    reinterpret_cast<SelectWeaponFn>(
        g_dungeon_base + kSelectCloseCombatWeaponRva)(action_code, 1);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void ConsumePendingWeaponWheel() {
  int32_t queued =
      g_pending_weapon_wheel_detents.load(std::memory_order_acquire);
  if (queued == 0) {
    return;
  }

  // Never carry a wheel action out of a menu or loading screen. DirectInput
  // observes the wheel independently, so stale menu scrolling must not equip
  // a weapon after gameplay resumes.
  const uint64_t age_ms =
      GetTickCount64() -
      g_last_weapon_wheel_event_ms.load(std::memory_order_relaxed);
  if (age_ms > 500u || !WeaponWheelGameplayReady()) {
    g_pending_weapon_wheel_detents.store(0, std::memory_order_release);
    ++g_weapon_wheel_rejections;
    return;
  }

  queued = g_pending_weapon_wheel_detents.exchange(
      0, std::memory_order_acq_rel);
  int32_t current = -1;
  if (!SafeReadValue(g_dungeon_base + kActiveCloseCombatWeaponRva,
                     &current) ||
      current < 0 || current > 7) {
    ++g_weapon_wheel_rejections;
    return;
  }

  // The retail close-combat selector maps internal IDs to non-contiguous
  // action codes. ID 0 and the special ID 7 are always present; IDs 1..6 use
  // the same inventory-presence test as the original selector.
  constexpr std::array<int32_t, 8> kActionCodeByWeaponId = {
      1, 2, 4, 5, 6, 7, 8, 0};
  int direction = queued > 0 ? -1 : 1;
  if (g_weapon_wheel_invert) {
    direction = -direction;
  }
  for (int distance = 1; distance <= 8; ++distance) {
    const int32_t candidate =
        (current + direction * distance + 64) % 8;
    if (!NativeWeaponAvailable(candidate)) {
      continue;
    }
    if (SelectNativeWeapon(kActionCodeByWeaponId[candidate])) {
      ++g_weapon_wheel_switches;
      AppendNativeLog(
          "weapon_wheel current=%d selected=%d action=%d direction=%d "
          "queued=%d switches=%llu rejections=%llu",
          current, candidate, kActionCodeByWeaponId[candidate], direction,
          queued, static_cast<unsigned long long>(g_weapon_wheel_switches),
          static_cast<unsigned long long>(g_weapon_wheel_rejections));
    } else {
      ++g_weapon_wheel_rejections;
    }
    return;
  }
  ++g_weapon_wheel_rejections;
}

using DynamicXInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using DynamicXInputSetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

struct ControllerSelectorState {
  bool direction_down = false;
  bool row_open = false;
  bool cancelled = false;
  bool selection_confirmed = false;
  uint32_t category = 0;
  uint32_t slot = 0;
  uint64_t pressed_ms = 0;
  std::array<uint32_t, 4> remembered_slot{};
};

enum class InjectedKey : size_t {
  kW,
  kS,
  kA,
  kD,
  kJ,
  kK,
  kShift,
  kSpace,
  kE,
  kQ,
  kC,
  kTab,
  kEscape,
  kUp,
  kDown,
  kLeft,
  kRight,
  kEnter,
  kCount,
};

HMODULE g_xinput_module = nullptr;
DynamicXInputGetStateFn g_xinput_get_state = nullptr;
DynamicXInputSetStateFn g_xinput_set_state = nullptr;
ControllerSelectorState g_controller_selector;
std::array<bool, static_cast<size_t>(InjectedKey::kCount)>
    g_injected_keys{};
bool g_injected_mouse_left = false;
bool g_injected_mouse_right = false;
bool g_xinput_was_connected = false;
std::atomic<bool> g_xinput_controller_present{false};
std::atomic<int32_t> g_support_last_controller_connected{-1};
WORD g_previous_xinput_buttons = 0;
std::atomic<bool> g_xinput_first_person_toggled{false};
std::atomic<bool> g_xinput_menu_mode{true};
std::atomic<bool> g_xinput_camera_relative_intent{false};
std::atomic<int32_t> g_xinput_desired_heading{0};
std::atomic<int32_t> g_xinput_movement_magnitude_milli{0};
std::atomic<uintptr_t> g_xinput_movement_controller{0};
std::atomic<uint64_t> g_xinput_camera_relative_started_ms{0};
std::atomic<uint64_t> g_xinput_last_locomotion_steer_ms{0};
std::atomic<bool> g_player_turn_hook_installed{false};
std::atomic<bool> g_player_state_dispatcher_hook_installed{false};
std::atomic<bool> g_immersive_root_motion_hooks_installed{false};
uint64_t g_immersive_root_motion_routes = 0;
uint64_t g_immersive_root_motion_rejections = 0;
thread_local bool g_immersive_locomotion_transaction_active = false;
thread_local ImmersiveLocomotionPlan g_immersive_locomotion_transaction_plan{};
std::atomic<bool> g_native_joystick_hooks_installed{false};
std::atomic<int32_t> g_xinput_native_joystick_x_milli{0};
std::atomic<int32_t> g_xinput_native_joystick_y_milli{0};
std::atomic<bool> g_xinput_native_joystick_active{false};
std::atomic<bool> g_third_person_heading_reference_valid{false};
std::atomic<int32_t> g_third_person_heading_reference_microradians{0};
bool g_xinput_previous_native_gameplay = false;
bool g_xinput_vibration_enabled = true;
uint32_t g_xinput_vibration_strength_percent = 100u;
uint32_t g_xinput_melee_swing_vibration_ms = 170u;
uint32_t g_xinput_block_vibration_ms = 60u;
uint32_t g_xinput_successful_block_vibration_ms = 210u;
uint32_t g_xinput_spell_cast_vibration_ms = 260u;
uint32_t g_xinput_ranged_shot_vibration_ms = 115u;
uint32_t g_xinput_healing_vibration_ms = 320u;
uint32_t g_xinput_selector_tick_vibration_ms = 38u;
uint32_t g_xinput_landing_vibration_ms = 145u;
uint32_t g_xinput_heavy_damage_vibration_ms = 380u;
uint32_t g_xinput_heavy_damage_threshold_hp = 12u;
uint32_t g_xinput_hit_vibration_ms = 150u;
uint32_t g_xinput_damage_vibration_ms = 240u;
uint32_t g_xinput_death_vibration_ms = 700u;
BYTE g_previous_xinput_left_trigger = 0;
WORD g_applied_vibration_left = 0;
WORD g_applied_vibration_right = 0;
uint64_t g_block_vibration_until_ms = 0;
std::atomic<uint64_t> g_melee_swing_vibration_until_ms{0};
std::atomic<uint64_t> g_successful_block_vibration_until_ms{0};
std::atomic<uint64_t> g_spell_cast_vibration_until_ms{0};
std::atomic<bool> g_successful_block_vibration_pending{false};
std::atomic<bool> g_spell_cast_vibration_pending{false};
std::atomic<bool> g_ranged_shot_vibration_pending{false};
std::atomic<bool> g_healing_vibration_pending{false};
std::atomic<bool> g_selector_tick_vibration_pending{false};
std::atomic<bool> g_landing_vibration_pending{false};
std::atomic<bool> g_player_melee_attack_window_active{false};
std::atomic<uint64_t> g_last_controller_attack_ms{0};
std::atomic<uint64_t> g_hit_vibration_until_ms{0};
std::atomic<uint64_t> g_damage_vibration_until_ms{0};
std::atomic<uint64_t> g_death_vibration_until_ms{0};
std::atomic<uint64_t> g_ranged_shot_vibration_until_ms{0};
std::atomic<uint64_t> g_healing_vibration_until_ms{0};
std::atomic<uint64_t> g_selector_tick_vibration_until_ms{0};
std::atomic<uint64_t> g_landing_vibration_until_ms{0};
std::atomic<uint64_t> g_heavy_damage_vibration_until_ms{0};
std::atomic<uint32_t> g_hit_vibration_percent{0};
std::atomic<uint32_t> g_damage_vibration_percent{0};
std::atomic<uint32_t> g_landing_vibration_percent{0};
bool g_landing_observer_valid = false;
bool g_landing_observer_airborne = false;
uint32_t g_landing_observer_airborne_ticks = 0;
int32_t g_landing_observer_previous_y = 0;
int32_t g_landing_observer_peak_vertical_delta = 0;
std::atomic_flag g_xinput_poll_guard = ATOMIC_FLAG_INIT;
bool g_xinput_camera_relative_was_active = false;
bool g_xinput_camera_relative_runtime_failed = false;
bool g_xinput_direct_heading_steering_was_active = false;
uint64_t g_xinput_camera_relative_turn_calls = 0;
uint64_t g_xinput_camera_relative_rejections = 0;

class ScopedXInputPoll {
 public:
  explicit ScopedXInputPoll(std::atomic_flag& flag) : flag_(flag) {
    acquired_ = !flag_.test_and_set(std::memory_order_acquire);
  }
  ~ScopedXInputPoll() {
    if (acquired_) {
      flag_.clear(std::memory_order_release);
    }
  }
  explicit operator bool() const { return acquired_; }

 private:
  std::atomic_flag& flag_;
  bool acquired_ = false;
};

constexpr std::array<WORD, static_cast<size_t>(InjectedKey::kCount)>
    kInjectedVirtualKeys = {L'W', L'S', L'A', L'D', L'J', L'K', VK_LSHIFT,
                            VK_SPACE, L'E', L'Q', L'C', VK_TAB, VK_ESCAPE,
                            VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN};

bool IsGameForeground() {
  const HWND foreground = GetForegroundWindow();
  if (!foreground) {
    return false;
  }
  DWORD process_id = 0;
  GetWindowThreadProcessId(foreground, &process_id);
  return process_id == GetCurrentProcessId();
}

void InjectVirtualKey(InjectedKey key, bool down) {
  const size_t index = static_cast<size_t>(key);
  if (g_injected_keys[index] == down) {
    return;
  }
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = kInjectedVirtualKeys[index];
  input.ki.dwFlags = down ? 0u : KEYEVENTF_KEYUP;
  if (key == InjectedKey::kUp || key == InjectedKey::kDown ||
      key == InjectedKey::kLeft || key == InjectedKey::kRight) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  if (SendInput(1, &input, sizeof(input)) == 1) {
    g_injected_keys[index] = down;
  }
}

void InjectMouseLeft(bool down) {
  if (g_injected_mouse_left == down) {
    return;
  }
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
  if (SendInput(1, &input, sizeof(input)) == 1) {
    g_injected_mouse_left = down;
  }
}

void InjectMouseRight(bool down) {
  if (g_injected_mouse_right == down) {
    return;
  }
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
  if (SendInput(1, &input, sizeof(input)) == 1) {
    g_injected_mouse_right = down;
  }
}

void InjectRelativeMouseMove(double normalized_x, double normalized_y,
                             int32_t pixels_per_tick) {
  const LONG movement_x = static_cast<LONG>(std::lround(
      normalized_x * static_cast<double>(pixels_per_tick)));
  const double y_sign = g_xinput_invert_right_y ? 1.0 : -1.0;
  const LONG movement_y = static_cast<LONG>(std::lround(
      normalized_y * y_sign * static_cast<double>(pixels_per_tick)));
  if (movement_x == 0 && movement_y == 0) {
    return;
  }
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dx = movement_x;
  input.mi.dy = movement_y;
  input.mi.dwFlags = MOUSEEVENTF_MOVE;
  SendInput(1, &input, sizeof(input));
}

bool ReadLivePlayerHeading(int32_t* heading, uintptr_t* live_controller);

double CurvedStick(double value, double exponent) {
  if (value == 0.0) {
    return 0.0;
  }
  return std::copysign(std::pow(std::abs(value), exponent), value);
}

void SetCustomHeadViewSelected(bool enabled, const char* source) {
  if (enabled && !g_immersive_first_person_enabled) {
    return;
  }
  const CustomCameraViewMode previous = CurrentCustomCameraViewMode();
  const CustomCameraViewMode next =
      enabled ? CustomCameraViewMode::kHead
              : CustomCameraViewMode::kModernThirdPerson;
  if (previous == next) {
    return;
  }
  g_custom_camera_view_mode.store(static_cast<uint32_t>(next),
                                  std::memory_order_release);
  if (!enabled) {
    if (previous == CustomCameraViewMode::kHead) {
      const double immersive_yaw = g_third_person_orbit_state.yaw;
      g_third_person_orbit_state.yaw =
          ThirdPersonOrbitYawFromImmersiveYaw(immersive_yaw);
      g_third_person_heading_reference_microradians.store(
          static_cast<int32_t>(std::lround(
              g_third_person_orbit_state.yaw * 1000000.0)),
          std::memory_order_release);
      g_third_person_heading_reference_valid.store(
          true, std::memory_order_release);
      AppendNativeLog(
          "camera_immersive_exit_yaw head=%.2f orbit=%.2f",
          immersive_yaw * 180.0 / kOrbitPi,
          g_third_person_orbit_state.yaw * 180.0 / kOrbitPi);
    }
    // Immersive view aligns the rendered body with the eye yaw through the
    // shared camera-relative heading writer, including during mouse/keyboard
    // play.  That ownership is independent of DirectInput's fresh key state,
    // so it must be released explicitly when F10/SELECT/R3 leaves the view.
    // Otherwise the stale target continues fighting native A/D steering and
    // makes keyboard movement appear locked to one direction.
    g_xinput_camera_relative_intent.store(false,
                                           std::memory_order_release);
    g_xinput_movement_magnitude_milli.store(0,
                                             std::memory_order_release);
    g_xinput_movement_controller.store(0, std::memory_order_release);
    g_xinput_camera_relative_started_ms.store(0,
                                               std::memory_order_release);
    g_immersive_keyboard_movement_x.store(0, std::memory_order_release);
    g_immersive_keyboard_movement_y.store(0, std::memory_order_release);
    g_immersive_xinput_movement_x_milli.store(
        0, std::memory_order_release);
    g_immersive_xinput_movement_y_milli.store(
        0, std::memory_order_release);
  }
  if (enabled) {
    // The overlay-owned view never borrows retail mode 4. Release a controller
    // Tab request first so R3/Tab remains an independent native camera.
    g_xinput_first_person_toggled.store(false, std::memory_order_release);

    // Third-person orbit yaw describes the radial direction from the focus to
    // the camera.  A head view must instead begin along the actor's visible
    // forward course.  Keeping an arbitrary front/side third-person orbit here
    // made F10 appear to turn the player by up to 180 degrees and forced the
    // user to rotate back manually before moving.  Convert the live Q10 body
    // heading to the equivalent behind-the-player radial yaw; subsequent head
    // input and a later return to third person then remain continuous.
    int32_t body_heading = 0;
    uintptr_t body_controller = 0;
    if (ReadLivePlayerHeading(&body_heading, &body_controller) &&
        body_controller) {
      g_third_person_orbit_state.yaw =
          ImmersiveOrbitYawFromPlayerHeading(
              body_heading, kPlayerHeadingUnitsPerTurn);
      g_third_person_orbit_state.pitch = 0.0;
      g_third_person_heading_reference_microradians.store(
          static_cast<int32_t>(std::lround(
              g_third_person_orbit_state.yaw * 1000000.0)),
          std::memory_order_release);
      g_third_person_heading_reference_valid.store(
          true, std::memory_order_release);
      AppendNativeLog(
          "camera_immersive_heading_seed body=%d yaw=%.2f pitch=%.2f "
          "controller=%p",
          body_heading,
          g_third_person_orbit_state.yaw * 180.0 / kOrbitPi,
          g_third_person_orbit_state.pitch * 180.0 / kOrbitPi,
          reinterpret_cast<void*>(body_controller));
    }
  }
  g_third_person_orbit_state.filtered_input_x = 0.0;
  g_third_person_orbit_state.filtered_input_y = 0.0;
  g_owned_camera_presentation_cut_pending.store(true,
                                                 std::memory_order_release);
  AppendNativeLog("camera_view select=%s source=%s previous=%s",
                  CustomCameraViewModeName(next),
                  source ? source : "UNKNOWN",
                  CustomCameraViewModeName(previous));
}

void ToggleCustomHeadView(const char* source) {
  SetCustomHeadViewSelected(!CustomHeadViewSelected(), source);
}

void PublishThirdPersonOrbitInput(double right_x, double right_y,
                                  bool active) {
  constexpr double kInputScale = 1000000.0;
  const CameraOrbitStickInput locked = CameraOrbitAxisLock(
      right_x, right_y, g_xinput_right_stick_axis_lock_ratio);
  const double curved_x = CurvedStick(locked.x, g_xinput_right_stick_curve);
  const double curved_y = CurvedStick(locked.y, g_xinput_right_stick_curve);
  g_third_person_orbit_input_x.store(
      static_cast<int32_t>(std::lround(curved_x * kInputScale)),
      std::memory_order_relaxed);
  g_third_person_orbit_input_y.store(
      static_cast<int32_t>(std::lround(curved_y * kInputScale)),
      std::memory_order_relaxed);
  g_third_person_orbit_input_active.store(active,
                                           std::memory_order_release);
  g_third_person_orbit_input_sequence.fetch_add(1,
                                                 std::memory_order_release);
}

bool ReadCameraPlayerPosition(void* controller,
                              std::array<int32_t, 3>* position) {
  if (!controller || !position) {
    return false;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  const std::array<size_t, 3> offsets = {
      kCameraControllerPlayerXPointerOffset,
      kCameraControllerPlayerYPointerOffset,
      kCameraControllerPlayerZPointerOffset};
  for (size_t axis = 0; axis < offsets.size(); ++axis) {
    uintptr_t coordinate = 0;
    if (!SafeReadValue(reinterpret_cast<const void*>(base + offsets[axis]),
                       &coordinate) ||
        !coordinate ||
        !SafeReadValue(reinterpret_cast<const void*>(coordinate),
                       &(*position)[axis])) {
      return false;
    }
  }
  return true;
}

bool ReadCameraFocusPosition(void* controller,
                             std::array<int32_t, 3>* position) {
  if (!controller || !position) {
    return false;
  }
  return SafeRead(reinterpret_cast<const uint8_t*>(controller) +
                      kCameraControllerFocusOffset,
                  position->data(), sizeof(*position));
}

bool NativeCameraVolumeBlocked(void* controller,
                               const std::array<int32_t, 3>& focus,
                               const std::array<int32_t, 3>& endpoint,
                               bool* blocked) {
  if (!controller || !blocked || !g_resolve_camera_sector ||
      !g_camera_volume_visible) {
    return false;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  uintptr_t focus_sector_holder = 0;
  uintptr_t focus_seed_sector = 0;
  uintptr_t endpoint_seed_sector = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerRoomPointerOffset),
                     &focus_sector_holder) ||
      !focus_sector_holder ||
      !SafeReadValue(reinterpret_cast<const void*>(focus_sector_holder),
                     &focus_seed_sector) ||
      !focus_seed_sector ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerEndpointSectorOffset),
                     &endpoint_seed_sector) ||
      !endpoint_seed_sector) {
    return false;
  }

  __try {
    const uintptr_t focus_sector =
        g_resolve_camera_sector(focus.data(), focus_seed_sector);
    uintptr_t endpoint_sector =
        g_resolve_camera_sector(endpoint.data(), endpoint_seed_sector);
    // controller+0x200 is the retail endpoint's cached sector. Our orbit may
    // point into a different adjacent room, so retry from the freshly-resolved
    // focus sector before treating the native query as unavailable.
    if (!endpoint_sector) {
      endpoint_sector =
          g_resolve_camera_sector(endpoint.data(), focus_sector);
    }
    if (!focus_sector || !endpoint_sector) {
      return false;
    }
    // 0x30910 is a visibility/traversability predicate, not a collision
    // predicate. Its non-zero result is the clear path used by retail
    // 0x2F6D0; zero sends mode 3 to the 0x2F750 fallback-camera search.
    *blocked = g_camera_volume_visible(
                   endpoint.data(), endpoint_sector,
                   focus.data(), focus_sector) == 0;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool NativeCameraVolumeFullyClear(
    void* controller, const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& endpoint, bool* fully_clear) {
  if (!controller || !fully_clear || !g_resolve_camera_sector ||
      !g_camera_room_trace_visible) {
    return false;
  }

  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  uintptr_t focus_sector_holder = 0;
  uintptr_t focus_seed_sector = 0;
  uintptr_t endpoint_seed_sector = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerRoomPointerOffset),
                     &focus_sector_holder) ||
      !focus_sector_holder ||
      !SafeReadValue(reinterpret_cast<const void*>(focus_sector_holder),
                     &focus_seed_sector) ||
      !focus_seed_sector ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerEndpointSectorOffset),
                     &endpoint_seed_sector) ||
      !endpoint_seed_sector) {
    return false;
  }

  // Preserve the exact centre-plus-six focus samples used by 0x30910, but do
  // not confuse them with the camera's footprint.  They establish that the
  // complete authored focus volume is reachable from the endpoint.
  constexpr int32_t kNativeCameraTraceOffset = 30;
  constexpr std::array<std::array<int32_t, 3>, 7> kFocusTraceOffsets = {{
      {0, 0, 0},
      {kNativeCameraTraceOffset, 0, 0},
      {-kNativeCameraTraceOffset, 0, 0},
      {0, kNativeCameraTraceOffset, 0},
      {0, -kNativeCameraTraceOffset, 0},
      {0, 0, kNativeCameraTraceOffset},
      {0, 0, -kNativeCameraTraceOffset},
  }};

  // Scene-mesh collision treats the camera as a 96-unit sphere.  The native
  // seven-ray query offsets only the focus by 30 units, so it can declare an
  // endpoint clear while the camera centre's own sphere already straddles a
  // wall in a narrow room.  Require centre plus the six cardinal points of
  // that endpoint sphere to live in sectors with a clear route to the focus.
  constexpr int32_t kEndpointFootprintRadius =
      static_cast<int32_t>(kCameraCollisionSphereRadius);
  constexpr std::array<std::array<int32_t, 3>, 7> kEndpointOffsets = {{
      {0, 0, 0},
      {kEndpointFootprintRadius, 0, 0},
      {-kEndpointFootprintRadius, 0, 0},
      {0, kEndpointFootprintRadius, 0},
      {0, -kEndpointFootprintRadius, 0},
      {0, 0, kEndpointFootprintRadius},
      {0, 0, -kEndpointFootprintRadius},
  }};

  __try {
    uintptr_t endpoint_sector =
        g_resolve_camera_sector(endpoint.data(), endpoint_seed_sector);
    const uintptr_t resolved_focus_sector =
        g_resolve_camera_sector(focus.data(), focus_seed_sector);
    if (!resolved_focus_sector) {
      return false;
    }
    if (!endpoint_sector) {
      endpoint_sector =
          g_resolve_camera_sector(endpoint.data(), resolved_focus_sector);
    }
    if (!endpoint_sector) {
      return false;
    }

    for (const auto& offset : kFocusTraceOffsets) {
      const std::array<int32_t, 3> focus_sample = {
          focus[0] + offset[0], focus[1] + offset[1],
          focus[2] + offset[2]};
      const uintptr_t focus_sample_sector = g_resolve_camera_sector(
          focus_sample.data(), resolved_focus_sector);
      if (!focus_sample_sector) {
        return false;
      }
      if (g_camera_room_trace_visible(
              endpoint_sector, endpoint.data(), focus_sample_sector,
              focus_sample.data()) == 0) {
        *fully_clear = false;
        return true;
      }
    }

    for (const auto& offset : kEndpointOffsets) {
      const std::array<int32_t, 3> endpoint_sample = {
          endpoint[0] + offset[0], endpoint[1] + offset[1],
          endpoint[2] + offset[2]};
      uintptr_t endpoint_sample_sector = g_resolve_camera_sector(
          endpoint_sample.data(), endpoint_sector);
      if (!endpoint_sample_sector) {
        endpoint_sample_sector = g_resolve_camera_sector(
            endpoint_sample.data(), resolved_focus_sector);
      }
      if (!endpoint_sample_sector ||
          g_camera_room_trace_visible(
              endpoint_sample_sector, endpoint_sample.data(),
              resolved_focus_sector, focus.data()) == 0) {
        *fully_clear = false;
        return true;
      }
    }
    *fully_clear = true;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ModernCameraFootprintBlocked(
    void* controller, const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& endpoint, bool* blocked) {
  if (!blocked) {
    return false;
  }
  bool fully_clear = false;
  if (!NativeCameraVolumeFullyClear(controller, focus, endpoint,
                                    &fully_clear)) {
    return false;
  }
  *blocked = !fully_clear;
  return true;
}

bool CaptureRetailCameraProbeState(void* controller,
                                   RetailCameraProbeSnapshot* snapshot) {
  if (!controller || !snapshot) {
    return false;
  }
  snapshot->valid = SafeRead(
      reinterpret_cast<const uint8_t*>(controller) +
          kCameraProbeStateBeginOffset,
      snapshot->bytes.data(), snapshot->bytes.size());
  return snapshot->valid;
}

bool RestoreRetailCameraProbeState(
    void* controller, const RetailCameraProbeSnapshot& snapshot) {
  return controller && snapshot.valid &&
         SafeWrite(reinterpret_cast<uint8_t*>(controller) +
                       kCameraProbeStateBeginOffset,
                   snapshot.bytes.data(), snapshot.bytes.size());
}

bool ClipThirdPersonOrbitAgainstNativeWorld(
    void* controller, const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& requested,
    std::array<int32_t, 3>* clipped) {
  if (!clipped) {
    return false;
  }
  const double ray_x = static_cast<double>(requested[0] - focus[0]);
  const double ray_y = static_cast<double>(requested[1] - focus[1]);
  const double ray_z = static_cast<double>(requested[2] - focus[2]);
  const double requested_distance =
      std::hypot(std::hypot(ray_x, ray_z), ray_y);
  if (!std::isfinite(requested_distance) ||
      requested_distance < 1.0 ||
      requested_distance > 5000.0) {
    return false;
  }

  bool requested_blocked = false;
  if (!ModernCameraFootprintBlocked(controller, focus, requested,
                                    &requested_blocked) ||
      !requested_blocked) {
    return false;
  }

  const std::array<double, 3> direction = {
      ray_x / requested_distance, ray_y / requested_distance,
      ray_z / requested_distance};
  // Binary search requires a proven-clear lower endpoint. Normally the focus
  // itself provides it. Near a thin lever, portal seam or moving block,
  // however, the native camera footprint can report the focus sample blocked
  // for a few ticks even though a point farther along the same complete ray
  // is clear. Returning immediately in that case hands the frame back to the
  // retail fixed-camera candidate. Search the same ray for a verified clear
  // lower endpoint before giving up; every accepted sample still passes the
  // complete native volume predicate.
  bool focus_blocked = true;
  if (!ModernCameraFootprintBlocked(controller, focus, focus,
                                    &focus_blocked)) {
    return false;
  }
  double clear_distance = 0.0;
  if (focus_blocked) {
    constexpr uint32_t kPivotRecoverySamples = 16u;
    bool recovered_clear_sample = false;
    for (uint32_t sample = 1u; sample < kPivotRecoverySamples; ++sample) {
      const double sample_distance =
          requested_distance * static_cast<double>(sample) /
          static_cast<double>(kPivotRecoverySamples);
      const std::array<int32_t, 3> candidate = {
          focus[0] + static_cast<int32_t>(
                         std::lround(direction[0] * sample_distance)),
          focus[1] + static_cast<int32_t>(
                         std::lround(direction[1] * sample_distance)),
          focus[2] + static_cast<int32_t>(
                         std::lround(direction[2] * sample_distance))};
      bool sample_blocked = true;
      if (!ModernCameraFootprintBlocked(controller, focus, candidate,
                                        &sample_blocked)) {
        return false;
      }
      if (!sample_blocked) {
        clear_distance = sample_distance;
        recovered_clear_sample = true;
      }
    }
    if (recovered_clear_sample) {
      if (g_debug_log) {
        AppendNativeLog(
            "camera_native_volume pivot_recovered clear=%.1f "
            "requested=%.1f focus=%d/%d/%d",
            clear_distance, requested_distance,
            focus[0], focus[1], focus[2]);
      }
    } else {
      if (g_debug_log) {
        AppendNativeLog(
            "camera_native_volume pivot_not_clear focus=%d/%d/%d",
            focus[0], focus[1], focus[2]);
      }
      return false;
    }
  }

  // The native query is boolean. Search the same focus->endpoint segment for
  // the farthest clear camera centre. Retraction is immediate; outward
  // recovery is handled independently by the spring-arm state.
  double blocked_distance = requested_distance;
  for (uint32_t iteration = 0; iteration < 12u; ++iteration) {
    const double candidate_distance =
        (clear_distance + blocked_distance) * 0.5;
    const std::array<int32_t, 3> candidate = {
        focus[0] + static_cast<int32_t>(
                       std::lround(direction[0] * candidate_distance)),
        focus[1] + static_cast<int32_t>(
                       std::lround(direction[1] * candidate_distance)),
        focus[2] + static_cast<int32_t>(
                       std::lround(direction[2] * candidate_distance))};
    bool candidate_blocked = true;
    if (!ModernCameraFootprintBlocked(controller, focus, candidate,
                                      &candidate_blocked)) {
      return false;
    }
    if (candidate_blocked) {
      blocked_distance = candidate_distance;
    } else {
      clear_distance = candidate_distance;
    }
  }

  constexpr double kNativeContactBackoff = 12.0;
  double safe_distance = std::max(0.0, clear_distance -
                                           kNativeContactBackoff);
  auto position_at = [&](double distance) {
    return std::array<int32_t, 3>{
        focus[0] + static_cast<int32_t>(
                       std::lround(direction[0] * distance)),
        focus[1] + static_cast<int32_t>(
                       std::lround(direction[1] * distance)),
        focus[2] + static_cast<int32_t>(
                       std::lround(direction[2] * distance))};
  };
  // Integer rounding can move the final centre onto the blocked side of the
  // boundary. Never publish an endpoint that was not itself verified.
  std::array<int32_t, 3> safe = position_at(safe_distance);
  bool safe_blocked = true;
  for (uint32_t retry = 0; retry < 8u; ++retry) {
    if (!ModernCameraFootprintBlocked(controller, focus, safe,
                                      &safe_blocked)) {
      return false;
    }
    if (!safe_blocked) {
      break;
    }
    safe_distance = std::max(0.0, safe_distance - 4.0);
    safe = position_at(safe_distance);
  }
  if (safe_blocked) {
    return false;
  }
  *clipped = safe;
  if (g_debug_log) {
    AppendNativeLog(
        "camera_native_volume requested=%.1f clear=%.1f safe=%.1f "
        "focus=%d/%d/%d",
        requested_distance, clear_distance, safe_distance,
        focus[0], focus[1], focus[2]);
  }
  return true;
}

bool BeginMode3SourceTick(void* controller) {
  int32_t engine_frame = 0;
  if (!g_dungeon_base ||
      !SafeReadValue(g_dungeon_base + kEngineFrameCounterRva,
                     &engine_frame)) {
    // Failing open preserves the retail callback if the module is unloading
    // or the frame stamp is temporarily unavailable.
    g_mode3_source_tick.valid = false;
    return true;
  }
  if (g_mode3_source_tick.valid &&
      g_mode3_source_tick.controller == controller &&
      g_mode3_source_tick.engine_frame == engine_frame) {
    ++g_mode3_source_tick.duplicate_calls;
    return false;
  }
  g_mode3_source_tick.controller = controller;
  g_mode3_source_tick.engine_frame = engine_frame;
  g_mode3_source_tick.valid = true;
  return true;
}

double CameraPositionDistance(const std::array<int32_t, 3>& a,
                              const std::array<int32_t, 3>& b) {
  return std::hypot(
      std::hypot(static_cast<double>(a[0]) - static_cast<double>(b[0]),
                 static_cast<double>(a[2]) - static_cast<double>(b[2])),
      static_cast<double>(a[1]) - static_cast<double>(b[1]));
}

uintptr_t ResolveControllerCameraNode(void* controller) {
  if (!controller) {
    return 0;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  uintptr_t camera_owner = 0;
  uintptr_t camera_node = 0;
  return SafeReadValue(
             reinterpret_cast<const void*>(
                 base + kCameraControllerOwnerOffset),
             &camera_owner) &&
         camera_owner &&
         SafeReadValue(
             reinterpret_cast<const void*>(
                 camera_owner + kCameraNodeOffset),
             &camera_node)
      ? camera_node
      : 0;
}

bool ReadRetailCameraOwner(void* controller, uintptr_t* owner) {
  if (!controller || !owner) {
    return false;
  }
  *owner = 0;
  uint8_t flags = 0;
  return SafeReadValue(
             reinterpret_cast<const uint8_t*>(controller) +
                 kCameraControllerScriptOwnerOffset,
             owner) &&
         *owner &&
         SafeReadValue(reinterpret_cast<const uint8_t*>(*owner) + 8u,
                       &flags) &&
         (flags & kCameraScriptOwnerActiveMask) != 0;
}

bool RetailCameraOwnerAlreadyCompleted(uintptr_t owner) {
  if (!owner) {
    return false;
  }
  return std::find(
             g_completed_script_camera_owners.begin(),
             g_completed_script_camera_owners.begin() +
                 g_completed_script_camera_owner_count,
             owner) !=
      g_completed_script_camera_owners.begin() +
          g_completed_script_camera_owner_count;
}

void RememberCompletedRetailCameraOwner(uintptr_t owner) {
  if (!owner || RetailCameraOwnerAlreadyCompleted(owner)) {
    return;
  }
  if (g_completed_script_camera_owner_count <
      g_completed_script_camera_owners.size()) {
    g_completed_script_camera_owners[g_completed_script_camera_owner_count++] =
        owner;
    return;
  }
  std::rotate(g_completed_script_camera_owners.begin(),
              g_completed_script_camera_owners.begin() + 1,
              g_completed_script_camera_owners.end());
  g_completed_script_camera_owners.back() = owner;
}

void ClearRetailCameraTakeover(const char* reason, bool begin_cooldown) {
  if (g_retail_camera_arbitration.takeover_latched) {
    AppendNativeLog("camera_script takeover=OFF reason=%s", reason);
    RememberCompletedRetailCameraOwner(
        g_retail_camera_arbitration.takeover_owner);
  }
  const uint64_t cooldown = begin_cooldown ? GetTickCount64() + 1200u : 0u;
  g_retail_camera_arbitration = {};
  g_retail_camera_arbitration.cooldown_until_ms = cooldown;
  g_retail_camera_arbitration.last_interaction_sequence =
      g_controller_interaction_sequence.load(std::memory_order_acquire);
  g_retail_camera_arbitration.interaction_consumed = begin_cooldown;
}

bool EvaluateRetailCameraTakeover(
    void* controller, uintptr_t owner,
    const std::array<int32_t, 3>& before_original) {
  (void)before_original;
  std::array<int32_t, 3> player{};
  std::array<int32_t, 3> native_candidate{};
  if (!ReadCameraPlayerPosition(controller, &player) ||
      !SafeRead(reinterpret_cast<const uint8_t*>(controller) +
                    kCameraControllerResolvedPositionOffset,
                native_candidate.data(), sizeof(native_candidate))) {
    ClearRetailCameraTakeover("invalid_native_sample", false);
    return false;
  }

  RetailCameraArbitrationState& state = g_retail_camera_arbitration;
  uint32_t controller_flags = 0;
  const bool controller_flags_valid = SafeReadValue(
      reinterpret_cast<const uint8_t*>(controller) + 0x180u,
      &controller_flags);
  const bool scripted_view_active = controller_flags_valid &&
      (controller_flags & kCameraOwnedTargetConvergedMask) != 0u;
  const bool owner_changed = state.owner != owner;
  double player_motion = 0.0;
  if (state.previous_player_valid) {
    player_motion = CameraPositionDistance(player, state.previous_player);
  }
  const bool player_stationary = player_motion <= 24.0;

  double native_motion = 0.0;
  if (!owner_changed && state.previous_native_candidate_valid) {
    native_motion = CameraPositionDistance(
        native_candidate, state.previous_native_candidate);
  }

  const uint64_t now_ms = GetTickCount64();
  const uint64_t last_interaction_ms =
      g_last_controller_interaction_ms.load(std::memory_order_acquire);
  const uint64_t interaction_age_ms =
      last_interaction_ms && now_ms >= last_interaction_ms
          ? now_ms - last_interaction_ms
          : std::numeric_limits<uint64_t>::max();
  const bool recent_interaction = interaction_age_ms <= 6000u;
  const uint64_t interaction_sequence =
      g_controller_interaction_sequence.load(std::memory_order_acquire);
  if (interaction_sequence != state.last_interaction_sequence) {
    // Arm an owner-less reveal only from a quiet, already-observed retail
    // baseline. Comparing the first retail sample with our modern camera made
    // an arbitrary E press look like a huge native-camera jump. A genuine
    // script-owner transition is recorded separately and remains immediate.
    state.ownerless_interaction_armed =
        state.previous_native_candidate_valid &&
        state.idle_native_quiet_ticks >= 5u;
    const bool owner_transition_at_arm = owner != 0 && owner != state.owner;
    state.interaction_scripted_view_transition_seen =
        scripted_view_active && !state.previous_scripted_view_active;
    state.stationary_native_motion = 0.0;
    state.candidate_motion_ticks = 0;
    state.last_interaction_sequence = interaction_sequence;
    state.interaction_consumed = false;
    if (g_debug_log) {
      AppendNativeLog(
          "camera_script arm sequence=%llu ownerless=%d quiet=%u "
          "owner_transition=%d scripted_transition=%d flags=%08X",
          static_cast<unsigned long long>(interaction_sequence),
          state.ownerless_interaction_armed ? 1 : 0,
          state.idle_native_quiet_ticks,
          owner_transition_at_arm ? 1 : 0,
          state.interaction_scripted_view_transition_seen ? 1 : 0,
          controller_flags);
    }
  }
  if (!state.interaction_consumed && recent_interaction &&
      scripted_view_active && !state.previous_scripted_view_active) {
    state.interaction_scripted_view_transition_seen = true;
    if (owner != 0 && RetailCameraOwnerAlreadyCompleted(owner)) {
      AppendNativeLog(
          "camera_script repeated_owner_suppressed owner=%08llX flags=%08X",
          static_cast<unsigned long long>(owner), controller_flags);
    }
  }
  if (state.takeover_latched) {
    state.owner_seen_during_takeover =
        state.owner_seen_during_takeover || owner != 0;
    state.owner_release_ticks =
        state.owner_seen_during_takeover && owner == 0
            ? state.owner_release_ticks + 1u
            : 0u;
    state.moving_ticks = player_motion > 32.0 ? state.moving_ticks + 1u : 0u;
    state.settled_ticks = native_motion < 8.0 ? state.settled_ticks + 1u : 0u;
    const uint64_t takeover_age_ms = now_ms - state.takeover_started_ms;
    // A lever sequence may pause for one or two seconds before the authored
    // camera owner is installed.  Runtime 0.0.62 released the native camera
    // during that pause, then resumed the orbit just as the actual reveal
    // began.  Once an owner has been observed, its release is authoritative.
    if (state.owner_seen_during_takeover && takeover_age_ms >= 700u &&
        state.owner_release_ticks >= 6u) {
      ClearRetailCameraTakeover("script_owner_released", true);
      return false;
    }
    // Some retail scripts leave their owner installed after the visible shot
    // has stopped. A quiet native endpoint must not retain the authored camera
    // until the nine-second emergency timeout.
    if (state.owner_seen_during_takeover && takeover_age_ms >= 2600u &&
        state.settled_ticks >= 24u) {
      ClearRetailCameraTakeover("script_owner_settled", true);
      return false;
    }
    // Owner-less reveals are retained for a bounded quiet tail.  Do not use
    // player motion as an early-out: several switches restore player control
    // before their final camera endpoint has been published.
    if (!state.owner_seen_during_takeover && takeover_age_ms >= 2600u &&
        state.settled_ticks >= 24u) {
      ClearRetailCameraTakeover("ownerless_shot_settled", true);
      return false;
    }
    if (takeover_age_ms > 9000u) {
      ClearRetailCameraTakeover("timeout", true);
      return false;
    }
  } else {
    if (!recent_interaction) {
      state.idle_native_quiet_ticks =
          native_motion < 8.0
              ? std::min(state.idle_native_quiet_ticks + 1u, 120u)
              : 0u;
    }
    if (!recent_interaction || !player_stationary) {
      state.stationary_native_motion = 0.0;
      state.candidate_motion_ticks = 0;
    } else if (native_motion >= 20.0) {
      state.stationary_native_motion += native_motion;
      state.candidate_motion_ticks =
          std::min(state.candidate_motion_ticks + 1u, 120u);
    } else if (native_motion < 8.0) {
      state.candidate_motion_ticks = 0;
    }

    // The broad owner pointer also belongs to ordinary fixed-camera zones and
    // cannot authorize a view by itself. Require its verified target-
    // convergence transition inside the bounded interaction window. This also
    // admits delayed lever shots which start after the old 1500 ms owner-only
    // deadline, while completed owners cannot replay on a later stray operate
    // press in the same session.
    const bool scripted_view_reveal =
        !state.interaction_consumed && player_stationary &&
        recent_interaction && scripted_view_active &&
        state.interaction_scripted_view_transition_seen && owner != 0 &&
        !RetailCameraOwnerAlreadyCompleted(owner);
    const bool travelling_reveal =
        !state.interaction_consumed && player_stationary && recent_interaction &&
        state.ownerless_interaction_armed && interaction_age_ms >= 250u &&
        state.candidate_motion_ticks >= 3u &&
        state.stationary_native_motion >= 360.0;
    if (now_ms >= state.cooldown_until_ms &&
        (scripted_view_reveal || travelling_reveal)) {
      state.takeover_latched = true;
      state.takeover_started_ms = now_ms;
      state.moving_ticks = 0;
      state.settled_ticks = 0;
      state.owner_release_ticks = 0;
      state.owner_seen_during_takeover = owner != 0;
      state.takeover_owner = owner;
      state.interaction_consumed = true;
      AppendNativeLog(
          "camera_script takeover=ON owner=%08llX player_motion=%.1f "
          "native_motion=%.1f accumulated=%.1f age=%llu reason=%s "
          "flags=%08X",
          static_cast<unsigned long long>(owner), player_motion,
          native_motion, state.stationary_native_motion,
          static_cast<unsigned long long>(interaction_age_ms),
          scripted_view_reveal ? "SCRIPT_FLAG" : "TRAVELLING",
          controller_flags);
    }
  }

  state.raw_owner_active = owner != 0;
  state.owner = owner;
  state.previous_scripted_view_active = scripted_view_active;
  state.previous_player = player;
  state.previous_player_valid = true;
  state.previous_native_candidate = native_candidate;
  state.previous_native_candidate_valid = true;
  return state.takeover_latched;
}

bool InitializeThirdPersonOrbit(void* controller, int32_t native_x,
                                int32_t native_y, int32_t native_z,
                                const std::array<int32_t, 3>& focus,
                                uint64_t input_sequence) {
  const double dx = static_cast<double>(native_x - focus[0]);
  const double dy = static_cast<double>(native_y - focus[1]);
  const double dz = static_cast<double>(native_z - focus[2]);
  const double horizontal = std::hypot(dx, dz);
  const double radius = std::hypot(horizontal, dy);
  if (!std::isfinite(radius) || radius < 250.0 || radius > 5000.0) {
    return false;
  }
  g_third_person_orbit_state.controller = controller;
  g_third_person_orbit_state.engaged = true;
  g_third_person_orbit_state.yaw = std::atan2(dx, dz);
  g_third_person_heading_reference_microradians.store(
      static_cast<int32_t>(std::lround(
          g_third_person_orbit_state.yaw * 1000000.0)),
      std::memory_order_release);
  g_third_person_heading_reference_valid.store(true,
                                                std::memory_order_release);
  g_third_person_orbit_state.pitch = std::clamp(
      std::atan2(dy, std::max(horizontal, 1.0)),
      g_third_person_orbit_min_pitch_radians,
      g_third_person_orbit_max_pitch_radians);
  // The retail candidate is frequently only a few hundred world units from
  // Lara. It remains useful for seeding yaw/pitch, but adopting its distance
  // pinned the modern camera to the minimum radius. Start the spring arm at a
  // stable user-selected distance and let the native collision resolver pull
  // it in only when geometry actually requires it.
  g_third_person_orbit_state.radius =
      g_third_person_orbit_preferred_radius;
  g_third_person_orbit_state.collision_radius =
      g_third_person_orbit_state.radius;
  g_third_person_orbit_state.collision_clear_ticks = 0;
  g_third_person_orbit_state.collision_blocked_release_ticks = 0;
  g_third_person_orbit_state.collision_blocked_candidate_distance =
      g_third_person_orbit_state.collision_radius;
  g_third_person_orbit_state.collision_blocker_key = 0;
  g_third_person_orbit_state.mesh_contact_preference = {};
  g_third_person_orbit_state.last_orbit_activity_ms = GetTickCount64();
  g_third_person_orbit_state.motion_active_this_tick = false;
  g_third_person_orbit_state.previous_player = focus;
  g_third_person_orbit_state.chase_focus = {
      static_cast<double>(focus[0]), static_cast<double>(focus[1]),
      static_cast<double>(focus[2])};
  g_third_person_orbit_state.chase_velocity = {0.0, 0.0, 0.0};
  g_third_person_orbit_state.chase_initialized = true;
  // Consume the current sample below so the first stick movement takes
  // effect immediately instead of waiting for another native source tick.
  g_third_person_orbit_state.last_input_sequence =
      input_sequence ? input_sequence - 1u : 0u;
  g_third_person_orbit_state.last_input_time_ms =
      GetTickCount64() - kOriginalPeriodMilliseconds;
  AppendNativeLog(
      "camera_orbit engage native=%d/%d/%d focus=%d/%d/%d "
      "yaw=%.2f pitch=%.2f radius=%.1f",
      native_x, native_y, native_z, focus[0], focus[1], focus[2],
      g_third_person_orbit_state.yaw * 180.0 / kOrbitPi,
      g_third_person_orbit_state.pitch * 180.0 / kOrbitPi,
      g_third_person_orbit_state.radius);
  return true;
}

void ResetThirdPersonOrbit(const char* reason) {
  if (g_third_person_orbit_state.owned_publication_active) {
    g_owned_camera_presentation_cut_pending.store(
        true, std::memory_order_release);
  }
  g_custom_head_publication_active.store(false,
                                          std::memory_order_release);
  g_custom_head_last_publication_ms.store(0, std::memory_order_release);
  if (g_third_person_orbit_state.engaged) {
    AppendNativeLog("camera_orbit disengage reason=%s applications=%llu",
                    reason,
                    static_cast<unsigned long long>(
                        g_third_person_orbit_state.applications));
  }
  g_third_person_orbit_state = {};
  g_third_person_heading_reference_valid.store(false,
                                                std::memory_order_release);
}

bool BuildThirdPersonOrbitPosition(void* controller,
                                   const std::array<int32_t, 3>& native,
                                   std::array<int32_t, 3>* orbit) {
  if (!controller || !orbit) {
    return false;
  }
  if (!g_third_person_orbit_enabled) {
    ResetThirdPersonOrbit("disabled");
    return false;
  }
  // This function is reached only from the verified mode-3 dispatcher hook.
  // Do not re-check controller+0x27C after the retail callback: fixed/rail
  // branches rewrite that byte to 0/1 even though execution is still inside
  // the mode-3 camera path.  The old post-callback test made orbit disappear
  // in most rooms and resume only when the byte happened to return to 3.
  // Input ownership and camera ownership are intentionally separate.  A
  // radial selector may suppress the right stick, and a machine may have no
  // XInput controller at all, but neither condition may hand the camera back
  // to a different retail rig.  They merely contribute zero orbit input.

  std::array<int32_t, 3> focus{};
  if (!ReadCameraFocusPosition(controller, &focus)) {
    ResetThirdPersonOrbit("camera_focus");
    return false;
  }

  constexpr double kInputScale = 1000000.0;
  const double input_x = static_cast<double>(
      g_third_person_orbit_input_x.load(std::memory_order_relaxed)) /
      kInputScale;
  const double input_y = static_cast<double>(
      g_third_person_orbit_input_y.load(std::memory_order_relaxed)) /
      kInputScale;
  const bool stick_input_active =
      g_third_person_orbit_input_active.load(std::memory_order_acquire);
  const int32_t mouse_delta_x = std::clamp(
      g_third_person_mouse_delta_x.exchange(0, std::memory_order_acq_rel),
      -2048, 2048);
  const int32_t mouse_delta_y = std::clamp(
      g_third_person_mouse_delta_y.exchange(0, std::memory_order_acq_rel),
      -2048, 2048);
  const uint64_t input_sequence =
      g_third_person_orbit_input_sequence.load(std::memory_order_acquire);
  const bool stick_moved = std::abs(input_x) > 0.0001 ||
                           std::abs(input_y) > 0.0001;
  const bool mouse_moved = mouse_delta_x != 0 || mouse_delta_y != 0;

  if (!g_third_person_orbit_state.engaged ||
      g_third_person_orbit_state.controller != controller) {
    // Seed the persistent third-person rig immediately.  Requiring a stick or
    // mouse event made the initial camera remain retail until a controller was
    // connected or moved.
    if (!InitializeThirdPersonOrbit(controller, native[0], native[1], native[2],
                                    focus, input_sequence)) {
      return false;
    }
  } else {
    const double focus_jump = std::hypot(
        std::hypot(static_cast<double>(
                       focus[0] -
                       g_third_person_orbit_state.previous_player[0]),
                   static_cast<double>(
                       focus[2] -
                       g_third_person_orbit_state.previous_player[2])),
        static_cast<double>(
            focus[1] - g_third_person_orbit_state.previous_player[1]));
    if (focus_jump > 2500.0 &&
        !InitializeThirdPersonOrbit(controller, native[0], native[1], native[2],
                                    focus, input_sequence)) {
      ResetThirdPersonOrbit("focus_teleport");
      return false;
    }
  }

  if (g_third_person_orbit_state.suspended) {
    g_third_person_orbit_state.suspended = false;
    g_third_person_orbit_state.last_input_time_ms = GetTickCount64();
    g_third_person_orbit_state.last_input_sequence = input_sequence;
    AppendNativeLog("camera_orbit resume yaw=%.2f pitch=%.2f radius=%.1f",
                    g_third_person_orbit_state.yaw * 180.0 / kOrbitPi,
                    g_third_person_orbit_state.pitch * 180.0 / kOrbitPi,
                    g_third_person_orbit_state.radius);
  }

  if (!stick_input_active) {
    // Selector and first-person ownership must be an immediate input cut, not
    // merely a zero target for the response filter. Otherwise the filtered
    // right-stick velocity keeps rotating the third-person camera for several
    // source ticks while the radial selector is visibly open.
    g_third_person_orbit_state.filtered_input_x = 0.0;
    g_third_person_orbit_state.filtered_input_y = 0.0;
  }

  const bool custom_head_view = CustomHeadViewSelected();
  const double active_minimum_pitch =
      custom_head_view ? g_custom_head_min_pitch_radians
                       : g_third_person_orbit_min_pitch_radians;
  const double active_maximum_pitch =
      custom_head_view ? g_custom_head_max_pitch_radians
                       : g_third_person_orbit_max_pitch_radians;
  g_third_person_orbit_state.pitch = std::clamp(
      g_third_person_orbit_state.pitch, active_minimum_pitch,
      active_maximum_pitch);

  if (input_sequence != g_third_person_orbit_state.last_input_sequence) {
    const uint64_t now_ms = GetTickCount64();
    const double elapsed_seconds = std::clamp(
        static_cast<double>(now_ms -
                            g_third_person_orbit_state.last_input_time_ms) /
            1000.0,
        0.010, 0.100);
    const double response = 1.0 - std::exp(
        -elapsed_seconds / g_third_person_orbit_response_seconds);
    if (stick_input_active) {
      g_third_person_orbit_state.filtered_input_x +=
          (input_x - g_third_person_orbit_state.filtered_input_x) * response;
      g_third_person_orbit_state.filtered_input_y +=
          (input_y - g_third_person_orbit_state.filtered_input_y) * response;
    }
    const CameraOrbitStickInput mode_input = CameraOrbitModeInput(
        g_third_person_orbit_state.filtered_input_x,
        g_third_person_orbit_state.filtered_input_y, custom_head_view,
        g_third_person_orbit_invert_x, g_third_person_orbit_invert_y,
        g_third_person_stick_speed_scale);
    g_third_person_orbit_state.yaw +=
        mode_input.x * g_third_person_orbit_horizontal_radians *
        (elapsed_seconds * 1000.0 /
         static_cast<double>(kOriginalPeriodMilliseconds));
    g_third_person_orbit_state.pitch = std::clamp(
        g_third_person_orbit_state.pitch +
            mode_input.y * g_third_person_orbit_vertical_radians *
                (elapsed_seconds * 1000.0 /
                 static_cast<double>(kOriginalPeriodMilliseconds)),
        active_minimum_pitch, active_maximum_pitch);
    if (g_third_person_orbit_state.yaw > kOrbitPi ||
        g_third_person_orbit_state.yaw < -kOrbitPi) {
      g_third_person_orbit_state.yaw = std::remainder(
          g_third_person_orbit_state.yaw, 2.0 * kOrbitPi);
    }
    g_third_person_orbit_state.last_input_sequence = input_sequence;
    g_third_person_orbit_state.last_input_time_ms = now_ms;
  }

  // Physical relative mouse motion is positional, not a velocity. Apply each
  // accumulated sample exactly once without stick-style temporal filtering;
  // the native 50 Hz snapshot interpolation provides the visible smoothness.
  if (mouse_moved) {
    const double horizontal_sign = g_third_person_orbit_invert_x ? -1.0 : 1.0;
    const double configured_vertical_sign =
        g_third_person_orbit_invert_y ? -1.0 : 1.0;
    // DirectInput's physical mouse Y convention and Dungeon's head-mounted
    // look-at pitch convention face opposite directions at the publication
    // boundary.  Keep the accepted third-person mouse orbit untouched and
    // reverse only the physical-mouse contribution to immersive pitch.  The
    // right-stick path above already has its independently accepted sign.
    const double vertical_sign = custom_head_view
        ? ImmersivePhysicalMouseVerticalSign(
              g_third_person_orbit_invert_y)
        : configured_vertical_sign;
    g_third_person_orbit_state.yaw +=
        static_cast<double>(mouse_delta_x) * horizontal_sign *
        g_third_person_mouse_horizontal_radians;
    g_third_person_orbit_state.pitch = std::clamp(
        g_third_person_orbit_state.pitch +
            static_cast<double>(mouse_delta_y) * vertical_sign *
                g_third_person_mouse_vertical_radians,
        active_minimum_pitch, active_maximum_pitch);
    if (g_third_person_orbit_state.yaw > kOrbitPi ||
        g_third_person_orbit_state.yaw < -kOrbitPi) {
      g_third_person_orbit_state.yaw = std::remainder(
          g_third_person_orbit_state.yaw, 2.0 * kOrbitPi);
    }
  }

  g_third_person_heading_reference_microradians.store(
      static_cast<int32_t>(std::lround(
          g_third_person_orbit_state.yaw * 1000000.0)),
      std::memory_order_release);
  g_third_person_heading_reference_valid.store(true,
                                                std::memory_order_release);

  const double player_motion = CameraPositionDistance(
      focus, g_third_person_orbit_state.previous_player);
  g_third_person_orbit_state.motion_active_this_tick =
      player_motion > 8.0 || stick_moved || mouse_moved;
  g_third_person_orbit_state.orbit_input_active_this_tick =
      stick_moved || mouse_moved;
  const uint64_t orbit_now_ms = GetTickCount64();
  if (g_third_person_orbit_state.orbit_input_active_this_tick) {
    g_third_person_orbit_state.last_orbit_activity_ms = orbit_now_ms;
  }

  const std::array<double, 3> chase_target = {
      static_cast<double>(focus[0]), static_cast<double>(focus[1]),
      static_cast<double>(focus[2])};
  const double chase_error = std::hypot(
      std::hypot(chase_target[0] -
                     g_third_person_orbit_state.chase_focus[0],
                 chase_target[2] -
                     g_third_person_orbit_state.chase_focus[2]),
      chase_target[1] - g_third_person_orbit_state.chase_focus[1]);
  if (!g_third_person_orbit_state.chase_initialized ||
      !std::isfinite(chase_error)) {
    g_third_person_orbit_state.chase_focus = chase_target;
    g_third_person_orbit_state.chase_velocity = {0.0, 0.0, 0.0};
    g_third_person_orbit_state.chase_initialized = true;
  } else {
    constexpr double kChaseDeltaSeconds =
        static_cast<double>(kOriginalPeriodMilliseconds) / 1000.0;
    const CameraChaseStep chase = StepCameraPivotChase(
        g_third_person_orbit_state.chase_focus,
        g_third_person_orbit_state.chase_velocity, chase_target,
        kChaseDeltaSeconds, 2.0, 9000.0, 50000.0);
    g_third_person_orbit_state.chase_focus = chase.position;
    g_third_person_orbit_state.chase_velocity = chase.velocity;
  }

  // Always construct and test the complete desired spring arm. Earlier
  // versions tested only the already-contracted arm and extended it before
  // every query, producing an artificial extend->hit->retract oscillation.
  const double active_radius = g_third_person_orbit_state.radius;
  const double horizontal = active_radius *
                            std::cos(g_third_person_orbit_state.pitch);
  const int32_t orbit_x = static_cast<int32_t>(std::lround(
      g_third_person_orbit_state.chase_focus[0] +
      std::sin(g_third_person_orbit_state.yaw) * horizontal));
  const int32_t requested_orbit_y =
      static_cast<int32_t>(std::lround(
          g_third_person_orbit_state.chase_focus[1] +
          std::sin(g_third_person_orbit_state.pitch) * active_radius));
  const CameraFloorLimit floor_limit = ResolveCameraFloorLimit(
      focus, g_previous_snapshot.player_position,
      g_previous_snapshot.player_position_valid);
  const int32_t orbit_y =
      std::max(requested_orbit_y, floor_limit.minimum_y);
  const int32_t orbit_z = static_cast<int32_t>(std::lround(
      g_third_person_orbit_state.chase_focus[2] +
      std::cos(g_third_person_orbit_state.yaw) * horizontal));
  if (g_debug_log && orbit_y != requested_orbit_y) {
    AppendNativeLog(
        "camera_floor_guard requested_y=%d safe_y=%d focus_y=%d "
        "player_root_y=%d player_root_used=%u",
        requested_orbit_y, orbit_y, focus[1],
        g_previous_snapshot.player_position[1],
        floor_limit.player_root_used ? 1u : 0u);
  }
  g_third_person_orbit_state.requested_position = {
      orbit_x, orbit_y, orbit_z};
  g_third_person_orbit_state.requested_position_valid = true;
  g_third_person_orbit_state.previous_player = focus;
  ++g_third_person_orbit_state.applications;
  if (g_debug_log &&
      (g_third_person_orbit_state.applications % 60u) == 1u) {
    AppendNativeLog(
        "camera_orbit apply input=%.3f/%.3f filtered=%.3f/%.3f "
        "desired=%d/%d/%d "
        "focus=%d/%d/%d yaw=%.2f pitch=%.2f radius=%.1f seq=%llu",
        input_x, input_y, g_third_person_orbit_state.filtered_input_x,
        g_third_person_orbit_state.filtered_input_y,
        orbit_x, orbit_y, orbit_z, focus[0], focus[1], focus[2],
        g_third_person_orbit_state.yaw * 180.0 / kOrbitPi,
        g_third_person_orbit_state.pitch * 180.0 / kOrbitPi,
        active_radius,
        static_cast<unsigned long long>(input_sequence));
  }
  *orbit = {orbit_x, orbit_y, orbit_z};
  return true;
}

bool SceneNodeDescendsFrom(const SceneSnapshot& scene, uintptr_t node,
                           uintptr_t ancestor) {
  if (!node || !ancestor) {
    return false;
  }
  for (uint32_t depth = 0; node && depth < 128u; ++depth) {
    if (node == ancestor) {
      return true;
    }
    const auto entry = scene.nodes.find(node);
    if (entry == scene.nodes.end() || entry->second.parent == node) {
      return false;
    }
    node = entry->second.parent;
  }
  return false;
}

struct HeadJointProbeCandidate {
  uintptr_t node = 0;
  uintptr_t parent = 0;
  uintptr_t resource = 0;
  uint32_t flags = 0;
  uint32_t depth = 0;
  uint32_t children = 0;
  int32_t relative_x = 0;
  int32_t relative_y = 0;
  int32_t relative_z = 0;
  int64_t horizontal_distance_squared = 0;
  const NodeTransform* transform = nullptr;
};

uintptr_t ResolvePlayerHeadJoint(const SceneSnapshot& scene) {
  if (!scene.player || scene.nodes.find(scene.player) == scene.nodes.end()) {
    return 0;
  }

  std::unordered_map<uintptr_t, uint32_t> topology_child_counts;
  topology_child_counts.reserve(scene.nodes.size());
  for (const auto& [node, transform] : scene.nodes) {
    if (node != scene.player && transform.parent) {
      const auto parent = scene.nodes.find(transform.parent);
      if (parent != scene.nodes.end() && CountsTowardImmersiveJointTopology(
              parent->second.flags, transform.flags)) {
        ++topology_child_counts[transform.parent];
      }
    }
  }

  uintptr_t selected = 0;
  for (const auto& [node, transform] : scene.nodes) {
    if (node == scene.player ||
        !SceneNodeDescendsFrom(scene, node, scene.player) ||
        !transform.bounds_valid) {
      continue;
    }
    const auto parent = scene.nodes.find(transform.parent);
    if (parent == scene.nodes.end()) {
      continue;
    }
    uint32_t depth = 0;
    uintptr_t ancestor = node;
    for (; ancestor && depth < 128u; ++depth) {
      if (ancestor == scene.player) {
        break;
      }
      const auto entry = scene.nodes.find(ancestor);
      if (entry == scene.nodes.end() || entry->second.parent == ancestor) {
        break;
      }
      ancestor = entry->second.parent;
    }
    if (ancestor != scene.player ||
        !MatchesImmersiveHeadJoint(
            topology_child_counts[node],
            parent->second.render_resource_handle,
            topology_child_counts[parent->second.parent],
            transform.bounds_radius,
            depth)) {
      continue;
    }

    // Topology and bounds must identify exactly one animated head. Do not
    // score ambiguous candidates by their current animation pose: that would
    // reintroduce frame-dependent identity and visible camera fallbacks.
    if (selected != 0) {
      return 0;
    }
    selected = node;
  }
  return selected;
}

bool ResolveLivePlayerHeadMount(
    const SceneSnapshot& scene,
    const std::array<int32_t, 3>& current_camera_focus,
    std::array<int32_t, 3>* head_center, uintptr_t* head_node) {
  if (!head_center) {
    return false;
  }
  const uintptr_t node = ResolvePlayerHeadJoint(scene);
  const auto player = scene.nodes.find(scene.player);
  const auto head = scene.nodes.find(node);
  if (!node || player == scene.nodes.end() || head == scene.nodes.end()) {
    return false;
  }

  Matrix3x4 live_player = player->second.world;
  Matrix3x4 live_head = head->second.world;
  // The source camera hook runs after animation/cache preparation in the
  // supported renderer. Read the live matrices so the new endpoint and the
  // body rendered for that endpoint belong to the same animation sample.
  SafeRead(reinterpret_cast<const void*>(scene.player + kMatrixOffset),
           &live_player, sizeof(live_player));
  SafeRead(reinterpret_cast<const void*>(node + kMatrixOffset),
           &live_head, sizeof(live_head));

  std::array<int32_t, 3> focus_from_snapshot_root = {0, 400, 0};
  if (scene.camera_focus_valid) {
    for (size_t axis = 0; axis < 3u; ++axis) {
      focus_from_snapshot_root[axis] =
          scene.camera_focus[axis] - player->second.world.values[9u + axis];
    }
  }
  for (size_t axis = 0; axis < 3u; ++axis) {
    const int64_t current_root =
        static_cast<int64_t>(current_camera_focus[axis]) -
        focus_from_snapshot_root[axis];
    const int64_t animated_offset =
        static_cast<int64_t>(live_head.values[9u + axis]) -
        live_player.values[9u + axis];
    (*head_center)[axis] = static_cast<int32_t>(std::clamp<int64_t>(
        current_root + animated_offset, std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max()));
  }
  if (head_node) {
    *head_node = node;
  }
  return true;
}

void ProbePlayerHeadJoints(const SceneSnapshot& scene, uint64_t source_tick,
                           bool force_on_resolution_failure = false) {
  static uintptr_t last_forced_player = 0;
  const bool scheduled_probe =
      g_head_joint_probe_enabled && (source_tick % 10u) == 0u;
  if (!g_debug_log || !CustomHeadViewSelected() || !scene.player ||
      (!scheduled_probe && !force_on_resolution_failure) ||
      (force_on_resolution_failure && last_forced_player == scene.player)) {
    return;
  }

  const auto player_entry = scene.nodes.find(scene.player);
  if (player_entry == scene.nodes.end()) {
    AppendNativeLog("head_joint_probe tick=%llu valid=0 reason=PLAYER_NODE",
                    static_cast<unsigned long long>(source_tick));
    return;
  }
  if (force_on_resolution_failure) {
    // Production keeps the heavy periodic probe disabled, but one compact
    // candidate set for each player object makes an unsupported skeleton
    // diagnosable from the ordinary per-launch log.
    last_forced_player = scene.player;
  }

  std::unordered_map<uintptr_t, uint32_t> child_counts;
  child_counts.reserve(scene.nodes.size());
  for (const auto& [node, transform] : scene.nodes) {
    if (node != scene.player && transform.parent) {
      ++child_counts[transform.parent];
    }
  }

  const auto& root_world = player_entry->second.world.values;
  const int32_t root_x = root_world[9];
  const int32_t root_y = root_world[10];
  const int32_t root_z = root_world[11];
  std::vector<HeadJointProbeCandidate> candidates;
  candidates.reserve(32u);
  for (const auto& [node, transform] : scene.nodes) {
    if (node == scene.player || !SceneNodeDescendsFrom(scene, node,
                                                       scene.player)) {
      continue;
    }

    const int32_t relative_x = transform.world.values[9] - root_x;
    const int32_t relative_y = transform.world.values[10] - root_y;
    const int32_t relative_z = transform.world.values[11] - root_z;
    const int64_t horizontal_distance_squared =
        static_cast<int64_t>(relative_x) * relative_x +
        static_cast<int64_t>(relative_z) * relative_z;
    // Keep the complete torso/head neighbourhood. The generous limits retain
    // animated hands and weapons as negative examples, while excluding the
    // rest of the room and keeping each diagnostic session compact.
    if (relative_y < 0 || relative_y > 1400 ||
        horizontal_distance_squared > 900LL * 900LL) {
      continue;
    }

    uint32_t depth = 0;
    uintptr_t ancestor = node;
    for (; ancestor && depth < 128u; ++depth) {
      if (ancestor == scene.player) {
        break;
      }
      const auto ancestor_entry = scene.nodes.find(ancestor);
      if (ancestor_entry == scene.nodes.end() ||
          ancestor_entry->second.parent == ancestor) {
        break;
      }
      ancestor = ancestor_entry->second.parent;
    }
    if (ancestor != scene.player) {
      continue;
    }

    candidates.push_back({
        node, transform.parent, transform.render_resource_handle,
        transform.flags, depth, child_counts[node], relative_x, relative_y,
        relative_z, horizontal_distance_squared, &transform});
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const HeadJointProbeCandidate& left,
               const HeadJointProbeCandidate& right) {
              if (left.relative_y != right.relative_y) {
                return left.relative_y > right.relative_y;
              }
              if (left.horizontal_distance_squared !=
                  right.horizontal_distance_squared) {
                return left.horizontal_distance_squared <
                       right.horizontal_distance_squared;
              }
              return left.node < right.node;
            });

  constexpr size_t kMaximumLoggedCandidates = 24u;
  const size_t logged =
      std::min(candidates.size(), kMaximumLoggedCandidates);
  AppendNativeLog(
      "head_joint_probe tick=%llu valid=1 player=%08llX root=%d/%d/%d "
      "candidates=%zu logged=%zu",
      static_cast<unsigned long long>(source_tick),
      static_cast<unsigned long long>(scene.player), root_x, root_y, root_z,
      candidates.size(), logged);
  for (size_t rank = 0; rank < logged; ++rank) {
    const HeadJointProbeCandidate& candidate = candidates[rank];
    const auto& world = candidate.transform->world.values;
    const auto& local = candidate.transform->local.values;
    AppendNativeLog(
        "head_joint_candidate tick=%llu rank=%zu node=%08llX "
        "parent=%08llX depth=%u children=%u resource=%08llX flags=%08X "
        "local=%d/%d/%d world=%d/%d/%d rel=%d/%d/%d bounds=%u/%d",
        static_cast<unsigned long long>(source_tick), rank,
        static_cast<unsigned long long>(candidate.node),
        static_cast<unsigned long long>(candidate.parent), candidate.depth,
        candidate.children,
        static_cast<unsigned long long>(candidate.resource), candidate.flags,
        local[9], local[10], local[11], world[9], world[10], world[11],
        candidate.relative_x, candidate.relative_y, candidate.relative_z,
        candidate.transform->bounds_valid ? 1u : 0u,
        candidate.transform->bounds_radius);
  }
}

Vec3 CameraMeshPointToWorld(const Matrix3x4& matrix,
                            const std::array<int32_t, 3>& point) {
  // Asylum stores affine matrices for row-vector multiplication: values
  // 0/1/2 are the world components of local X, 3/4/5 of local Y and 6/7/8
  // of local Z. All components, including translation, use 14-bit fixed
  // point, so the result remains in the same coordinate space as the camera
  // controller and node+0x80 world bounds.
  constexpr double scale = 1.0 / 16384.0;
  return {
      (static_cast<double>(point[0]) * matrix.values[0] +
       static_cast<double>(point[1]) * matrix.values[3] +
       static_cast<double>(point[2]) * matrix.values[6]) * scale +
          matrix.values[9],
      (static_cast<double>(point[0]) * matrix.values[1] +
       static_cast<double>(point[1]) * matrix.values[4] +
       static_cast<double>(point[2]) * matrix.values[7]) * scale +
          matrix.values[10],
      (static_cast<double>(point[0]) * matrix.values[2] +
       static_cast<double>(point[1]) * matrix.values[5] +
       static_cast<double>(point[2]) * matrix.values[8]) * scale +
          matrix.values[11]};
}

bool ReadCameraMeshVertex(uintptr_t reference,
                          std::array<int32_t, 3>* vertex) {
  if (!reference || !vertex) {
    return false;
  }
  uintptr_t vertex_address = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(reference + 8u),
                     &vertex_address) ||
      !vertex_address ||
      !SafeRead(reinterpret_cast<const void*>(vertex_address + 4u),
                vertex->data(), sizeof(*vertex))) {
    return false;
  }
  // Corrupt/unloaded resource pointers must never turn into an enormous
  // camera blocker. Retail levels stay many orders of magnitude below this.
  constexpr int32_t kMaximumCoordinate = 1 << 26;
  return std::abs(static_cast<int64_t>((*vertex)[0])) < kMaximumCoordinate &&
         std::abs(static_cast<int64_t>((*vertex)[1])) < kMaximumCoordinate &&
         std::abs(static_cast<int64_t>((*vertex)[2])) < kMaximumCoordinate;
}

const CameraCollisionMesh* ResolveCameraCollisionMesh(uintptr_t handle) {
  if (!g_dungeon_base || !handle || handle > 0xFFFFu) {
    return nullptr;
  }
  int32_t resource_count = 0;
  if (!SafeReadValue(g_dungeon_base + kRenderResourceCountRva,
                     &resource_count) ||
      resource_count <= 0 || handle >= static_cast<uintptr_t>(resource_count)) {
    return nullptr;
  }
  uintptr_t resource = 0;
  if (!SafeReadValue(g_dungeon_base + kRenderResourceTableRva +
                         handle * sizeof(uintptr_t),
                     &resource) ||
      !resource) {
    return nullptr;
  }
  uint32_t polygon_count = 0;
  uintptr_t polygon_table = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(resource + 0x18u),
                     &polygon_count) ||
      !SafeReadValue(reinterpret_cast<const void*>(resource + 0x1Cu),
                     &polygon_table) ||
      !polygon_table || polygon_count == 0 || polygon_count > 65536u) {
    return nullptr;
  }

  auto found = g_camera_collision_meshes.find(handle);
  if (found != g_camera_collision_meshes.end() &&
      found->second.resource == resource &&
      found->second.polygon_table == polygon_table &&
      found->second.polygon_count == polygon_count) {
    ++g_camera_mesh_cache_hits;
    return found->second.parsed ? &found->second : nullptr;
  }

  ++g_camera_mesh_cache_misses;
  CameraCollisionMesh mesh;
  mesh.resource = resource;
  mesh.polygon_table = polygon_table;
  mesh.polygon_count = polygon_count;
  std::array<int32_t, 3> local_min = {
      std::numeric_limits<int32_t>::max(),
      std::numeric_limits<int32_t>::max(),
      std::numeric_limits<int32_t>::max()};
  std::array<int32_t, 3> local_max = {
      std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::min()};
  bool local_bounds_valid = false;
  constexpr uint32_t kMaximumPolygonVertices = 128u;
  constexpr size_t kMaximumTrianglesPerResource = 262144u;
  for (uint32_t polygon_index = 0; polygon_index < polygon_count;
       ++polygon_index) {
    const uintptr_t polygon = polygon_table +
        static_cast<uintptr_t>(polygon_index) * 0x34u;
    uint32_t vertex_count = 0;
    uintptr_t references = 0;
    if (!SafeReadValue(reinterpret_cast<const void*>(polygon + 0x28u),
                       &vertex_count) ||
        !SafeReadValue(reinterpret_cast<const void*>(polygon + 0x2Cu),
                       &references) ||
        !references || vertex_count < 3u ||
        vertex_count > kMaximumPolygonVertices) {
      continue;
    }
    std::vector<std::array<int32_t, 3>> vertices(vertex_count);
    bool valid = true;
    for (uint32_t vertex_index = 0; vertex_index < vertex_count;
         ++vertex_index) {
      if (!ReadCameraMeshVertex(
              references + static_cast<uintptr_t>(vertex_index) * 0x10u,
              &vertices[vertex_index])) {
        valid = false;
        break;
      }
    }
    if (!valid) {
      continue;
    }
    for (const auto& vertex : vertices) {
      for (size_t axis = 0; axis < vertex.size(); ++axis) {
        local_min[axis] = std::min(local_min[axis], vertex[axis]);
        local_max[axis] = std::max(local_max[axis], vertex[axis]);
      }
      local_bounds_valid = true;
    }
    // Asylum's renderer emits a convex polygon from every 0x34-byte surface
    // record. The same fan used by the fixed-function backend gives us the
    // actual visible surface rather than a coarse node sphere.
    for (uint32_t vertex_index = 1u; vertex_index + 1u < vertex_count;
         ++vertex_index) {
      mesh.triangles.push_back(
          {vertices[0], vertices[vertex_index],
           vertices[vertex_index + 1u]});
      if (mesh.triangles.size() >= kMaximumTrianglesPerResource) {
        break;
      }
    }
    if (mesh.triangles.size() >= kMaximumTrianglesPerResource) {
      break;
    }
  }
  mesh.local_bounds_valid = local_bounds_valid;
  if (local_bounds_valid) {
    mesh.local_min = local_min;
    mesh.local_max = local_max;
  }
  mesh.parsed = !mesh.triangles.empty() && mesh.local_bounds_valid;
  auto inserted = g_camera_collision_meshes.insert_or_assign(
      handle, std::move(mesh));
  if (g_debug_log) {
    AppendNativeLog(
        "camera_mesh_cache handle=%llu polygons=%u triangles=%llu valid=%d",
        static_cast<unsigned long long>(handle), polygon_count,
        static_cast<unsigned long long>(inserted.first->second.triangles.size()),
        inserted.first->second.parsed ? 1 : 0);
  }
  return inserted.first->second.parsed ? &inserted.first->second : nullptr;
}

bool CameraCollisionMeshBlocksCameraVolume(
    const CameraCollisionMesh& mesh, const Matrix3x4& world,
    std::array<double, 3>* scaled_extents = nullptr) {
  if (!mesh.local_bounds_valid) {
    return false;
  }
  constexpr double kMatrixScale = 1.0 / 16384.0;
  std::array<double, 3> extents{};
  for (size_t axis = 0; axis < extents.size(); ++axis) {
    const double local_span = static_cast<double>(
        static_cast<int64_t>(mesh.local_max[axis]) -
        static_cast<int64_t>(mesh.local_min[axis]));
    const size_t basis = axis * 3u;
    const double basis_length = std::hypot(
        std::hypot(static_cast<double>(world.values[basis]),
                   static_cast<double>(world.values[basis + 1u])),
        static_cast<double>(world.values[basis + 2u])) * kMatrixScale;
    extents[axis] = local_span * basis_length;
  }
  if (scaled_extents) {
    *scaled_extents = extents;
  }
  // Scene props need substantially more transverse mass than the camera
  // itself before they should steer the spring arm. One camera diameter made
  // the 309x1036x309 flag beside the door a blocker and produced a stable
  // two-object A/B collision cycle. Two diameters ignore that narrow clutter
  // while retaining walls and large moving blocks.
  return CameraMeshExtentsBlockVolume(
      extents, kCameraCollisionSphereRadius * 2.0);
}

void LogNonblockingCameraMesh(uintptr_t handle,
                              const std::array<double, 3>& extents) {
  if (g_debug_log &&
      g_camera_nonblocking_meshes_logged.insert(handle).second) {
    AppendNativeLog(
        "camera_mesh_nonblocking resource=%llu extents=%.1f/%.1f/%.1f "
        "required_two_axis_span=%.1f",
        static_cast<unsigned long long>(handle), extents[0], extents[1],
        extents[2], kCameraCollisionSphereRadius * 2.0);
  }
}

bool CameraCollisionNodeDrawsOwnResource(uintptr_t node,
                                         const NodeTransform& transform) {
  if ((transform.flags & kNodeSkipOwnRenderFlag) == 0u) {
    return true;
  }
  if (g_debug_log &&
      g_camera_renderer_skipped_nodes_logged.insert(node).second) {
    AppendNativeLog(
        "camera_mesh_renderer_skip node=%llu resource=%llu flags=%08X "
        "reason=NO_OWN_DRAW",
        static_cast<unsigned long long>(node),
        static_cast<unsigned long long>(transform.render_resource_handle),
        transform.flags);
  }
  return false;
}

double CameraVectorDot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 CameraVectorSubtract(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 CameraVectorAdd(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 CameraVectorScale(const Vec3& value, double scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}

Vec3 CameraVectorCross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

bool CameraMeshExpandedBoundsPushout(
    const CameraCollisionMesh& mesh, const Matrix3x4& world,
    const Vec3& point, const Vec3& reference, double radius,
    double margin, double minimum_distance, Vec3* pushed,
    size_t* pushed_axis = nullptr,
    const Vec3* continuity_reference = nullptr,
    bool prefer_contained_ray_exit = false,
    size_t preferred_axis = std::numeric_limits<size_t>::max(),
    double preferred_axis_hysteresis = 0.0) {
  if (!pushed || !mesh.local_bounds_valid ||
      !std::isfinite(radius) || radius <= 0.0) {
    return false;
  }

  constexpr double kMatrixScale = 1.0 / 16384.0;
  std::array<Vec3, 3> axes{};
  std::array<double, 3> point_coordinates{};
  std::array<double, 3> reference_coordinates{};
  std::array<double, 3> continuity_coordinates{};
  std::array<double, 3> half_extents{};
  Vec3 center{
      static_cast<double>(world.values[9]),
      static_cast<double>(world.values[10]),
      static_cast<double>(world.values[11])};
  for (size_t axis = 0; axis < axes.size(); ++axis) {
    const size_t basis = axis * 3u;
    const Vec3 scaled_axis{
        static_cast<double>(world.values[basis]) * kMatrixScale,
        static_cast<double>(world.values[basis + 1u]) * kMatrixScale,
        static_cast<double>(world.values[basis + 2u]) * kMatrixScale};
    const double axis_length = std::sqrt(
        CameraVectorDot(scaled_axis, scaled_axis));
    if (!std::isfinite(axis_length) || axis_length < 1.0e-6) {
      return false;
    }
    axes[axis] = CameraVectorScale(scaled_axis, 1.0 / axis_length);
    const double local_center =
        (static_cast<double>(mesh.local_min[axis]) +
         static_cast<double>(mesh.local_max[axis])) * 0.5;
    center = CameraVectorAdd(
        center, CameraVectorScale(scaled_axis, local_center));
    const double local_span =
        static_cast<double>(
            static_cast<int64_t>(mesh.local_max[axis]) -
            static_cast<int64_t>(mesh.local_min[axis]));
    half_extents[axis] =
        std::abs(local_span) * axis_length * 0.5 + radius;
  }

  const Vec3 point_offset = CameraVectorSubtract(point, center);
  const Vec3 reference_offset = CameraVectorSubtract(reference, center);
  const Vec3 continuity_offset = CameraVectorSubtract(
      continuity_reference ? *continuity_reference : reference, center);
  size_t vertical_axis = 0;
  double vertical_alignment = -1.0;
  for (size_t axis = 0; axis < axes.size(); ++axis) {
    point_coordinates[axis] =
        CameraVectorDot(point_offset, axes[axis]);
    reference_coordinates[axis] =
        CameraVectorDot(reference_offset, axes[axis]);
    continuity_coordinates[axis] =
        CameraVectorDot(continuity_offset, axes[axis]);
    const double alignment = std::abs(axes[axis].y);
    if (alignment > vertical_alignment) {
      vertical_alignment = alignment;
      vertical_axis = axis;
    }
  }

  std::array<double, 3> pushed_coordinates{};
  size_t selected_axis = axes.size();
  const bool pushed_valid =
      minimum_distance > 0.0
          ? ((prefer_contained_ray_exit &&
              PushCameraToUsableExpandedBoxRayExit(
                  point_coordinates, reference_coordinates, half_extents,
                  vertical_axis, margin, minimum_distance,
                  &pushed_coordinates, &selected_axis, preferred_axis,
                  preferred_axis_hysteresis)) ||
             PushCameraAlongExpandedBoxSupportingFace(
                 point_coordinates, reference_coordinates, half_extents,
                 vertical_axis, margin, minimum_distance,
                 &pushed_coordinates, &selected_axis) ||
             PushCameraToUsableExpandedBoxFace(
                 point_coordinates, continuity_coordinates, half_extents,
                 vertical_axis, margin, minimum_distance,
                 &pushed_coordinates, &selected_axis))
          : PushCameraOutOfExpandedBox(
                point_coordinates, reference_coordinates, half_extents,
                vertical_axis, margin, &pushed_coordinates, &selected_axis,
                preferred_axis, preferred_axis_hysteresis);
  if (!pushed_valid) {
    return false;
  }
  if (minimum_distance > 0.0 &&
      !PreserveCameraEscapePitch(
          point_coordinates, reference_coordinates, vertical_axis,
          &pushed_coordinates)) {
    return false;
  }

  Vec3 result = center;
  for (size_t axis = 0; axis < axes.size(); ++axis) {
    result = CameraVectorAdd(
        result,
        CameraVectorScale(axes[axis], pushed_coordinates[axis]));
  }
  *pushed = result;
  if (pushed_axis) {
    *pushed_axis = selected_axis;
  }
  return true;
}

bool CameraMeshContainedPivotRayExitDistance(
    const CameraCollisionMesh& mesh, const Matrix3x4& world,
    const Vec3& origin, const Vec3& direction, double radius,
    double margin, double maximum_distance, double* exit_distance,
    size_t* exit_axis = nullptr) {
  if (!exit_distance || !mesh.local_bounds_valid ||
      !std::isfinite(radius) || radius <= 0.0) {
    return false;
  }

  constexpr double kMatrixScale = 1.0 / 16384.0;
  std::array<double, 3> point_coordinates{};
  std::array<double, 3> direction_coordinates{};
  std::array<double, 3> half_extents{};
  Vec3 center{static_cast<double>(world.values[9]),
              static_cast<double>(world.values[10]),
              static_cast<double>(world.values[11])};
  std::array<Vec3, 3> axes{};
  for (size_t axis = 0; axis < axes.size(); ++axis) {
    const size_t basis = axis * 3u;
    const Vec3 scaled_axis{
        static_cast<double>(world.values[basis]) * kMatrixScale,
        static_cast<double>(world.values[basis + 1u]) * kMatrixScale,
        static_cast<double>(world.values[basis + 2u]) * kMatrixScale};
    const double axis_length =
        std::sqrt(CameraVectorDot(scaled_axis, scaled_axis));
    if (!std::isfinite(axis_length) || axis_length < 1.0e-6) {
      return false;
    }
    axes[axis] = CameraVectorScale(scaled_axis, 1.0 / axis_length);
    const double local_center =
        (static_cast<double>(mesh.local_min[axis]) +
         static_cast<double>(mesh.local_max[axis])) * 0.5;
    center = CameraVectorAdd(center,
                             CameraVectorScale(scaled_axis, local_center));
    const double local_span = static_cast<double>(
        static_cast<int64_t>(mesh.local_max[axis]) -
        static_cast<int64_t>(mesh.local_min[axis]));
    half_extents[axis] =
        std::abs(local_span) * axis_length * 0.5 + radius;
  }

  const Vec3 offset = CameraVectorSubtract(origin, center);
  for (size_t axis = 0; axis < axes.size(); ++axis) {
    point_coordinates[axis] = CameraVectorDot(offset, axes[axis]);
    direction_coordinates[axis] = CameraVectorDot(direction, axes[axis]);
  }
  const CameraExpandedBoxRayExit exit = FindContainedExpandedBoxRayExit(
      point_coordinates, direction_coordinates, half_extents, margin,
      maximum_distance);
  if (!exit.valid) {
    return false;
  }
  *exit_distance = exit.distance;
  if (exit_axis) {
    *exit_axis = exit.axis;
  }
  return true;
}

double CameraPointTriangleDistanceSquared(const Vec3& point, const Vec3& a,
                                          const Vec3& b, const Vec3& c) {
  // Closest-point regions from Real-Time Collision Detection. This catches a
  // sphere that begins a source tick already touching an edge or vertex, so
  // it cannot tunnel merely because the face plane lies behind the start.
  const Vec3 ab = CameraVectorSubtract(b, a);
  const Vec3 ac = CameraVectorSubtract(c, a);
  const Vec3 ap = CameraVectorSubtract(point, a);
  const double d1 = CameraVectorDot(ab, ap);
  const double d2 = CameraVectorDot(ac, ap);
  if (d1 <= 0.0 && d2 <= 0.0) {
    return CameraVectorDot(ap, ap);
  }
  const Vec3 bp = CameraVectorSubtract(point, b);
  const double d3 = CameraVectorDot(ab, bp);
  const double d4 = CameraVectorDot(ac, bp);
  if (d3 >= 0.0 && d4 <= d3) {
    return CameraVectorDot(bp, bp);
  }
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
    const double v = d1 / (d1 - d3);
    const Vec3 delta = CameraVectorSubtract(
        point, CameraVectorAdd(a, CameraVectorScale(ab, v)));
    return CameraVectorDot(delta, delta);
  }
  const Vec3 cp = CameraVectorSubtract(point, c);
  const double d5 = CameraVectorDot(ab, cp);
  const double d6 = CameraVectorDot(ac, cp);
  if (d6 >= 0.0 && d5 <= d6) {
    return CameraVectorDot(cp, cp);
  }
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
    const double w = d2 / (d2 - d6);
    const Vec3 delta = CameraVectorSubtract(
        point, CameraVectorAdd(a, CameraVectorScale(ac, w)));
    return CameraVectorDot(delta, delta);
  }
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
    const Vec3 bc = CameraVectorSubtract(c, b);
    const double w = (d4 - d3) /
        ((d4 - d3) + (d5 - d6));
    const Vec3 delta = CameraVectorSubtract(
        point, CameraVectorAdd(b, CameraVectorScale(bc, w)));
    return CameraVectorDot(delta, delta);
  }
  const double denominator = va + vb + vc;
  if (std::abs(denominator) < 1.0e-12) {
    return std::min({CameraVectorDot(ap, ap), CameraVectorDot(bp, bp),
                     CameraVectorDot(cp, cp)});
  }
  const double inverse = 1.0 / denominator;
  const double v = vb * inverse;
  const double w = vc * inverse;
  const Vec3 closest = CameraVectorAdd(
      a, CameraVectorAdd(CameraVectorScale(ab, v),
                         CameraVectorScale(ac, w)));
  const Vec3 delta = CameraVectorSubtract(point, closest);
  return CameraVectorDot(delta, delta);
}

bool CameraPointInsideTriangle(const Vec3& point, const Vec3& a,
                               const Vec3& b, const Vec3& c,
                               const Vec3& normal) {
  constexpr double epsilon = 1.0e-5;
  const Vec3 ab = CameraVectorSubtract(b, a);
  const Vec3 bc = CameraVectorSubtract(c, b);
  const Vec3 ca = CameraVectorSubtract(a, c);
  const double side0 = CameraVectorDot(
      CameraVectorCross(ab, CameraVectorSubtract(point, a)), normal);
  const double side1 = CameraVectorDot(
      CameraVectorCross(bc, CameraVectorSubtract(point, b)), normal);
  const double side2 = CameraVectorDot(
      CameraVectorCross(ca, CameraVectorSubtract(point, c)), normal);
  return (side0 >= -epsilon && side1 >= -epsilon && side2 >= -epsilon) ||
         (side0 <= epsilon && side1 <= epsilon && side2 <= epsilon);
}

bool CameraRaySphereDistance(const Vec3& origin, const Vec3& direction,
                             const Vec3& center, double radius,
                             double minimum_distance, double maximum_distance,
                             double* distance) {
  const Vec3 offset = CameraVectorSubtract(origin, center);
  const double b = CameraVectorDot(offset, direction);
  const double c = CameraVectorDot(offset, offset) - radius * radius;
  const double discriminant = b * b - c;
  if (discriminant < 0.0) {
    return false;
  }
  const double root = std::sqrt(std::max(0.0, discriminant));
  double candidate = -b - root;
  if (candidate < minimum_distance) {
    candidate = -b + root;
  }
  if (!std::isfinite(candidate) || candidate < minimum_distance ||
      candidate >= maximum_distance) {
    return false;
  }
  if (distance) {
    *distance = candidate;
  }
  return true;
}

bool CameraRayCapsuleDistance(const Vec3& origin, const Vec3& direction,
                              const Vec3& a, const Vec3& b, double radius,
                              double minimum_distance,
                              double maximum_distance, double* distance) {
  const Vec3 segment = CameraVectorSubtract(b, a);
  const Vec3 offset = CameraVectorSubtract(origin, a);
  const double segment_squared = CameraVectorDot(segment, segment);
  if (segment_squared < 1.0e-8) {
    return CameraRaySphereDistance(origin, direction, a, radius,
                                   minimum_distance, maximum_distance,
                                   distance);
  }
  const double segment_ray = CameraVectorDot(segment, direction);
  const double segment_offset = CameraVectorDot(segment, offset);
  const double ray_offset = CameraVectorDot(direction, offset);
  const double offset_squared = CameraVectorDot(offset, offset);
  const double qa = segment_squared - segment_ray * segment_ray;
  const double qb = segment_squared * ray_offset -
                    segment_offset * segment_ray;
  const double qc = segment_squared * offset_squared -
                    segment_offset * segment_offset -
                    radius * radius * segment_squared;
  double nearest = maximum_distance;
  bool hit = false;
  if (std::abs(qa) > 1.0e-8) {
    const double discriminant = qb * qb - qa * qc;
    if (discriminant >= 0.0) {
      const double candidate = (-qb - std::sqrt(discriminant)) / qa;
      const double segment_position = segment_offset +
                                      candidate * segment_ray;
      if (candidate >= minimum_distance && candidate < nearest &&
          segment_position > 0.0 && segment_position < segment_squared) {
        nearest = candidate;
        hit = true;
      }
    }
  }
  double endpoint = nearest;
  if (CameraRaySphereDistance(origin, direction, a, radius,
                              minimum_distance, nearest, &endpoint)) {
    nearest = endpoint;
    hit = true;
  }
  endpoint = nearest;
  if (CameraRaySphereDistance(origin, direction, b, radius,
                              minimum_distance, nearest, &endpoint)) {
    nearest = endpoint;
    hit = true;
  }
  if (hit && distance) {
    *distance = nearest;
  }
  return hit;
}

bool CameraSweptSphereTriangleDistance(
    const Vec3& origin, const Vec3& direction, const Vec3& a, const Vec3& b,
    const Vec3& c, double radius, double minimum_distance,
    double maximum_distance, double* distance,
    bool* initial_overlap = nullptr) {
  if (initial_overlap) {
    *initial_overlap = false;
  }
  double nearest = maximum_distance;
  bool hit = false;
  const Vec3 start = CameraVectorAdd(
      origin, CameraVectorScale(direction, minimum_distance));
  const double start_distance_squared =
      CameraPointTriangleDistanceSquared(start, a, b, c);
  if (start_distance_squared <= radius * radius) {
    constexpr double kOverlapDirectionProbe = 4.0;
    const double available_probe = maximum_distance - minimum_distance;
    const double probe_distance =
        std::min(kOverlapDirectionProbe, std::max(0.0, available_probe));
    const Vec3 probe = CameraVectorAdd(
        start, CameraVectorScale(direction, probe_distance));
    const double probe_distance_squared =
        CameraPointTriangleDistanceSquared(probe, a, b, c);
    if (!CameraInitialOverlapBlocks(start_distance_squared,
                                    probe_distance_squared)) {
      // A straight ray that is already moving away from (or tangentially to)
      // this convex triangle cannot hit it later. Do not reinterpret the exit
      // from an edge capsule as a new obstruction; that creates a one-way
      // overlap trap and prevents camera recovery from a prop corner.
      return false;
    }
    nearest = minimum_distance;
    hit = true;
    if (initial_overlap) {
      *initial_overlap = true;
    }
  }

  Vec3 normal = CameraVectorCross(CameraVectorSubtract(b, a),
                                  CameraVectorSubtract(c, a));
  const double normal_length = std::sqrt(CameraVectorDot(normal, normal));
  if (normal_length > 1.0e-8) {
    normal = CameraVectorScale(normal, 1.0 / normal_length);
    const double origin_plane = CameraVectorDot(
        CameraVectorSubtract(origin, a), normal);
    const double ray_plane = CameraVectorDot(direction, normal);
    if (std::abs(ray_plane) > 1.0e-8) {
      for (double target_plane : {radius, -radius}) {
        const double candidate =
            (target_plane - origin_plane) / ray_plane;
        if (candidate < minimum_distance || candidate >= nearest) {
          continue;
        }
        const Vec3 center = CameraVectorAdd(
            origin, CameraVectorScale(direction, candidate));
        const Vec3 contact = CameraVectorSubtract(
            center, CameraVectorScale(normal, target_plane));
        if (CameraPointInsideTriangle(contact, a, b, c, normal)) {
          nearest = candidate;
          hit = true;
        }
      }
    }
  }

  for (const auto& edge :
       {std::pair<Vec3, Vec3>{a, b}, {b, c}, {c, a}}) {
    double candidate = nearest;
    if (CameraRayCapsuleDistance(origin, direction, edge.first, edge.second,
                                 radius, minimum_distance, nearest,
                                 &candidate)) {
      nearest = candidate;
      hit = true;
    }
  }
  if (hit && distance) {
    *distance = nearest;
  }
  return hit;
}

bool CameraMeshSweepDistance(const CameraCollisionMesh& mesh,
                             const Matrix3x4& world, const Vec3& origin,
                             const Vec3& direction, double radius,
                             double minimum_distance,
                             double maximum_distance,
                             double* nearest_distance,
                             bool* initial_overlap = nullptr,
                             CameraMeshHitDiagnostic* diagnostic = nullptr) {
  if (initial_overlap) {
    *initial_overlap = false;
  }
  bool hit = false;
  bool any_initial_overlap = false;
  double nearest = maximum_distance;
  for (size_t triangle_index = 0; triangle_index < mesh.triangles.size();
       ++triangle_index) {
    const CameraMeshTriangle& triangle = mesh.triangles[triangle_index];
    const Vec3 a = CameraMeshPointToWorld(world, triangle.a);
    const Vec3 b = CameraMeshPointToWorld(world, triangle.b);
    const Vec3 c = CameraMeshPointToWorld(world, triangle.c);
    double candidate = nearest;
    bool triangle_initial_overlap = false;
    if (CameraSweptSphereTriangleDistance(
            origin, direction, a, b, c, radius, minimum_distance, nearest,
            &candidate, &triangle_initial_overlap)) {
      nearest = candidate;
      hit = true;
      if (diagnostic) {
        diagnostic->triangle_index = triangle_index;
        diagnostic->a = a;
        diagnostic->b = b;
        diagnostic->c = c;
        diagnostic->world = world;
        diagnostic->valid = true;
      }
      any_initial_overlap =
          any_initial_overlap || triangle_initial_overlap;
    }
  }
  if (hit && nearest_distance) {
    *nearest_distance = nearest;
  }
  if (initial_overlap) {
    *initial_overlap = any_initial_overlap;
  }
  return hit;
}

bool SweepOwnedCameraAgainstSceneMeshesInSnapshots(
    const SceneSnapshot& scene, const SceneSnapshot& stable_scene,
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& requested,
    OwnedCameraSceneSweepResult* result) {
  if (!result) {
    return false;
  }
  *result = {};
  result->position = requested;
  if (scene.nodes.empty() || stable_scene.nodes.empty() || !scene.player ||
      scene.root != stable_scene.root) {
    return false;
  }

  const Vec3 origin{static_cast<double>(focus[0]),
                    static_cast<double>(focus[1]),
                    static_cast<double>(focus[2])};
  const Vec3 endpoint{static_cast<double>(requested[0]),
                      static_cast<double>(requested[1]),
                      static_cast<double>(requested[2])};
  const Vec3 delta = CameraVectorSubtract(endpoint, origin);
  const double requested_distance =
      std::sqrt(CameraVectorDot(delta, delta));
  if (!std::isfinite(requested_distance) || requested_distance > 5000.0) {
    return false;
  }
  result->requested_distance = requested_distance;
  result->contact_distance = requested_distance;
  result->safe_distance = requested_distance;
  result->valid = true;
  // A fully collapsed arm is a valid direction-preserving near-pivot shot,
  // not a failed mesh query. There is no segment to sweep; the player-side
  // pivot is already the terminal endpoint and the player mesh is excluded.
  if (requested_distance < 1.0) {
    return true;
  }
  const Vec3 direction = CameraVectorScale(delta, 1.0 / requested_distance);

  constexpr double kContactBackoff = 8.0;
  double nearest_contact = requested_distance;
  CameraMeshHitDiagnostic nearest_diagnostic;
  bool nearest_initial_overlap = false;
  bool found = false;
  std::lock_guard<std::mutex> mesh_lock(g_camera_collision_mesh_mutex);
  for (const auto& entry : scene.nodes) {
    const uintptr_t node = entry.first;
    const NodeTransform& current = entry.second;
    if (!node || node == scene.root || node == scene.camera ||
        !current.render_resource_handle || !current.bounds_valid ||
        current.bounds_radius > kCameraCollisionMaximumObjectRadius ||
        !CameraCollisionNodeDrawsOwnResource(node, current)) {
      continue;
    }
    if (SceneNodeDescendsFrom(scene, node, scene.player) ||
        SceneNodeDescendsFrom(scene, scene.player, node)) {
      continue;
    }

    const auto stable = stable_scene.nodes.find(node);
    if (stable == stable_scene.nodes.end() ||
        !stable->second.bounds_valid ||
        stable->second.render_resource_handle !=
            current.render_resource_handle) {
      continue;
    }
    const double bounds_motion = CameraPositionDistance(
        current.bounds_center, stable->second.bounds_center);
    const double radius_motion =
        std::abs(static_cast<double>(current.bounds_radius) -
                 static_cast<double>(stable->second.bounds_radius));
    if (!std::isfinite(bounds_motion) ||
        radius_motion > kCameraCollisionRadiusMotionTolerance) {
      continue;
    }

    const Vec3 relative_center{
        static_cast<double>(current.bounds_center[0]) - origin.x,
        static_cast<double>(current.bounds_center[1]) - origin.y,
        static_cast<double>(current.bounds_center[2]) - origin.z};
    const double inflated_radius =
        static_cast<double>(current.bounds_radius) +
        kCameraCollisionSphereRadius;
    const double projection = CameraVectorDot(relative_center, direction);
    if (projection + inflated_radius <= 0.0 ||
        projection - inflated_radius >= nearest_contact) {
      continue;
    }
    const double center_distance_squared =
        CameraVectorDot(relative_center, relative_center);
    const double perpendicular_squared = std::max(
        0.0, center_distance_squared - projection * projection);
    if (!std::isfinite(perpendicular_squared) ||
        perpendicular_squared > inflated_radius * inflated_radius) {
      continue;
    }

    const CameraCollisionMesh* mesh =
        ResolveCameraCollisionMesh(current.render_resource_handle);
    if (!mesh) {
      continue;
    }
    double contact_distance = nearest_contact;
    bool initial_overlap = false;
    CameraMeshHitDiagnostic diagnostic;
    if (!CameraMeshSweepDistance(
            *mesh, current.world, origin, direction,
            kCameraCollisionSphereRadius, 0.0, nearest_contact,
            &contact_distance, &initial_overlap, &diagnostic)) {
      continue;
    }
    std::array<double, 3> scaled_extents{};
    if (!CameraCollisionMeshBlocksCameraVolume(
            *mesh, current.world, &scaled_extents)) {
      LogNonblockingCameraMesh(current.render_resource_handle,
                               scaled_extents);
      continue;
    }

    if (initial_overlap) {
      // A zero-distance triangle contact only says the 96-unit sphere around
      // the player-side pivot currently overlaps this render mesh. It does
      // not make every outward orbit ray invalid. Skip exactly the interval
      // needed to leave the mesh's expanded bounds, then sweep the remaining
      // ray against the real triangles. A later re-entry still blocks; a
      // clean exit cannot collapse the owned camera to the focus or force a
      // one-frame hybrid fallback.
      double pivot_exit_distance = 0.0;
      size_t pivot_exit_axis = std::numeric_limits<size_t>::max();
      if (CameraMeshContainedPivotRayExitDistance(
              *mesh, current.world, origin, direction,
              kCameraCollisionSphereRadius, kContactBackoff,
              nearest_contact, &pivot_exit_distance, &pivot_exit_axis)) {
        constexpr double kPostExitStartMargin = 1.0;
        const double post_exit_start = std::min(
            nearest_contact, pivot_exit_distance + kPostExitStartMargin);
        bool post_exit_hit = false;
        double post_exit_distance = nearest_contact;
        CameraMeshHitDiagnostic post_exit_diagnostic;
        if (post_exit_start + 0.5 < nearest_contact) {
          post_exit_hit = CameraMeshSweepDistance(
              *mesh, current.world, origin, direction,
              kCameraCollisionSphereRadius, post_exit_start,
              nearest_contact, &post_exit_distance, nullptr,
              &post_exit_diagnostic);
        }
        if (!post_exit_hit) {
          if (g_debug_log) {
            // A direct solution and its spring revalidation may inspect the
            // same contained pivot several times in one source tick. Keep a
            // state-change/heartbeat record without synchronously emitting
            // duplicate diagnostics for an identical clear exit.
            static uintptr_t last_clear_node = 0;
            static uint64_t last_clear_resource = 0;
            static size_t last_clear_axis =
                std::numeric_limits<size_t>::max();
            static uint64_t last_clear_tick = 0;
            const uint64_t source_tick =
                g_source_ticks.load(std::memory_order_relaxed);
            const bool changed = node != last_clear_node ||
                current.render_resource_handle != last_clear_resource ||
                pivot_exit_axis != last_clear_axis;
            if (changed || source_tick - last_clear_tick >= 30u) {
              AppendNativeLog(
                  "camera_owned_scene_pivot_exit result=CLEAR node=%08llX "
                  "resource=%llu exit=%.1f requested=%.1f axis=%llu",
                  static_cast<unsigned long long>(node),
                  static_cast<unsigned long long>(
                      current.render_resource_handle),
                  pivot_exit_distance, requested_distance,
                  static_cast<unsigned long long>(pivot_exit_axis));
              last_clear_node = node;
              last_clear_resource = current.render_resource_handle;
              last_clear_axis = pivot_exit_axis;
              last_clear_tick = source_tick;
            }
          }
          continue;
        }
        contact_distance = post_exit_distance;
        initial_overlap = false;
        diagnostic = post_exit_diagnostic;
        if (g_debug_log) {
          AppendNativeLog(
              "camera_owned_scene_pivot_exit result=REENTRY node=%08llX "
              "resource=%llu exit=%.1f contact=%.1f requested=%.1f "
              "axis=%llu",
              static_cast<unsigned long long>(node),
              static_cast<unsigned long long>(
                  current.render_resource_handle),
              pivot_exit_distance, contact_distance, requested_distance,
              static_cast<unsigned long long>(pivot_exit_axis));
        }
      }
    }

    nearest_contact = contact_distance;
    nearest_initial_overlap = initial_overlap;
    nearest_diagnostic = diagnostic;
    nearest_diagnostic.node = node;
    nearest_diagnostic.resource = current.render_resource_handle;
    nearest_diagnostic.bounds_center = current.bounds_center;
    nearest_diagnostic.bounds_radius = current.bounds_radius;
    nearest_diagnostic.bounds_motion = bounds_motion;
    nearest_diagnostic.initial_overlap = initial_overlap;
    nearest_diagnostic.overlap_pushout = false;
    nearest_diagnostic.near_pivot_escape = false;
    found = true;
  }

  if (!found) {
    return true;
  }
  result->blocked = true;
  result->initial_overlap = nearest_initial_overlap;
  result->contact_distance = nearest_contact;
  result->safe_distance = std::max(0.0, nearest_contact - kContactBackoff);
  result->position = {
      focus[0] + static_cast<int32_t>(
                       std::lround(direction.x * result->safe_distance)),
      focus[1] + static_cast<int32_t>(
                       std::lround(direction.y * result->safe_distance)),
      focus[2] + static_cast<int32_t>(
                       std::lround(direction.z * result->safe_distance))};
  result->diagnostic = nearest_diagnostic;
  return true;
}

bool SelectOwnedCameraCombinedPlan(
    const SceneSnapshot& scene, const SceneSnapshot& stable_scene,
    const std::array<int32_t, 3>& focus,
    const deathtrap_camera::RoomOrbitPlan& room_plan,
    double preferred_useful_distance, double switch_hysteresis_distance,
    double minimum_retained_distance, size_t previous_selected_index,
    uint32_t* direct_clear_ticks,
    OwnedCameraCombinedPlan* combined_plan) {
  if (!direct_clear_ticks || !combined_plan || !room_plan.valid ||
      room_plan.candidates.empty() ||
      !std::isfinite(preferred_useful_distance) ||
      preferred_useful_distance < 0.0 ||
      !std::isfinite(switch_hysteresis_distance) ||
      switch_hysteresis_distance < 0.0 ||
      !std::isfinite(minimum_retained_distance) ||
      minimum_retained_distance < 0.0) {
    return false;
  }
  *combined_plan = {};
  combined_plan->candidates.resize(room_plan.candidates.size());

  auto evaluate = [&](size_t index) {
    if (index >= room_plan.candidates.size()) {
      return false;
    }
    OwnedCameraCombinedCandidate& combined =
        combined_plan->candidates[index];
    if (combined.evaluated) {
      return true;
    }
    const auto& room = room_plan.candidates[index];
    combined.evaluated = true;
    combined.safe_distance = room.sweep.valid ? room.safe_distance : 0.0;
    if (!room.sweep.valid) {
      return true;
    }
    const std::array<int32_t, 3> room_position = {
        static_cast<int32_t>(std::lround(room.sweep.position.x)),
        static_cast<int32_t>(std::lround(room.sweep.position.y)),
        static_cast<int32_t>(std::lround(room.sweep.position.z))};
    if (!SweepOwnedCameraAgainstSceneMeshesInSnapshots(
            scene, stable_scene, focus, room_position, &combined.scene)) {
      return false;
    }
    if (combined.scene.blocked) {
      combined.safe_distance =
          std::min(combined.safe_distance, combined.scene.safe_distance);
    }
    return true;
  };

  if (!evaluate(0u)) {
    return false;
  }
  const double direct_quality = std::min(
      combined_plan->candidates[0].safe_distance,
      preferred_useful_distance);
  constexpr uint32_t kDirectReleaseTicks = 8u;
  const bool previous_avoidance_valid = previous_selected_index != 0u &&
      previous_selected_index < combined_plan->candidates.size();
  if (previous_avoidance_valid && !evaluate(previous_selected_index)) {
    return false;
  }
  const CameraAvoidanceLatchStep latch = StepCameraAvoidanceLatch(
      previous_avoidance_valid,
      combined_plan->candidates[0].safe_distance,
      previous_avoidance_valid
          ? combined_plan->candidates[previous_selected_index].safe_distance
          : 0.0,
      preferred_useful_distance, minimum_retained_distance,
      *direct_clear_ticks, kDirectReleaseTicks);
  *direct_clear_ticks = latch.direct_clear_ticks;
  if (latch.retain_previous) {
    combined_plan->selected_index = previous_selected_index;
    combined_plan->retained_previous = true;
    combined_plan->valid = true;
    return true;
  }
  if (direct_quality >= preferred_useful_distance) {
    combined_plan->selected_index = 0u;
    combined_plan->valid = true;
    return true;
  }

  size_t best_index = 0u;
  double best_quality = direct_quality;
  double best_cost = room_plan.candidates[0].angular_cost;
  std::vector<size_t> candidate_order;
  candidate_order.reserve(room_plan.candidates.size() - 1u);
  for (size_t index = 1u; index < room_plan.candidates.size(); ++index) {
    candidate_order.push_back(index);
  }
  std::stable_sort(
      candidate_order.begin(), candidate_order.end(),
      [&](size_t left, size_t right) {
        return room_plan.candidates[left].angular_cost <
            room_plan.candidates[right].angular_cost;
      });
  for (size_t index : candidate_order) {
    if (!evaluate(index)) {
      return false;
    }
    const auto& combined = combined_plan->candidates[index];
    const double quality =
        std::min(combined.safe_distance, preferred_useful_distance);
    const double cost = room_plan.candidates[index].angular_cost;
    const bool better_quality = quality > best_quality + 1.0e-6;
    const bool equal_quality = std::abs(quality - best_quality) <= 1.0e-6;
    if (better_quality || (equal_quality && cost < best_cost - 1.0e-9)) {
      best_index = index;
      best_quality = quality;
      best_cost = cost;
    }
    // Candidates are evaluated in increasing angular cost. Once a useful
    // full-quality shot is found, no later candidate can improve the primary
    // score or its angular tie-break. The prior side is evaluated separately
    // below for hysteresis.
    if (best_quality >= preferred_useful_distance) {
      break;
    }
  }

  if (best_index != 0u &&
      previous_selected_index < combined_plan->candidates.size()) {
    if (!evaluate(previous_selected_index)) {
      return false;
    }
    const auto& previous =
        combined_plan->candidates[previous_selected_index];
    const double previous_quality =
        std::min(previous.safe_distance, preferred_useful_distance);
    const bool escaping_collapsed_shot =
        previous.safe_distance < minimum_retained_distance &&
        best_quality > previous_quality + 1.0e-6;
    if (!escaping_collapsed_shot &&
        previous_quality + switch_hysteresis_distance >= best_quality) {
      best_index = previous_selected_index;
      combined_plan->retained_previous = true;
    }
  }

  combined_plan->selected_index = best_index;
  if (best_index == 0u) {
    *direct_clear_ticks = 0u;
  }
  combined_plan->valid = true;
  return true;
}

bool BuildOwnedCameraViewForward(
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& requested,
    std::array<double, 3>* view_forward) {
  if (!view_forward) {
    return false;
  }
  const double dx = static_cast<double>(requested[0] - focus[0]);
  const double dy = static_cast<double>(requested[1] - focus[1]);
  const double dz = static_cast<double>(requested[2] - focus[2]);
  const double distance = std::hypot(std::hypot(dx, dz), dy);
  if (!std::isfinite(distance) || distance < 1.0) {
    return false;
  }
  *view_forward = {-dx / distance, -dy / distance, -dz / distance};
  return true;
}

bool BuildDetachedCameraMatrices(
    const std::array<uint32_t, kCameraNodeProbeDwords>& live_node,
    const std::array<int32_t, 3>& position,
    const std::array<int32_t, 3>& angles,
    const std::array<uint32_t, 9>* local_transform_source,
    Matrix3x4* local, Matrix3x4* world) {
  if (!local || !world || !g_camera_node_local_update ||
      !g_camera_node_world_update) {
    return false;
  }

  // Preserve the camera's real parent so 0x3AC00 reads the same parent world
  // matrix, but sever every writable/external edge.  Resource and bounds work
  // then remains entirely inside this aligned stack copy and recursion cannot
  // reach a live child, sibling, callback or render resource.
  std::array<uint32_t, kCameraNodeProbeDwords> detached = live_node;
  uint8_t* const bytes = reinterpret_cast<uint8_t*>(detached.data());
  auto store = [&](size_t offset, const auto& value) {
    std::memcpy(bytes + offset, &value, sizeof(value));
  };
  const uintptr_t null_pointer = 0;
  const uintptr_t null_resource = 0;
  store(kChildOffset, null_pointer);
  store(kSiblingOffset, null_pointer);
  store(kRenderResourceHandleOffset, null_resource);
  for (const size_t offset : {0x40u, 0x44u, 0x48u, 0x4Cu, 0x50u}) {
    store(offset, null_pointer);
  }
  uint32_t flags = 0;
  std::memcpy(&flags, bytes + kNodeFlagsOffset, sizeof(flags));
  // Both routines cache completion in these bits.  Clear only their two
  // update stamps plus resource-side dirty flags on the detached copy.
  flags &= ~(0x00100000u | 0x00010000u | 0x00040000u | 0x00800000u);
  store(kNodeFlagsOffset, flags);
  if (local_transform_source) {
    std::memcpy(bytes, local_transform_source->data(),
                sizeof(*local_transform_source));
  }
  std::memcpy(bytes, position.data(), sizeof(position));
  std::memcpy(bytes + 0x18u, angles.data(), sizeof(angles));

  bool invoked = false;
  __try {
    g_camera_node_local_update(detached.data());
    g_camera_node_world_update(detached.data());
    invoked = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    invoked = false;
  }
  return invoked &&
      SafeRead(bytes + kLocalMatrixOffset, local, sizeof(*local)) &&
      SafeRead(bytes + kMatrixOffset, world, sizeof(*world));
}

struct OwnedCameraPublicationBackup {
  std::array<int32_t, 3> resolved{};
  std::array<int32_t, 3> desired{};
  std::array<int32_t, 3> history_average{};
  std::array<int32_t,
             kCameraControllerPositionHistorySampleCount * 3u>
      history_samples{};
  uintptr_t controller_sector = 0;
  std::array<int32_t, 3> node_position{};
  std::array<int32_t, 3> node_angles{};
  uintptr_t node_sector = 0;
  Matrix3x4 node_local{};
  Matrix3x4 node_world{};
  Matrix3x4 published{};
};

bool ReadOwnedCameraPublicationBackup(
    void* controller, uintptr_t camera_node,
    OwnedCameraPublicationBackup* backup) {
  if (!controller || !camera_node || !backup || !g_dungeon_base) {
    return false;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  return SafeRead(reinterpret_cast<const void*>(
                      base + kCameraControllerResolvedPositionOffset),
                  backup->resolved.data(), sizeof(backup->resolved)) &&
      SafeRead(reinterpret_cast<const void*>(
                   base + kCameraControllerDesiredPositionOffset),
               backup->desired.data(), sizeof(backup->desired)) &&
      SafeRead(reinterpret_cast<const void*>(
                   base + kCameraControllerPositionHistoryAverageOffset),
               backup->history_average.data(),
               sizeof(backup->history_average)) &&
      SafeRead(reinterpret_cast<const void*>(
                   base + kCameraControllerPositionHistorySamplesOffset),
               backup->history_samples.data(),
               sizeof(backup->history_samples)) &&
      SafeReadValue(reinterpret_cast<const void*>(
                        base + kCameraControllerEndpointSectorOffset),
                    &backup->controller_sector) &&
      SafeRead(reinterpret_cast<const void*>(camera_node),
               backup->node_position.data(),
               sizeof(backup->node_position)) &&
      SafeRead(reinterpret_cast<const void*>(camera_node + 0x18u),
               backup->node_angles.data(), sizeof(backup->node_angles)) &&
      SafeReadValue(reinterpret_cast<const void*>(
                        camera_node + kCameraNodeSectorOffset),
                    &backup->node_sector) &&
      SafeRead(reinterpret_cast<const void*>(
                   camera_node + kLocalMatrixOffset),
               &backup->node_local, sizeof(backup->node_local)) &&
      SafeRead(reinterpret_cast<const void*>(camera_node + kMatrixOffset),
               &backup->node_world, sizeof(backup->node_world)) &&
      SafeRead(g_dungeon_base + kPublishedCameraMatrixRva,
               &backup->published, sizeof(backup->published));
}

bool WriteOwnedCameraPublicationState(
    void* controller, uintptr_t camera_node,
    const std::array<int32_t, 3>& position,
    const std::array<int32_t, 3>& angles, uintptr_t sector,
    const Matrix3x4& local, const Matrix3x4& world) {
  if (!controller || !camera_node || !sector || !g_dungeon_base) {
    return false;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  bool written = SafeWrite(reinterpret_cast<void*>(camera_node),
                           position.data(), sizeof(position));
  written &= SafeWrite(reinterpret_cast<void*>(camera_node + 0x18u),
                       angles.data(), sizeof(angles));
  written &= SafeWrite(reinterpret_cast<void*>(
                           camera_node + kCameraNodeSectorOffset),
                       &sector, sizeof(sector));
  written &= SafeWrite(reinterpret_cast<void*>(
                           camera_node + kLocalMatrixOffset),
                       &local, sizeof(local));
  written &= SafeWrite(reinterpret_cast<void*>(camera_node + kMatrixOffset),
                       &world, sizeof(world));
  written &= SafeWrite(reinterpret_cast<void*>(
                           base + kCameraControllerResolvedPositionOffset),
                       position.data(), sizeof(position));
  written &= SafeWrite(reinterpret_cast<void*>(
                           base + kCameraControllerDesiredPositionOffset),
                       position.data(), sizeof(position));
  written &= SafeWrite(reinterpret_cast<void*>(
                           base +
                               kCameraControllerPositionHistoryAverageOffset),
                       position.data(), sizeof(position));
  for (size_t index = 0;
       index < kCameraControllerPositionHistorySampleCount; ++index) {
    written &= SafeWrite(
        reinterpret_cast<void*>(
            base + kCameraControllerPositionHistorySamplesOffset +
            index * kCameraControllerPositionHistorySampleStride),
        position.data(), sizeof(position));
  }
  written &= SafeWrite(reinterpret_cast<void*>(
                           base + kCameraControllerEndpointSectorOffset),
                       &sector, sizeof(sector));
  // The global render matrix is the publication boundary and is written last.
  written &= SafeWrite(g_dungeon_base + kPublishedCameraMatrixRva,
                       &world, sizeof(world));
  return written;
}

bool OwnedCameraPublicationMatches(
    void* controller, uintptr_t camera_node,
    const std::array<int32_t, 3>& position,
    const std::array<int32_t, 3>& angles, uintptr_t sector,
    const Matrix3x4& local, const Matrix3x4& world) {
  OwnedCameraPublicationBackup actual{};
  if (!ReadOwnedCameraPublicationBackup(controller, camera_node, &actual) ||
      actual.resolved != position || actual.desired != position ||
      actual.history_average != position ||
      actual.controller_sector != sector ||
      actual.node_position != position || actual.node_angles != angles ||
      actual.node_sector != sector ||
      std::memcmp(&actual.node_local, &local, sizeof(local)) != 0 ||
      std::memcmp(&actual.node_world, &world, sizeof(world)) != 0 ||
      std::memcmp(&actual.published, &world, sizeof(world)) != 0) {
    return false;
  }
  for (size_t index = 0;
       index < kCameraControllerPositionHistorySampleCount; ++index) {
    if (!std::equal(position.begin(), position.end(),
                    actual.history_samples.begin() + index * 3u)) {
      return false;
    }
  }
  return true;
}

bool RestoreOwnedCameraPublication(
    void* controller, uintptr_t camera_node,
    const OwnedCameraPublicationBackup& backup) {
  if (!controller || !camera_node || !g_dungeon_base) {
    return false;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  bool restored = SafeWrite(g_dungeon_base + kPublishedCameraMatrixRva,
                            &backup.published,
                            sizeof(backup.published));
  restored &= SafeWrite(reinterpret_cast<void*>(
                            base + kCameraControllerResolvedPositionOffset),
                        backup.resolved.data(), sizeof(backup.resolved));
  restored &= SafeWrite(reinterpret_cast<void*>(
                            base + kCameraControllerDesiredPositionOffset),
                        backup.desired.data(), sizeof(backup.desired));
  restored &= SafeWrite(
      reinterpret_cast<void*>(
          base + kCameraControllerPositionHistoryAverageOffset),
      backup.history_average.data(), sizeof(backup.history_average));
  restored &= SafeWrite(
      reinterpret_cast<void*>(
          base + kCameraControllerPositionHistorySamplesOffset),
      backup.history_samples.data(), sizeof(backup.history_samples));
  restored &= SafeWrite(reinterpret_cast<void*>(
                            base + kCameraControllerEndpointSectorOffset),
                        &backup.controller_sector,
                        sizeof(backup.controller_sector));
  restored &= SafeWrite(reinterpret_cast<void*>(camera_node),
                        backup.node_position.data(),
                        sizeof(backup.node_position));
  restored &= SafeWrite(reinterpret_cast<void*>(camera_node + 0x18u),
                        backup.node_angles.data(),
                        sizeof(backup.node_angles));
  restored &= SafeWrite(reinterpret_cast<void*>(
                            camera_node + kCameraNodeSectorOffset),
                        &backup.node_sector, sizeof(backup.node_sector));
  restored &= SafeWrite(reinterpret_cast<void*>(
                            camera_node + kLocalMatrixOffset),
                        &backup.node_local, sizeof(backup.node_local));
  restored &= SafeWrite(reinterpret_cast<void*>(camera_node + kMatrixOffset),
                        &backup.node_world, sizeof(backup.node_world));
  return restored;
}

bool OwnedCameraPublicationBackupMatches(
    void* controller, uintptr_t camera_node,
    const OwnedCameraPublicationBackup& expected) {
  OwnedCameraPublicationBackup actual{};
  return ReadOwnedCameraPublicationBackup(controller, camera_node, &actual) &&
      actual.resolved == expected.resolved &&
      actual.desired == expected.desired &&
      actual.history_average == expected.history_average &&
      actual.history_samples == expected.history_samples &&
      actual.controller_sector == expected.controller_sector &&
      actual.node_position == expected.node_position &&
      actual.node_angles == expected.node_angles &&
      actual.node_sector == expected.node_sector &&
      std::memcmp(&actual.node_local, &expected.node_local,
                  sizeof(actual.node_local)) == 0 &&
      std::memcmp(&actual.node_world, &expected.node_world,
                  sizeof(actual.node_world)) == 0 &&
      std::memcmp(&actual.published, &expected.published,
                  sizeof(actual.published)) == 0;
}

bool PublishOwnedCameraEndpoint(
    void* controller, const std::array<int32_t, 3>& collision_origin,
    const std::array<int32_t, 3>& look_target,
    const std::array<double, 3>* fixed_view_forward,
    const std::array<int32_t, 3>& combined_position,
    size_t start_sector_index, bool transition_cut) {
  if (!controller || !g_original_camera_look_at ||
      !g_camera_node_local_update || !g_camera_node_world_update ||
      !g_runtime_room_graph.valid ||
      start_sector_index >= g_runtime_room_graph.sectors.size()) {
    return false;
  }

  const deathtrap_camera::RoomVec3 room_focus{
      static_cast<double>(collision_origin[0]),
      static_cast<double>(collision_origin[1]),
      static_cast<double>(collision_origin[2])};
  const deathtrap_camera::RoomVec3 room_target{
      static_cast<double>(combined_position[0]),
      static_cast<double>(combined_position[1]),
      static_cast<double>(combined_position[2])};
  // Dynamic collision can shorten an endpoint before a portal crossing. Run
  // the final combined point through the room graph once more so the owned
  // publication sector belongs to the actual final centre, not the longer
  // static-only candidate.
  const deathtrap_camera::RoomSweepResult final_room =
      deathtrap_camera::SweepSphereThroughRooms(
          g_runtime_room_graph.sectors, start_sector_index, room_focus,
          room_target, kCameraCollisionSphereRadius, 8.0, 32u);
  if (!final_room.valid ||
      final_room.sector >= g_runtime_room_graph.sectors.size()) {
    AppendNativeLog(
        "camera_owned_publish_live valid=0 reason=FINAL_ROOM target=%d/%d/%d",
        combined_position[0], combined_position[1], combined_position[2]);
    return false;
  }
  const std::array<int32_t, 3> target = {
      static_cast<int32_t>(std::lround(final_room.position.x)),
      static_cast<int32_t>(std::lround(final_room.position.y)),
      static_cast<int32_t>(std::lround(final_room.position.z))};
  const uintptr_t graph_sector =
      g_runtime_room_graph.sector_base + final_room.sector * 0x3Cu;

  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  uintptr_t camera_owner = 0;
  uintptr_t camera_node = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerOwnerOffset),
                     &camera_owner) ||
      !camera_owner ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         camera_owner + kCameraNodeOffset),
                     &camera_node) ||
      !camera_node) {
    AppendNativeLog(
        "camera_owned_publish_live valid=0 reason=CAMERA_NODE target=%d/%d/%d",
        target[0], target[1], target[2]);
    return false;
  }

  std::array<uint32_t, kCameraNodeProbeDwords> camera_before{};
  std::array<uint32_t, kCameraNodeProbeDwords> camera_after{};
  Matrix3x4 published_before{};
  Matrix3x4 published_after{};
  if (!SafeRead(reinterpret_cast<const void*>(camera_node),
                camera_before.data(), sizeof(camera_before)) ||
      !SafeRead(g_dungeon_base + kPublishedCameraMatrixRva,
                &published_before, sizeof(published_before))) {
    AppendNativeLog(
        "camera_owned_publish_live valid=0 reason=CAMERA_READ node=%08llX",
        static_cast<unsigned long long>(camera_node));
    return false;
  }

  uintptr_t parent = 0;
  uintptr_t child = 0;
  uintptr_t sibling = 0;
  std::array<int32_t, 3> current_position{};
  std::array<int32_t, 3> current_angles{};
  std::memcpy(current_position.data(), camera_before.data(),
              sizeof(current_position));
  std::memcpy(current_angles.data(),
              reinterpret_cast<const uint8_t*>(camera_before.data()) + 0x18u,
              sizeof(current_angles));
  std::memcpy(&parent,
              reinterpret_cast<const uint8_t*>(camera_before.data()) +
                  kParentOffset,
              sizeof(parent));
  std::memcpy(&child,
              reinterpret_cast<const uint8_t*>(camera_before.data()) +
                  kChildOffset,
              sizeof(child));
  std::memcpy(&sibling,
              reinterpret_cast<const uint8_t*>(camera_before.data()) +
                  kSiblingOffset,
              sizeof(sibling));

  constexpr size_t kPlayerAuditBytes = 0x110u;
  std::array<uint8_t, kPlayerAuditBytes> player_before{};
  std::array<uint8_t, kPlayerAuditBytes> player_after{};
  const uintptr_t player_node = g_previous_snapshot.player;
  const bool player_read = player_node &&
      SafeRead(reinterpret_cast<const void*>(player_node),
               player_before.data(), player_before.size());

  const auto previous_camera = g_previous_snapshot.nodes.find(camera_node);
  const bool previous_pose_valid =
      previous_camera != g_previous_snapshot.nodes.end() &&
      previous_camera->second.pose_fields_valid;
  const std::array<int32_t, 3>& baseline_position =
      previous_pose_valid ? previous_camera->second.position
                          : current_position;
  const std::array<int32_t, 3>& baseline_angles =
      previous_pose_valid ? previous_camera->second.angles
                          : current_angles;

  std::array<int32_t, 3> publication_look_target = look_target;
  if (fixed_view_forward) {
    constexpr double kLookAheadDistance = 1400.0;
    const double forward_length = std::hypot(
        std::hypot((*fixed_view_forward)[0], (*fixed_view_forward)[2]),
        (*fixed_view_forward)[1]);
    if (!std::isfinite(forward_length) || forward_length < 0.5) {
      return false;
    }
    publication_look_target = {
        target[0] + static_cast<int32_t>(std::lround(
            (*fixed_view_forward)[0] / forward_length *
            kLookAheadDistance)),
        target[1] + static_cast<int32_t>(std::lround(
            (*fixed_view_forward)[1] / forward_length *
            kLookAheadDistance)),
        target[2] + static_cast<int32_t>(std::lround(
            (*fixed_view_forward)[2] / forward_length *
            kLookAheadDistance))};
  }

  std::array<int32_t, 3> owned_angles{};
  bool look_at_valid = false;
  __try {
    // Unlike the hybrid correction, the full-owned camera intentionally owns
    // its focus/look target.  The detached audit therefore uses the current
    // owned focus rather than borrowing a resolver-authored retail target.
    g_original_camera_look_at(target.data(), publication_look_target.data(),
                              owned_angles.data());
    look_at_valid = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    look_at_valid = false;
  }

  Matrix3x4 baseline_local{};
  Matrix3x4 baseline_world{};
  Matrix3x4 owned_local{};
  Matrix3x4 owned_world{};
  const bool baseline_built = BuildDetachedCameraMatrices(
      camera_before, baseline_position, baseline_angles,
      previous_pose_valid
          ? &previous_camera->second.local_transform_source
          : nullptr,
      &baseline_local, &baseline_world);
  const bool owned_built = look_at_valid && BuildDetachedCameraMatrices(
      camera_before, target, owned_angles, nullptr,
      &owned_local, &owned_world);

  uintptr_t retail_sector = 0;
  if (g_resolve_camera_sector) {
    __try {
      retail_sector = g_resolve_camera_sector(target.data(), graph_sector);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      retail_sector = 0;
    }
  }

  const uint8_t* const live_bytes =
      reinterpret_cast<const uint8_t*>(camera_before.data());
  Matrix3x4 live_world{};
  Matrix3x4 live_local{};
  std::memcpy(&live_world, live_bytes + kMatrixOffset, sizeof(live_world));
  std::memcpy(&live_local, live_bytes + kLocalMatrixOffset, sizeof(live_local));
  const Matrix3x4& expected_local = previous_pose_valid
      ? previous_camera->second.local : live_local;
  const Matrix3x4& expected_world = previous_pose_valid
      ? previous_camera->second.world : live_world;
  const bool baseline_local_exact = baseline_built &&
      std::memcmp(&baseline_local, &expected_local,
                  sizeof(expected_local)) == 0;
  const bool baseline_world_exact = baseline_built &&
      std::memcmp(&baseline_world, &expected_world,
                  sizeof(expected_world)) == 0;
  const bool published_exact =
      std::memcmp(&published_before, &live_world, sizeof(live_world)) == 0;
  const bool owned_translation_exact = owned_built &&
      std::equal(target.begin(), target.end(),
                 owned_world.values.begin() + 9);

  const bool camera_read_after =
      SafeRead(reinterpret_cast<const void*>(camera_node),
               camera_after.data(), sizeof(camera_after));
  const bool published_read_after =
      SafeRead(g_dungeon_base + kPublishedCameraMatrixRva,
               &published_after, sizeof(published_after));
  const bool player_read_after = player_read &&
      SafeRead(reinterpret_cast<const void*>(player_node),
               player_after.data(), player_after.size());
  const bool camera_untouched = camera_read_after &&
      std::memcmp(camera_before.data(), camera_after.data(),
                  sizeof(camera_before)) == 0;
  const bool published_untouched = published_read_after &&
      std::memcmp(&published_before, &published_after,
                  sizeof(published_before)) == 0;
  const bool player_untouched = player_read_after &&
      std::memcmp(player_before.data(), player_after.data(),
                  player_before.size()) == 0;

  // The published/live equality describes the state on entry. It is allowed
  // to be false on the first modern frame after a scene/camera transition:
  // the global matrix can still be the preceding exact render while the
  // retail probe has already updated the live node. The owned transaction
  // replaces both, so its gate is detached reproducibility and zero mutation,
  // not equality between two stale pre-publication owners.
  const bool audit_ready = baseline_local_exact && baseline_world_exact &&
      owned_translation_exact && camera_untouched && published_untouched &&
      player_untouched && camera_node != player_node && retail_sector &&
      retail_sector == graph_sector;
  OwnedCameraPublicationBackup publication_backup{};
  const bool backup_valid = audit_ready &&
      ReadOwnedCameraPublicationBackup(controller, camera_node,
                                       &publication_backup);
  bool committed = backup_valid && WriteOwnedCameraPublicationState(
      controller, camera_node, target, owned_angles, graph_sector,
      owned_local, owned_world);
  committed = committed && OwnedCameraPublicationMatches(
      controller, camera_node, target, owned_angles, graph_sector,
      owned_local, owned_world);
  std::array<uint8_t, kPlayerAuditBytes> player_committed{};
  const bool player_still_untouched = committed && player_read &&
      SafeRead(reinterpret_cast<const void*>(player_node),
               player_committed.data(), player_committed.size()) &&
      std::memcmp(player_before.data(), player_committed.data(),
                  player_before.size()) == 0;
  committed = committed && player_still_untouched;
  bool rollback_exact = true;
  if (backup_valid && !committed) {
    rollback_exact = RestoreOwnedCameraPublication(
        controller, camera_node, publication_backup) &&
        OwnedCameraPublicationBackupMatches(
            controller, camera_node, publication_backup);
  }
  if (committed && transition_cut) {
    g_owned_camera_presentation_cut_pending.store(
        true, std::memory_order_release);
  }
  static uint64_t audit_sequence = 0;
  ++audit_sequence;
  if (!committed || transition_cut || final_room.blocked ||
      (audit_sequence % 30u) == 1u) {
    AppendNativeLog(
        "camera_owned_publish_live valid=%u target=%d/%d/%d "
        "room_blocked=%u cut=%u sector=%08llX/%08llX node=%08llX "
        "parent=%08llX child=%08llX sibling=%08llX player=%08llX "
        "angles=%d/%d/%d->%d/%d/%d rebuild=%u/%u published=%u "
        "owned_translation=%u untouched=%u/%u/%u commit=%u rollback=%u",
        committed ? 1u : 0u, target[0], target[1], target[2],
        final_room.blocked ? 1u : 0u, transition_cut ? 1u : 0u,
        static_cast<unsigned long long>(graph_sector),
        static_cast<unsigned long long>(retail_sector),
        static_cast<unsigned long long>(camera_node),
        static_cast<unsigned long long>(parent),
        static_cast<unsigned long long>(child),
        static_cast<unsigned long long>(sibling),
        static_cast<unsigned long long>(player_node),
        current_angles[0], current_angles[1], current_angles[2],
        owned_angles[0], owned_angles[1], owned_angles[2],
        baseline_local_exact ? 1u : 0u,
        baseline_world_exact ? 1u : 0u,
        published_exact ? 1u : 0u,
        owned_translation_exact ? 1u : 0u,
        camera_untouched ? 1u : 0u,
        published_untouched ? 1u : 0u,
        player_untouched ? 1u : 0u,
        committed ? 1u : 0u, rollback_exact ? 1u : 0u);
  }
  return committed;
}

bool PublishImmersiveFirstPersonEndpoint(
    void* controller, const std::array<int32_t, 3>& camera_focus,
    uintptr_t room_or_sector, bool transition_cut) {
  if (!controller) {
    return false;
  }

  const double yaw = g_third_person_orbit_state.yaw;
  const double pitch = g_third_person_orbit_state.pitch;
  std::array<int32_t, 3> head_center{};
  uintptr_t head_node = 0;
  if (!ResolveLivePlayerHeadMount(g_previous_snapshot, camera_focus,
                                  &head_center, &head_node)) {
    ProbePlayerHeadJoints(
        g_previous_snapshot,
        g_source_ticks.load(std::memory_order_relaxed), true);
    AppendNativeLog(
        "camera_immersive_first_person valid=0 reason=HEAD_JOINT");
    return false;
  }
  const ImmersiveFirstPersonPose pose = BuildImmersiveHeadMountedPose(
      head_center, yaw, pitch, g_custom_head_height,
      g_custom_head_forward_offset);
  if (!pose.valid) {
    AppendNativeLog(
        "camera_immersive_first_person valid=0 reason=POSE yaw=%.2f "
        "pitch=%.2f",
        yaw * 180.0 / kOrbitPi, pitch * 180.0 / kOrbitPi);
    return false;
  }
  const std::array<double, 3> look_at_forward =
      ImmersiveFirstPersonLookAtVector(pose.forward);
  const std::array<int32_t, 3>& requested_eye = pose.eye;

  deathtrap_camera::RoomSweepResult room_sweep;
  std::array<int32_t, 3> safe_eye = requested_eye;
  size_t start_sector_index = std::numeric_limits<size_t>::max();
  if (!SweepOwnedCameraAgainstRooms(
          head_center, requested_eye, room_or_sector, &room_sweep, &safe_eye,
          nullptr, std::numeric_limits<size_t>::max(), &start_sector_index)) {
    AppendNativeLog("camera_immersive_first_person valid=0 reason=ROOM");
    return false;
  }

  OwnedCameraSceneSweepResult scene_sweep;
  const bool scene_valid = SweepOwnedCameraAgainstSceneMeshesInSnapshots(
      g_previous_snapshot, g_older_snapshot, head_center, safe_eye,
      &scene_sweep);
  if (scene_valid && scene_sweep.blocked) {
    safe_eye = scene_sweep.position;
  }

  const bool committed = PublishOwnedCameraEndpoint(
      controller, head_center, head_center, &look_at_forward, safe_eye,
      start_sector_index, transition_cut);
  static uint64_t head_sequence = 0;
  ++head_sequence;
  if (!committed || transition_cut || room_sweep.blocked ||
      (scene_valid && scene_sweep.blocked) ||
      (head_sequence % 30u) == 1u) {
    AppendNativeLog(
        "camera_immersive_first_person valid=%u eye=%d/%d/%d safe=%d/%d/%d "
        "head=%d/%d/%d node=%08llX yaw=%.2f pitch=%.2f "
        "room=%u scene=%u/%u cut=%u",
        committed ? 1u : 0u, requested_eye[0], requested_eye[1],
        requested_eye[2], safe_eye[0], safe_eye[1], safe_eye[2],
        head_center[0], head_center[1], head_center[2],
        static_cast<unsigned long long>(head_node),
        yaw * 180.0 / kOrbitPi, pitch * 180.0 / kOrbitPi,
        room_sweep.blocked ? 1u : 0u, scene_valid ? 1u : 0u,
        scene_valid && scene_sweep.blocked ? 1u : 0u,
        transition_cut ? 1u : 0u);
  }
  return committed;
}

// Source camera endpoints are collision-resolved independently, but the 50 Hz
// presentation path used to connect them with a straight Cartesian chord. An
// orbit turning around a prop corner can have two valid endpoints while that
// chord still passes through the prop. Test that temporal segment against the
// same render meshes used by the exact spring-arm resolver so synthetic frames
// never place the camera behind a one-sided surface.
bool CameraTemporalChordIntersectsSceneObjects(
    const SceneSnapshot& scene, const SceneSnapshot& stable_scene,
    const Vec3& origin, const Vec3& endpoint,
    CameraMeshHitDiagnostic* hit_diagnostic = nullptr,
    double* hit_distance = nullptr) {
  if (scene.nodes.empty() || stable_scene.nodes.empty() || !scene.player ||
      scene.root != stable_scene.root) {
    return false;
  }

  const Vec3 delta = CameraVectorSubtract(endpoint, origin);
  const double segment_distance =
      std::sqrt(CameraVectorDot(delta, delta));
  if (!std::isfinite(segment_distance) || segment_distance < 1.0 ||
      segment_distance > 5000.0) {
    return false;
  }
  const Vec3 direction = CameraVectorScale(delta, 1.0 / segment_distance);

  double nearest_distance = segment_distance;
  CameraMeshHitDiagnostic nearest_diagnostic;
  bool found = false;
  std::lock_guard<std::mutex> mesh_lock(g_camera_collision_mesh_mutex);
  for (const auto& entry : scene.nodes) {
    const uintptr_t node = entry.first;
    const NodeTransform& current = entry.second;
    if (!node || node == scene.root || node == scene.camera ||
        !current.render_resource_handle || !current.bounds_valid ||
        current.bounds_radius > kCameraCollisionMaximumObjectRadius ||
        !CameraCollisionNodeDrawsOwnResource(node, current)) {
      continue;
    }
    if (SceneNodeDescendsFrom(scene, node, scene.player) ||
        SceneNodeDescendsFrom(scene, scene.player, node)) {
      continue;
    }

    const auto stable = stable_scene.nodes.find(node);
    if (stable == stable_scene.nodes.end() ||
        !stable->second.bounds_valid ||
        stable->second.render_resource_handle !=
            current.render_resource_handle) {
      continue;
    }
    const double bounds_motion = CameraPositionDistance(
        current.bounds_center, stable->second.bounds_center);
    const double radius_motion =
        std::abs(static_cast<double>(current.bounds_radius) -
                 static_cast<double>(stable->second.bounds_radius));
    if (!std::isfinite(bounds_motion) ||
        radius_motion > kCameraCollisionRadiusMotionTolerance) {
      continue;
    }

    const Vec3 relative_center{
        static_cast<double>(current.bounds_center[0]) - origin.x,
        static_cast<double>(current.bounds_center[1]) - origin.y,
        static_cast<double>(current.bounds_center[2]) - origin.z};
    const double inflated_radius =
        static_cast<double>(current.bounds_radius) +
        kCameraCollisionSphereRadius;
    const double center_distance_squared =
        CameraVectorDot(relative_center, relative_center);
    if (!std::isfinite(center_distance_squared)) {
      continue;
    }
    const double projection = CameraVectorDot(relative_center, direction);
    if (projection + inflated_radius <= 0.0 ||
        projection - inflated_radius >= nearest_distance) {
      continue;
    }
    const double perpendicular_squared = std::max(
        0.0, center_distance_squared - projection * projection);
    if (perpendicular_squared > inflated_radius * inflated_radius) {
      continue;
    }

    const CameraCollisionMesh* mesh =
        ResolveCameraCollisionMesh(current.render_resource_handle);
    if (!mesh) {
      continue;
    }
    double candidate_distance = nearest_distance;
    CameraMeshHitDiagnostic candidate_diagnostic;
    if (!CameraMeshSweepDistance(
            *mesh, current.world, origin, direction,
            kCameraCollisionSphereRadius, 0.0, nearest_distance,
            &candidate_distance, nullptr, &candidate_diagnostic)) {
      continue;
    }
    std::array<double, 3> scaled_extents{};
    if (!CameraCollisionMeshBlocksCameraVolume(
            *mesh, current.world, &scaled_extents)) {
      LogNonblockingCameraMesh(current.render_resource_handle,
                               scaled_extents);
      continue;
    }
    nearest_distance = candidate_distance;
    nearest_diagnostic = candidate_diagnostic;
    nearest_diagnostic.node = node;
    nearest_diagnostic.resource = current.render_resource_handle;
    nearest_diagnostic.bounds_center = current.bounds_center;
    nearest_diagnostic.bounds_radius = current.bounds_radius;
    nearest_diagnostic.bounds_motion = bounds_motion;
    found = true;
  }

  if (!found) {
    return false;
  }
  if (hit_diagnostic) {
    *hit_diagnostic = nearest_diagnostic;
  }
  if (hit_distance) {
    *hit_distance = nearest_distance;
  }
  return true;
}

bool CameraEndpointClearOfSceneObjectTrianglesInSnapshots(
    const SceneSnapshot& scene, const SceneSnapshot& stable_scene,
    const std::array<int32_t, 3>& endpoint) {
  if (scene.nodes.empty() || stable_scene.nodes.empty() ||
      !scene.player || scene.root != stable_scene.root) {
    return false;
  }

  const Vec3 point{
      static_cast<double>(endpoint[0]),
      static_cast<double>(endpoint[1]),
      static_cast<double>(endpoint[2])};
  constexpr double kRadiusSquared =
      kCameraCollisionSphereRadius * kCameraCollisionSphereRadius;
  std::lock_guard<std::mutex> mesh_lock(g_camera_collision_mesh_mutex);
  for (const auto& entry : scene.nodes) {
    const uintptr_t node = entry.first;
    const NodeTransform& current = entry.second;
    if (!node || node == scene.root || node == scene.camera ||
        !current.render_resource_handle || !current.bounds_valid ||
        current.bounds_radius > kCameraCollisionMaximumObjectRadius ||
        !CameraCollisionNodeDrawsOwnResource(node, current)) {
      continue;
    }
    if (SceneNodeDescendsFrom(scene, node, scene.player) ||
        SceneNodeDescendsFrom(scene, scene.player, node)) {
      continue;
    }

    const auto stable = stable_scene.nodes.find(node);
    if (stable == stable_scene.nodes.end() ||
        !stable->second.bounds_valid ||
        stable->second.render_resource_handle !=
            current.render_resource_handle) {
      continue;
    }
    const double radius_motion =
        std::abs(static_cast<double>(current.bounds_radius) -
                 static_cast<double>(stable->second.bounds_radius));
    if (radius_motion > kCameraCollisionRadiusMotionTolerance) {
      continue;
    }

    const Vec3 bounds_delta{
        point.x - current.bounds_center[0],
        point.y - current.bounds_center[1],
        point.z - current.bounds_center[2]};
    const double inflated_radius =
        static_cast<double>(current.bounds_radius) +
        kCameraCollisionSphereRadius;
    if (CameraVectorDot(bounds_delta, bounds_delta) >
        inflated_radius * inflated_radius) {
      continue;
    }

    const CameraCollisionMesh* mesh =
        ResolveCameraCollisionMesh(current.render_resource_handle);
    if (!mesh) {
      continue;
    }
    std::array<double, 3> scaled_extents{};
    if (!CameraCollisionMeshBlocksCameraVolume(
            *mesh, current.world, &scaled_extents)) {
      continue;
    }

    // Endpoint acceptance must use the same real render triangles as the
    // preceding swept-sphere clip. The expanded OBB is only a conservative
    // broad phase and may contain large empty regions (the flag/door resource
    // is the captured example). Treating OBB containment as final occupancy
    // rejects the clip's own 8-unit-backed-off boundary, so pre-history passes
    // the unsafe candidate while post-native commits the safe one forever.
    // A complete focus-to-endpoint sweep already rejects actual closed-mesh
    // entry; this final check only guards integer rounding at the endpoint.
    for (const CameraMeshTriangle& triangle : mesh->triangles) {
      const Vec3 a = CameraMeshPointToWorld(current.world, triangle.a);
      const Vec3 b = CameraMeshPointToWorld(current.world, triangle.b);
      const Vec3 c = CameraMeshPointToWorld(current.world, triangle.c);
      if (CameraPointTriangleDistanceSquared(point, a, b, c) <=
          kRadiusSquared) {
        return false;
      }
    }
  }
  return true;
}

bool CameraEndpointClearOfSceneObjectTriangles(
    const std::array<int32_t, 3>& endpoint) {
  return CameraEndpointClearOfSceneObjectTrianglesInSnapshots(
      g_previous_snapshot, g_older_snapshot, endpoint);
}

bool ClipThirdPersonOrbitAgainstSceneObjectsInSnapshots(
    const SceneSnapshot& scene, const SceneSnapshot& stable_scene,
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& requested,
    std::array<int32_t, 3>* clipped,
    CameraMeshHitDiagnostic* hit_diagnostic = nullptr,
    const CameraMeshContactPreference* contact_preference = nullptr,
    bool emit_log = true) {
  if (!clipped || scene.nodes.empty() || stable_scene.nodes.empty() ||
      !scene.player || scene.root != stable_scene.root) {
    return false;
  }

  const double ray_x = static_cast<double>(requested[0] - focus[0]);
  const double ray_y = static_cast<double>(requested[1] - focus[1]);
  const double ray_z = static_cast<double>(requested[2] - focus[2]);
  const double requested_distance =
      std::hypot(std::hypot(ray_x, ray_z), ray_y);
  if (!std::isfinite(requested_distance) || requested_distance < 1.0 ||
      requested_distance > 5000.0) {
    return false;
  }
  const std::array<double, 3> direction = {
      ray_x / requested_distance, ray_y / requested_distance,
      ray_z / requested_distance};

  // The retail room resolver traverses BSP cells and portals only. Visible
  // props such as lever housings and stairs may not participate in that
  // structure at all. node+0x80 remains a useful broad phase, but the actual
  // hit is now calculated against the original render polygons referenced by
  // node+0x3C. This avoids both classes of sphere failure: a concave sphere
  // containing the player, and a large decorative object whose sphere was
  // rejected by the old 900-unit cap.
  // The camera is a volume, not a point. Runtime 0.0.78 proved that a 64-unit
  // sphere could leave the camera centre outside resource 12708 while a
  // near-plane corner still intersected its triangle edge: the last accepted
  // centre was only about 66 units from that edge. Restore the 96-unit volume
  // originally validated in 0.0.70-0.0.73. The sweep is now a veto on the
  // complete native result: a blocked probe is restored and a shorter target
  // is resubmitted through 0x2F380, so wall/floor/orientation ownership stays
  // native and no camera matrix is overwritten.
  constexpr double kContactBackoff = 8.0;
  constexpr double kBroadPhaseInflation = kCameraCollisionSphereRadius;
  constexpr double kSweepStartDistance = 0.0;

  double nearest_distance = requested_distance;
  // The complete desired spring arm is queried every source tick. Geometry
  // beyond its endpoint is not an obstruction and must not keep a previously
  // contracted arm latched.
  double nearest_surface_distance = requested_distance;
  uintptr_t nearest_node = 0;
  uintptr_t nearest_resource = 0;
  double nearest_object_radius = 0.0;
  size_t nearest_triangle_count = 0;
  bool nearest_initial_overlap = false;
  bool nearest_nonradial_pushout = false;
  size_t nearest_preferred_axis = std::numeric_limits<size_t>::max();
  std::array<int32_t, 3> nearest_target = requested;
  CameraMeshHitDiagnostic nearest_diagnostic;
  const Vec3 origin{static_cast<double>(focus[0]),
                    static_cast<double>(focus[1]),
                    static_cast<double>(focus[2])};
  const Vec3 ray_direction{direction[0], direction[1], direction[2]};
  const Vec3 requested_reference{
      static_cast<double>(requested[0]),
      static_cast<double>(requested[1]),
      static_cast<double>(requested[2])};
  Vec3 overlap_reference = requested_reference;
  const auto previous_camera = scene.nodes.find(scene.camera);
  if (previous_camera != scene.nodes.end()) {
    overlap_reference = {
        static_cast<double>(previous_camera->second.world.values[9]),
        static_cast<double>(previous_camera->second.world.values[10]),
        static_cast<double>(previous_camera->second.world.values[11])};
  }
  std::lock_guard<std::mutex> mesh_lock(g_camera_collision_mesh_mutex);
  for (const auto& entry : scene.nodes) {
    const uintptr_t node = entry.first;
    const NodeTransform& current = entry.second;
    if (!node || node == scene.root || node == scene.camera ||
        !current.render_resource_handle || !current.bounds_valid ||
        current.bounds_radius > kCameraCollisionMaximumObjectRadius ||
        !CameraCollisionNodeDrawsOwnResource(node, current)) {
      continue;
    }

    // Ignore Lara and all of her mesh/bone nodes. Also ignore drawable room
    // ancestors that contain Lara; their aggregate geometry is already owned
    // by the native room collision and can include transitional scene data.
    if (SceneNodeDescendsFrom(scene, node, scene.player) ||
        SceneNodeDescendsFrom(scene, scene.player, node)) {
      continue;
    }

    const auto older = stable_scene.nodes.find(node);
    if (older == stable_scene.nodes.end() ||
        !older->second.bounds_valid ||
        older->second.render_resource_handle !=
            current.render_resource_handle) {
      continue;
    }
    const double bounds_motion = CameraPositionDistance(
        current.bounds_center, older->second.bounds_center);
    const double radius_motion =
        std::abs(static_cast<double>(current.bounds_radius) -
                 static_cast<double>(older->second.bounds_radius));
    if (!std::isfinite(bounds_motion) ||
        radius_motion > kCameraCollisionRadiusMotionTolerance) {
      continue;
    }

    const double center_x =
        static_cast<double>(current.bounds_center[0] - focus[0]);
    const double center_y =
        static_cast<double>(current.bounds_center[1] - focus[1]);
    const double center_z =
        static_cast<double>(current.bounds_center[2] - focus[2]);
    const double inflated_radius =
        static_cast<double>(current.bounds_radius) + kBroadPhaseInflation;
    const double center_distance_squared =
        center_x * center_x + center_y * center_y + center_z * center_z;
    if (!std::isfinite(center_distance_squared)) {
      continue;
    }
    const double projection = center_x * direction[0] +
                              center_y * direction[1] +
                              center_z * direction[2];
    if (projection + inflated_radius <= kSweepStartDistance ||
        projection - inflated_radius >= nearest_surface_distance) {
      continue;
    }
    const double perpendicular_squared = std::max(
        0.0, center_distance_squared - projection * projection);
    const double radius_squared = inflated_radius * inflated_radius;
    if (perpendicular_squared > radius_squared) {
      continue;
    }

    const CameraCollisionMesh* mesh =
        ResolveCameraCollisionMesh(current.render_resource_handle);
    if (!mesh) {
      continue;
    }
    double surface_distance = nearest_surface_distance;
    bool mesh_initial_overlap = false;
    CameraMeshHitDiagnostic candidate_diagnostic;
    if (!CameraMeshSweepDistance(
            *mesh, current.world, origin, ray_direction,
            kCameraCollisionSphereRadius, kSweepStartDistance,
            nearest_surface_distance, &surface_distance,
            &mesh_initial_overlap, &candidate_diagnostic)) {
      continue;
    }
    std::array<double, 3> scaled_extents{};
    if (!CameraCollisionMeshBlocksCameraVolume(
            *mesh, current.world, &scaled_extents)) {
      LogNonblockingCameraMesh(current.render_resource_handle,
                               scaled_extents);
      continue;
    }
    candidate_diagnostic.initial_overlap = mesh_initial_overlap;
    constexpr double kSupportingAxisHysteresis =
        kCameraCollisionSphereRadius;
    const bool matching_preference =
        contact_preference && contact_preference->valid &&
        contact_preference->node == node &&
        contact_preference->resource == current.render_resource_handle;
    const size_t preferred_axis =
        matching_preference
            ? contact_preference->axis
            : std::numeric_limits<size_t>::max();
    if (mesh_initial_overlap) {
      // `initial_overlap` means the pivot-side camera sphere touches at least
      // one triangle. It does not prove that the requested endpoint is inside
      // the object. Large or hollow render resources can conservatively
      // contain the player-side pivot in their expanded OBB; treating that
      // containment as a zero-distance hit makes every orbit direction choose
      // one supporting face and permanently pins the camera.
      //
      // Remove only the interval in which the ray leaves that expanded OBB,
      // then sweep the remaining segment against the real triangles again. A
      // later re-entry is still an ordinary obstruction, while a clean exit
      // is not converted into a synthetic face contact. The same accepted
      // boundary is consequently idempotent on the next source tick.
      double pivot_exit_distance = 0.0;
      size_t pivot_exit_axis = std::numeric_limits<size_t>::max();
      if (CameraMeshContainedPivotRayExitDistance(
              *mesh, current.world, origin, ray_direction,
              kCameraCollisionSphereRadius, kContactBackoff,
              nearest_surface_distance, &pivot_exit_distance,
              &pivot_exit_axis)) {
        constexpr double kPostExitStartMargin = 1.0;
        const double post_exit_start = std::min(
            nearest_surface_distance,
            pivot_exit_distance + kPostExitStartMargin);
        bool post_exit_hit = false;
        double post_exit_distance = nearest_surface_distance;
        CameraMeshHitDiagnostic post_exit_diagnostic;
        if (post_exit_start + 0.5 < nearest_surface_distance) {
          post_exit_hit = CameraMeshSweepDistance(
              *mesh, current.world, origin, ray_direction,
              kCameraCollisionSphereRadius, post_exit_start,
              nearest_surface_distance, &post_exit_distance, nullptr,
              &post_exit_diagnostic);
        }
        if (!post_exit_hit) {
          if (g_debug_log && emit_log) {
            AppendNativeLog(
                "camera_mesh_pivot_exit result=CLEAR node=%08llX "
                "resource=%llu exit=%.1f requested=%.1f axis=%llu",
                static_cast<unsigned long long>(node),
                static_cast<unsigned long long>(
                    current.render_resource_handle),
                pivot_exit_distance, requested_distance,
                static_cast<unsigned long long>(pivot_exit_axis));
          }
          continue;
        }
        surface_distance = post_exit_distance;
        mesh_initial_overlap = false;
        candidate_diagnostic = post_exit_diagnostic;
        candidate_diagnostic.initial_overlap = false;
        if (g_debug_log && emit_log) {
          AppendNativeLog(
              "camera_mesh_pivot_exit result=REENTRY node=%08llX "
              "resource=%llu exit=%.1f contact=%.1f requested=%.1f "
              "axis=%llu",
              static_cast<unsigned long long>(node),
              static_cast<unsigned long long>(
                  current.render_resource_handle),
              pivot_exit_distance, surface_distance, requested_distance,
              static_cast<unsigned long long>(pivot_exit_axis));
        }
      }
    }
    if (mesh_initial_overlap) {
      // A radial spring has no valid inward endpoint when the expanded object
      // already contains the player-side pivot. Collapsing to radius zero puts
      // the near plane on the same polygons and produces the observed black
      // texture. Resolve this exceptional topology like the maintained Tomb
      // camera: push the camera volume through the nearest horizontal face of
      // the object's oriented bounds, choosing the side of the previous
      // camera for continuity. Room collision validates the result later.
      constexpr double kOverlapPushoutMargin = 8.0;
      Vec3 pushed{};
      size_t pushout_axis = std::numeric_limits<size_t>::max();
      if (!CameraMeshExpandedBoundsPushout(
              *mesh, current.world, origin, overlap_reference,
              kCameraCollisionSphereRadius, kOverlapPushoutMargin,
              0.0, &pushed, &pushout_axis, nullptr, false,
              preferred_axis, kSupportingAxisHysteresis)) {
        continue;
      }
      double push_distance = std::sqrt(
          CameraVectorDot(CameraVectorSubtract(pushed, origin),
                          CameraVectorSubtract(pushed, origin)));
      bool near_pivot_escape = false;
      if (std::isfinite(push_distance) &&
          push_distance < kThirdPersonMinimumCameraDistance) {
        Vec3 usable_face{};
        size_t usable_axis = std::numeric_limits<size_t>::max();
        // A contained pivot must remain responsive to orbit input. Exit the
        // expanded OBB along the requested horizontal ray and continue on
        // that same ray to the usable-distance boundary. If the pivot is
        // already outside one face, retain the preceding accepted side as the
        // supporting-face fallback instead of crossing back through the box.
        if (CameraMeshExpandedBoundsPushout(
                *mesh, current.world, origin, requested_reference,
                kCameraCollisionSphereRadius, kOverlapPushoutMargin,
                kThirdPersonMinimumCameraDistance, &usable_face,
                &usable_axis, &overlap_reference, true, preferred_axis,
                kSupportingAxisHysteresis)) {
          const double usable_distance = std::sqrt(
              CameraVectorDot(
                  CameraVectorSubtract(usable_face, origin),
                  CameraVectorSubtract(usable_face, origin)));
          if (std::isfinite(usable_distance) &&
              usable_distance <= requested_distance) {
            pushed = usable_face;
            pushout_axis = usable_axis;
            push_distance = usable_distance;
            near_pivot_escape = true;
          }
        }
      }
      if (!std::isfinite(push_distance) ||
          (nearest_nonradial_pushout &&
           push_distance >= nearest_distance)) {
        continue;
      }
      nearest_distance = push_distance;
      nearest_target = {
          static_cast<int32_t>(std::lround(pushed.x)),
          static_cast<int32_t>(std::lround(pushed.y)),
          static_cast<int32_t>(std::lround(pushed.z))};
      nearest_node = node;
      nearest_resource = current.render_resource_handle;
      nearest_object_radius = static_cast<double>(current.bounds_radius);
      nearest_triangle_count = mesh->triangles.size();
      nearest_initial_overlap = true;
      nearest_nonradial_pushout = true;
      nearest_preferred_axis = preferred_axis;
      nearest_diagnostic = candidate_diagnostic;
      nearest_diagnostic.node = node;
      nearest_diagnostic.resource = current.render_resource_handle;
      nearest_diagnostic.bounds_center = current.bounds_center;
      nearest_diagnostic.bounds_radius = current.bounds_radius;
      nearest_diagnostic.bounds_motion = bounds_motion;
      nearest_diagnostic.initial_overlap = true;
      nearest_diagnostic.overlap_pushout = true;
      nearest_diagnostic.near_pivot_escape = near_pivot_escape;
      nearest_diagnostic.pushout_axis = pushout_axis;
      continue;
    }
    if (nearest_nonradial_pushout) {
      continue;
    }

    const double safe_distance =
        std::max(0.0, surface_distance - kContactBackoff);
    if (safe_distance < kThirdPersonMinimumCameraDistance) {
      constexpr double kNearPivotEscapeMargin = 8.0;
      Vec3 escaped{};
      size_t escape_axis = std::numeric_limits<size_t>::max();
      // The same contained-pivot ray exit avoids both opposite-face snapping
      // and the fixed-face anchor exposed by 0.0.107. The preceding accepted
      // camera is used only when an already-outside pivot needs a supporting
      // face to route around the object.
      if (CameraMeshExpandedBoundsPushout(
              *mesh, current.world, origin, requested_reference,
              kCameraCollisionSphereRadius, kNearPivotEscapeMargin,
              kThirdPersonMinimumCameraDistance, &escaped,
              &escape_axis, &overlap_reference, true, preferred_axis,
              kSupportingAxisHysteresis)) {
        const double escape_distance = std::sqrt(
            CameraVectorDot(CameraVectorSubtract(escaped, origin),
                            CameraVectorSubtract(escaped, origin)));
        if (std::isfinite(escape_distance) &&
            escape_distance <= requested_distance) {
          nearest_distance = escape_distance;
          nearest_target = {
              static_cast<int32_t>(std::lround(escaped.x)),
              static_cast<int32_t>(std::lround(escaped.y)),
              static_cast<int32_t>(std::lround(escaped.z))};
          nearest_node = node;
          nearest_resource = current.render_resource_handle;
          nearest_object_radius =
              static_cast<double>(current.bounds_radius);
          nearest_triangle_count = mesh->triangles.size();
          nearest_initial_overlap = false;
          nearest_nonradial_pushout = true;
          nearest_preferred_axis = preferred_axis;
          nearest_diagnostic = candidate_diagnostic;
          nearest_diagnostic.node = node;
          nearest_diagnostic.resource =
              current.render_resource_handle;
          nearest_diagnostic.bounds_center = current.bounds_center;
          nearest_diagnostic.bounds_radius = current.bounds_radius;
          nearest_diagnostic.bounds_motion = bounds_motion;
          nearest_diagnostic.initial_overlap = false;
          nearest_diagnostic.overlap_pushout = false;
          nearest_diagnostic.near_pivot_escape = true;
          nearest_diagnostic.pushout_axis = escape_axis;
          continue;
        }
      }
    }
    nearest_distance = std::min(nearest_distance, safe_distance);
    nearest_surface_distance = surface_distance;
    nearest_target = {
        focus[0] + static_cast<int32_t>(
                        std::lround(direction[0] * nearest_distance)),
        focus[1] + static_cast<int32_t>(
                        std::lround(direction[1] * nearest_distance)),
        focus[2] + static_cast<int32_t>(
                        std::lround(direction[2] * nearest_distance))};
    nearest_node = node;
    nearest_resource = current.render_resource_handle;
    nearest_object_radius = static_cast<double>(current.bounds_radius);
    nearest_triangle_count = mesh->triangles.size();
    nearest_initial_overlap = mesh_initial_overlap;
    nearest_preferred_axis = std::numeric_limits<size_t>::max();
    nearest_diagnostic = candidate_diagnostic;
    nearest_diagnostic.node = node;
    nearest_diagnostic.resource = current.render_resource_handle;
    nearest_diagnostic.bounds_center = current.bounds_center;
    nearest_diagnostic.bounds_radius = current.bounds_radius;
    nearest_diagnostic.bounds_motion = bounds_motion;
    nearest_diagnostic.initial_overlap = false;
    nearest_diagnostic.overlap_pushout = false;
    nearest_diagnostic.near_pivot_escape = false;
  }

  if (!nearest_node) {
    return false;
  }
  *clipped = nearest_target;
  if (hit_diagnostic) {
    *hit_diagnostic = nearest_diagnostic;
  }
  if (g_debug_log && emit_log) {
    AppendNativeLog(
        "camera_mesh_sweep node=%08llX resource=%llu triangles=%llu "
        "object_radius=%.1f contact=%.1f sphere=%.1f camera_radius=%.1f "
        "requested=%.1f overlap=%u pushout=%u escape=%u axis=%lld "
        "preferred_axis=%lld motion=%.1f",
        static_cast<unsigned long long>(nearest_node),
        static_cast<unsigned long long>(nearest_resource),
        static_cast<unsigned long long>(nearest_triangle_count),
        nearest_object_radius, nearest_surface_distance,
        kCameraCollisionSphereRadius, nearest_distance, requested_distance,
        nearest_initial_overlap ? 1u : 0u,
        nearest_diagnostic.overlap_pushout ? 1u : 0u,
        nearest_diagnostic.near_pivot_escape ? 1u : 0u,
        nearest_nonradial_pushout
            ? static_cast<long long>(nearest_diagnostic.pushout_axis)
            : -1ll,
        nearest_preferred_axis < 3u
            ? static_cast<long long>(nearest_preferred_axis)
            : -1ll,
        nearest_diagnostic.bounds_motion);
    if (nearest_diagnostic.valid) {
      AppendNativeLog(
          "camera_mesh_hit node=%08llX resource=%llu tri=%llu "
          "world_t=%.0f/%.0f/%.0f bounds=%d/%d/%d/r%d "
          "a=%.0f/%.0f/%.0f b=%.0f/%.0f/%.0f c=%.0f/%.0f/%.0f",
          static_cast<unsigned long long>(nearest_diagnostic.node),
          static_cast<unsigned long long>(nearest_diagnostic.resource),
          static_cast<unsigned long long>(nearest_diagnostic.triangle_index),
          static_cast<double>(nearest_diagnostic.world.values[9]),
          static_cast<double>(nearest_diagnostic.world.values[10]),
          static_cast<double>(nearest_diagnostic.world.values[11]),
          nearest_diagnostic.bounds_center[0],
          nearest_diagnostic.bounds_center[1],
          nearest_diagnostic.bounds_center[2],
          nearest_diagnostic.bounds_radius,
          nearest_diagnostic.a.x, nearest_diagnostic.a.y,
          nearest_diagnostic.a.z, nearest_diagnostic.b.x,
          nearest_diagnostic.b.y, nearest_diagnostic.b.z,
          nearest_diagnostic.c.x, nearest_diagnostic.c.y,
          nearest_diagnostic.c.z);
    }
  }
  ++g_camera_mesh_sweeps;
  return true;
}

bool ClipThirdPersonOrbitAgainstSceneObjects(
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& requested,
    std::array<int32_t, 3>* clipped,
    CameraMeshHitDiagnostic* hit_diagnostic = nullptr,
    const CameraMeshContactPreference* contact_preference = nullptr,
    bool emit_log = true) {
  return ClipThirdPersonOrbitAgainstSceneObjectsInSnapshots(
      g_previous_snapshot, g_older_snapshot, focus, requested, clipped,
      hit_diagnostic, contact_preference, emit_log);
}

uint64_t CameraSpringBlockerKey(
    bool native_blocked, bool mesh_blocked,
    const CameraMeshHitDiagnostic* mesh_diagnostic) {
  constexpr uint64_t kNativeBlockerKey = 1u;
  if (!mesh_blocked || !mesh_diagnostic ||
      (!mesh_diagnostic->node && !mesh_diagnostic->resource)) {
    return native_blocked ? kNativeBlockerKey : 0u;
  }
  // Node pointers are stable only within one launch, which is exactly the
  // lifetime of the spring state. Mix the render resource as well so reused
  // nodes cannot inherit another object's outward-release confirmation.
  uint64_t key = static_cast<uint64_t>(mesh_diagnostic->node);
  key ^= static_cast<uint64_t>(mesh_diagnostic->resource) +
         0x9E3779B97F4A7C15ull + (key << 6u) + (key >> 2u);
  if (native_blocked) {
    key ^= 0xD1B54A32D192ED03ull;
  }
  // Zero means no obstruction and one is reserved for native room geometry.
  return key > kNativeBlockerKey ? key : key + 2u;
}

bool ResolveThirdPersonSpringArm(
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& desired,
    const std::array<int32_t, 3>& hard_safe_endpoint,
    bool obstruction_present, uint64_t blocker_key,
    std::array<int32_t, 3>* submitted) {
  if (!submitted || !g_third_person_orbit_state.engaged) {
    return false;
  }
  const double dx = static_cast<double>(desired[0] - focus[0]);
  const double dy = static_cast<double>(desired[1] - focus[1]);
  const double dz = static_cast<double>(desired[2] - focus[2]);
  const double desired_distance = std::hypot(std::hypot(dx, dz), dy);
  if (!std::isfinite(desired_distance) ||
      desired_distance < kThirdPersonMinimumCameraDistance ||
      desired_distance > 5000.0) {
    return false;
  }

  const double safe_dx =
      static_cast<double>(hard_safe_endpoint[0] - focus[0]);
  const double safe_dy =
      static_cast<double>(hard_safe_endpoint[1] - focus[1]);
  const double safe_dz =
      static_cast<double>(hard_safe_endpoint[2] - focus[2]);
  const double measured_safe_distance =
      std::hypot(std::hypot(safe_dx, safe_dz), safe_dy);
  const double hard_safe_distance = obstruction_present
      ? std::clamp(measured_safe_distance, 0.0, desired_distance)
      : desired_distance;

  ThirdPersonOrbitState& state = g_third_person_orbit_state;
  const double previous_radius =
      std::clamp(state.collision_radius, 0.0, desired_distance);
  const CameraSpringArmStep step = StepCameraSpringArm(
      desired_distance, hard_safe_distance, previous_radius,
      obstruction_present, state.collision_clear_ticks,
      state.collision_blocked_release_ticks,
      state.collision_blocked_candidate_distance,
      state.collision_blocker_key, blocker_key);
  const double next_radius = step.radius;
  state.collision_radius = next_radius;
  state.collision_clear_ticks = step.clear_ticks;
  state.collision_blocked_release_ticks = step.blocked_release_ticks;
  state.collision_blocked_candidate_distance =
      step.blocked_candidate_distance;
  state.collision_blocker_key = step.blocker_key;

  const std::array<double, 3> direction = {
      dx / desired_distance, dy / desired_distance, dz / desired_distance};
  *submitted = {
      focus[0] + static_cast<int32_t>(
                     std::lround(direction[0] * next_radius)),
      focus[1] + static_cast<int32_t>(
                     std::lround(direction[1] * next_radius)),
      focus[2] + static_cast<int32_t>(
                     std::lround(direction[2] * next_radius))};
  if (g_debug_log && std::abs(next_radius - previous_radius) > 1.0) {
    AppendNativeLog(
        "camera_spring desired=%.1f hard=%.1f actual=%.1f->%.1f "
        "blocked=%u clear_ticks=%u blocked_release_ticks=%u "
        "blocker=%llu candidate=%.1f",
        desired_distance, hard_safe_distance, previous_radius, next_radius,
        obstruction_present ? 1u : 0u, state.collision_clear_ticks,
        state.collision_blocked_release_ticks,
        static_cast<unsigned long long>(state.collision_blocker_key),
        state.collision_blocked_candidate_distance);
  }
  return true;
}

bool ResolveOwnedCameraSpringArm(
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& requested,
    const std::array<int32_t, 3>& collision_safe,
    bool obstruction_present, uint64_t blocker_key,
    std::array<int32_t, 3>* submitted) {
  if (!submitted || !g_third_person_orbit_state.engaged) {
    return false;
  }
  const double desired_dx = static_cast<double>(requested[0] - focus[0]);
  const double desired_dy = static_cast<double>(requested[1] - focus[1]);
  const double desired_dz = static_cast<double>(requested[2] - focus[2]);
  const double desired_distance = std::hypot(
      std::hypot(desired_dx, desired_dz), desired_dy);
  if (!std::isfinite(desired_distance) ||
      desired_distance < kThirdPersonMinimumCameraDistance ||
      desired_distance > 5000.0) {
    return false;
  }
  const double safe_dx = static_cast<double>(collision_safe[0] - focus[0]);
  const double safe_dy = static_cast<double>(collision_safe[1] - focus[1]);
  const double safe_dz = static_cast<double>(collision_safe[2] - focus[2]);
  const double safe_distance = std::hypot(
      std::hypot(safe_dx, safe_dz), safe_dy);
  if (!std::isfinite(safe_distance) ||
      safe_distance > desired_distance + 2.0) {
    return false;
  }
  // A collision endpoint only supplies radial clearance. Its integer-rounded
  // direction becomes unstable as the arm approaches the pivot, so always
  // reconstruct the submitted point on the exact user-requested orbit ray.
  const std::array<double, 3> direction = {
      desired_dx / desired_distance, desired_dy / desired_distance,
      desired_dz / desired_distance};
  ThirdPersonOrbitState& state = g_third_person_orbit_state;
  if (!state.owned_publication_active) {
    state.owned_collision_radius = safe_distance;
    state.owned_collision_clear_ticks = 0;
    state.owned_collision_blocked_release_ticks = 0;
    state.owned_collision_blocked_candidate_distance = safe_distance;
    state.owned_collision_blocker_key = blocker_key;
    *submitted = {
        focus[0] + static_cast<int32_t>(
                       std::lround(direction[0] * safe_distance)),
        focus[1] + static_cast<int32_t>(
                       std::lround(direction[1] * safe_distance)),
        focus[2] + static_cast<int32_t>(
                       std::lround(direction[2] * safe_distance))};
    return true;
  }

  const double previous_radius = std::clamp(
      state.owned_collision_radius, 0.0, desired_distance);
  const CameraSpringArmStep step = StepCameraSpringArm(
      desired_distance,
      obstruction_present ? std::min(safe_distance, desired_distance)
                          : desired_distance,
      previous_radius, obstruction_present,
      state.owned_collision_clear_ticks,
      state.owned_collision_blocked_release_ticks,
      state.owned_collision_blocked_candidate_distance,
      state.owned_collision_blocker_key, blocker_key,
      10u, 8u, 64.0, false);
  state.owned_collision_radius = step.radius;
  state.owned_collision_clear_ticks = step.clear_ticks;
  state.owned_collision_blocked_release_ticks =
      step.blocked_release_ticks;
  state.owned_collision_blocked_candidate_distance =
      step.blocked_candidate_distance;
  state.owned_collision_blocker_key = step.blocker_key;
  *submitted = {
      focus[0] + static_cast<int32_t>(
                     std::lround(direction[0] * step.radius)),
      focus[1] + static_cast<int32_t>(
                     std::lround(direction[1] * step.radius)),
      focus[2] + static_cast<int32_t>(
                     std::lround(direction[2] * step.radius))};
  if (g_debug_log && std::abs(step.radius - previous_radius) > 1.0) {
    AppendNativeLog(
        "camera_owned_spring desired=%.1f hard=%.1f actual=%.1f->%.1f "
        "blocked=%u clear_ticks=%u blocked_release_ticks=%u "
        "blocker=%llu candidate=%.1f",
        desired_distance, safe_distance, previous_radius, step.radius,
        obstruction_present ? 1u : 0u,
        state.owned_collision_clear_ticks,
        state.owned_collision_blocked_release_ticks,
        static_cast<unsigned long long>(state.owned_collision_blocker_key),
        state.owned_collision_blocked_candidate_distance);
  }
  return true;
}

bool RebaseThirdPersonCameraPositionHistory(
    void* controller, const std::array<int32_t, 3>& previous_focus,
    const std::array<int32_t, 3>& current_focus) {
  if (!controller) {
    return false;
  }
  const double focus_motion =
      CameraPositionDistance(previous_focus, current_focus);
  if (!std::isfinite(focus_motion) || focus_motion <= 0.0 ||
      focus_motion > 512.0) {
    return false;
  }

  // 0x2F380 smooths camera translation through one cached average followed by
  // four contiguous position samples. They are absolute world-space points.
  // Leaving them behind while the orbit focus moves makes an input-idle camera
  // stick to one room coordinate for several source ticks, then catch up in a
  // visible jump. Translate the complete ring by the same focus delta before
  // the one ordinary configure call. Relative camera history is preserved;
  // 0x2F380 still owns room clipping, floors and orientation.
  constexpr size_t kHistoryPositionCount =
      1u + kCameraControllerPositionHistorySampleCount;
  std::array<int32_t, kHistoryPositionCount * 3u> history{};
  const uintptr_t history_address =
      reinterpret_cast<uintptr_t>(controller) +
      kCameraControllerPositionHistoryAverageOffset;
  if (!SafeRead(reinterpret_cast<const void*>(history_address),
                history.data(), sizeof(history))) {
    AppendNativeLog("camera_history_rebase result=READ_FAILED");
    return false;
  }

  for (size_t position = 0; position < kHistoryPositionCount; ++position) {
    const size_t offset = position * 3u;
    const std::array<int32_t, 3> point = {
        history[offset], history[offset + 1u], history[offset + 2u]};
    const std::array<int32_t, 3> translated =
        TranslateCameraTargetWithFocus(
            previous_focus, current_focus, point);
    std::copy(translated.begin(), translated.end(),
              history.begin() + offset);
  }
  if (!SafeWrite(reinterpret_cast<void*>(history_address),
                 history.data(), sizeof(history))) {
    AppendNativeLog("camera_history_rebase result=WRITE_FAILED");
    return false;
  }

  static uint64_t rebase_count = 0;
  ++rebase_count;
  if (g_debug_log && (rebase_count % 60u) == 1u) {
    AppendNativeLog(
        "camera_history_rebase result=OK count=%llu delta=%d/%d/%d "
        "motion=%.1f",
        static_cast<unsigned long long>(rebase_count),
        current_focus[0] - previous_focus[0],
        current_focus[1] - previous_focus[1],
        current_focus[2] - previous_focus[2], focus_motion);
  }
  return true;
}

bool CameraExactTranslationTransactionMatches(
    void* controller, uintptr_t camera_node,
    const std::array<int32_t, 3>& target,
    uintptr_t target_sector,
    const std::array<int32_t, 3>* angles = nullptr) {
  if (!controller || !camera_node || !target_sector || !g_dungeon_base) {
    return false;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  auto position_matches = [&](uintptr_t address) {
    std::array<int32_t, 3> position{};
    return SafeRead(reinterpret_cast<const void*>(address), position.data(),
                    sizeof(position)) &&
           position == target;
  };
  if (!position_matches(base + kCameraControllerResolvedPositionOffset) ||
      !position_matches(base + kCameraControllerDesiredPositionOffset) ||
      !position_matches(base + kCameraControllerPositionHistoryAverageOffset)) {
    return false;
  }
  for (size_t index = 0;
       index < kCameraControllerPositionHistorySampleCount; ++index) {
    if (!position_matches(
            base + kCameraControllerPositionHistorySamplesOffset +
            index * kCameraControllerPositionHistorySampleStride)) {
      return false;
    }
  }
  if (!position_matches(camera_node)) {
    return false;
  }
  uintptr_t controller_sector = 0;
  uintptr_t node_sector = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerEndpointSectorOffset),
                     &controller_sector) ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         camera_node + kCameraNodeSectorOffset),
                     &node_sector) ||
      controller_sector != target_sector || node_sector != target_sector) {
    return false;
  }
  if (angles) {
    std::array<int32_t, 3> node_angles{};
    if (!SafeRead(reinterpret_cast<const void*>(camera_node + 0x18u),
                  node_angles.data(), sizeof(node_angles)) ||
        node_angles != *angles) {
      return false;
    }
  }
  Matrix3x4 world{};
  Matrix3x4 local{};
  Matrix3x4 published{};
  return SafeRead(reinterpret_cast<const void*>(camera_node + kMatrixOffset),
                  &world, sizeof(world)) &&
         SafeRead(reinterpret_cast<const void*>(
                      camera_node + kLocalMatrixOffset),
                  &local, sizeof(local)) &&
         SafeRead(g_dungeon_base + kPublishedCameraMatrixRva, &published,
                  sizeof(published)) &&
         std::equal(target.begin(), target.end(), world.values.begin() + 9) &&
         std::equal(target.begin(), target.end(), local.values.begin() + 9) &&
         std::equal(target.begin(), target.end(),
                    published.values.begin() + 9);
}

bool CommitValidatedModernCameraEndpoint(
    void* controller, const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& submitted, bool endpoint_validated,
    const char* reason,
    std::array<int32_t, 3>* committed_position) {
  if (!controller || !endpoint_validated || !reason) {
    return false;
  }

  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  std::array<int32_t, 3> configured_resolved{};
  if (!SafeRead(reinterpret_cast<const void*>(
                    base + kCameraControllerResolvedPositionOffset),
                configured_resolved.data(), sizeof(configured_resolved))) {
    return false;
  }

  const double submitted_radius = CameraPositionDistance(focus, submitted);
  const double resolved_radius =
      CameraPositionDistance(focus, configured_resolved);
  if (!std::isfinite(submitted_radius) || !std::isfinite(resolved_radius)) {
    return false;
  }

  // Exact publication has two narrow authorities. Positive scene-mesh
  // contacts commit an endpoint already validated against native room volume.
  // A completely clear full-radius modern tick may also commit, but only
  // after all seven focus traces and all seven endpoint-footprint traces
  // succeed; the retail 0x30910 any-trace predicate used by rejected 0.0.85
  // is not sufficient. Every ambiguous wall/floor/collision case remains on
  // the ordinary resolver path.
  const std::array<int32_t, 3>& target = submitted;
  const bool frequent_diagnostic =
      std::strcmp(reason, "STRICT_CLEAR") != 0;
  static uint64_t strict_clear_commit_sequence = 0;
  if (!frequent_diagnostic) {
    ++strict_clear_commit_sequence;
  }
  const bool log_commit =
      frequent_diagnostic || (strict_clear_commit_sequence % 60u) == 1u;

  uintptr_t camera_owner = 0;
  uintptr_t camera_node = 0;
  const bool node_valid =
      SafeReadValue(reinterpret_cast<const void*>(
                        base + kCameraControllerOwnerOffset),
                    &camera_owner) &&
      camera_owner &&
      SafeReadValue(reinterpret_cast<const void*>(camera_owner +
                                                  kCameraNodeOffset),
                    &camera_node) &&
      camera_node;
  if (!node_valid) {
    return false;
  }

  uintptr_t endpoint_seed_sector = 0;
  uintptr_t focus_sector_holder = 0;
  uintptr_t focus_seed_sector = 0;
  if (!g_resolve_camera_sector ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerEndpointSectorOffset),
                     &endpoint_seed_sector) ||
      !endpoint_seed_sector ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerRoomPointerOffset),
                     &focus_sector_holder) ||
      !focus_sector_holder ||
      !SafeReadValue(reinterpret_cast<const void*>(focus_sector_holder),
                     &focus_seed_sector) ||
      !focus_seed_sector) {
    return false;
  }
  uintptr_t target_sector = 0;
  __try {
    target_sector = g_resolve_camera_sector(target.data(),
                                             endpoint_seed_sector);
    if (!target_sector) {
      target_sector = g_resolve_camera_sector(target.data(),
                                               focus_seed_sector);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    target_sector = 0;
  }
  if (!target_sector) {
    return false;
  }

  // 0x30730 is a pure look-at leaf, but its second argument is not the modern
  // focus.  Capture the resolver-owned target from the immediately preceding
  // 0x2F380 transaction and reuse that exact contract for a mesh-corrected
  // translation.  This keeps position and basis atomic without guessing the
  // authored/native look target or replaying the unsafe camera-cache tail.
  std::array<int32_t, 3> committed_angles{};
  bool committed_angles_valid = false;
  if (g_original_camera_look_at && g_camera_look_at_capture.valid) {
    __try {
      g_original_camera_look_at(
          target.data(), g_camera_look_at_capture.look_target.data(),
          committed_angles.data());
      committed_angles_valid = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      committed_angles_valid = false;
    }
  }
  if (CameraExactTranslationTransactionMatches(controller, camera_node,
                                               target,
                                               target_sector,
                                               committed_angles_valid
                                                   ? &committed_angles
                                                   : nullptr)) {
    if (committed_position) {
      *committed_position = target;
    }
    if (g_debug_log && log_commit) {
      AppendNativeLog(
          "camera_mesh_commit reason=%s result=UNCHANGED resolved_radius=%.1f "
          "safe_radius=%.1f target=%d/%d/%d sector=%08llX pose=%s "
          "node=%08llX",
          reason, resolved_radius, submitted_radius, target[0], target[1],
          target[2], static_cast<unsigned long long>(target_sector),
          committed_angles_valid ? "native_look_target" : "translation",
          static_cast<unsigned long long>(camera_node));
    }
    return true;
  }

  bool written = SafeWrite(
      reinterpret_cast<void*>(base +
                              kCameraControllerResolvedPositionOffset),
      target.data(), sizeof(target));
  written &= SafeWrite(reinterpret_cast<void*>(
                           base + kCameraControllerEndpointSectorOffset),
                       &target_sector, sizeof(target_sector));
  written &= SafeWrite(
      reinterpret_cast<void*>(base +
                              kCameraControllerDesiredPositionOffset),
      target.data(), sizeof(target));
  written &= SafeWrite(
      reinterpret_cast<void*>(
          base + kCameraControllerPositionHistoryAverageOffset),
      target.data(), sizeof(target));
  for (size_t index = 0;
       index < kCameraControllerPositionHistorySampleCount; ++index) {
    written &= SafeWrite(
        reinterpret_cast<void*>(
            base + kCameraControllerPositionHistorySamplesOffset +
            index * kCameraControllerPositionHistorySampleStride),
        target.data(), sizeof(target));
  }

  if (node_valid) {
    // The 0.0.152 experiment called 0x30730 with the modern focus, but retail
    // first selects and authors a separate look target in 0x2DF60/0x2E950.
    // Version 0.0.154 writes angles only when that real same-transaction target
    // was captured above; otherwise the safe fallback remains translation-only.
    written &= SafeWrite(reinterpret_cast<void*>(camera_node), target.data(),
                         sizeof(target));
    written &= SafeWrite(reinterpret_cast<void*>(
                             camera_node + kCameraNodeSectorOffset),
                         &target_sector, sizeof(target_sector));
    if (committed_angles_valid) {
      written &= SafeWrite(reinterpret_cast<void*>(camera_node + 0x18u),
                           committed_angles.data(),
                           sizeof(committed_angles));
    }

    // Keep immediate translations synchronized so no observer in the callback
    // sees the old native-history origin before the ordinary cache tail.
    Matrix3x4 world{};
    if (SafeRead(reinterpret_cast<const void*>(camera_node + kMatrixOffset),
                 &world, sizeof(world))) {
      std::copy(target.begin(), target.end(), world.values.begin() + 9);
      written &= SafeWrite(reinterpret_cast<void*>(camera_node + kMatrixOffset),
                           &world, sizeof(world));
    } else {
      written = false;
    }
    Matrix3x4 local{};
    if (SafeRead(
            reinterpret_cast<const void*>(camera_node + kLocalMatrixOffset),
            &local, sizeof(local))) {
      std::copy(target.begin(), target.end(), local.values.begin() + 9);
      written &= SafeWrite(
          reinterpret_cast<void*>(camera_node + kLocalMatrixOffset), &local,
          sizeof(local));
    } else {
      written = false;
    }
  } else {
    written = false;
  }

  Matrix3x4 published{};
  if (g_dungeon_base &&
      SafeRead(g_dungeon_base + kPublishedCameraMatrixRva, &published,
               sizeof(published))) {
    std::copy(target.begin(), target.end(), published.values.begin() + 9);
    written &= SafeWrite(g_dungeon_base + kPublishedCameraMatrixRva,
                         &published, sizeof(published));
  } else {
    written = false;
  }

  if (committed_position) {
    *committed_position = target;
  }
  if (g_debug_log && (log_commit || !written)) {
    AppendNativeLog(
        "camera_mesh_commit reason=%s result=%s resolved_radius=%.1f "
        "safe_radius=%.1f target=%d/%d/%d sector=%08llX pose=%s "
        "node=%08llX",
        reason, written ? "OK" : "PARTIAL", resolved_radius, submitted_radius,
        target[0], target[1], target[2],
        static_cast<unsigned long long>(target_sector),
        committed_angles_valid ? "native_look_target" : "translation",
        static_cast<unsigned long long>(camera_node));
  }
  return written;
}

struct CameraMeshPushoutResult {
  bool configured = false;
  bool mesh_contact = false;
  bool idempotent_contact = false;
  bool correction_applied = false;
  bool exact_committed = false;
  bool pre_history_clipped = false;
  bool minimum_distance_restored = false;
  bool exhausted = false;
  bool native_resolved_valid = false;
  uint32_t passes = 0;
  std::array<int32_t, 3> native_resolved{};
  std::array<int32_t, 3> initial_published{};
  std::array<int32_t, 3> final_published{};
  std::array<int32_t, 3> accepted_target{};
  CameraMeshHitDiagnostic diagnostic;
};

bool ValidateCameraPreHistoryMeshReplacement(
    void* controller, const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& proposed,
    std::array<int32_t, 3>* validated) {
  if (!controller || !validated) {
    return false;
  }
  std::array<int32_t, 3> candidate = proposed;
  constexpr uint32_t kValidationPasses = 3u;
  for (uint32_t pass = 0u; pass < kValidationPasses; ++pass) {
    if (!CameraTargetMeetsMinimumDistance(
            focus, candidate, kThirdPersonMinimumCameraDistance)) {
      return false;
    }

    bool native_blocked = true;
    if (!ModernCameraFootprintBlocked(
            controller, focus, candidate, &native_blocked)) {
      return false;
    }
    if (native_blocked) {
      std::array<int32_t, 3> native_safe{};
      if (!ClipThirdPersonOrbitAgainstNativeWorld(
              controller, focus, candidate, &native_safe) ||
          native_safe == candidate) {
        return false;
      }
      candidate = native_safe;
      continue;
    }

    std::array<int32_t, 3> scene_safe = candidate;
    CameraMeshHitDiagnostic ignored_diagnostic;
    const bool scene_contact = ClipThirdPersonOrbitAgainstSceneObjects(
        focus, candidate, &scene_safe, &ignored_diagnostic, nullptr, false);
    if (scene_contact &&
        CameraPositionDistance(candidate, scene_safe) > 2.0) {
      candidate = scene_safe;
      continue;
    }
    if (!CameraEndpointClearOfSceneObjectTriangles(candidate)) {
      return false;
    }

    // The accepted integer endpoint must remain clear after all clipping.
    native_blocked = true;
    if (!ModernCameraFootprintBlocked(
            controller, focus, candidate, &native_blocked) ||
        native_blocked) {
      return false;
    }
    *validated = candidate;
    return true;
  }
  return false;
}

int32_t* __cdecl HookCameraHistoryAdd(void* history,
                                      const int32_t* position) {
  if (!g_original_camera_history_add) {
    return nullptr;
  }

  CameraPreHistoryMeshVetoScope& scope =
      g_camera_pre_history_mesh_veto;
  const uintptr_t expected_history =
      scope.controller
          ? reinterpret_cast<uintptr_t>(scope.controller) +
                kCameraControllerPositionHistoryOffset
          : 0u;
  const bool owns_position_ring =
      scope.active && scope.controller && position &&
      reinterpret_cast<uintptr_t>(history) == expected_history;
  if (!owns_position_ring) {
    return g_original_camera_history_add(history, position);
  }

  scope.candidate_seen = SafeRead(
      position, scope.candidate.data(), sizeof(scope.candidate));
  if (!scope.candidate_seen) {
    return g_original_camera_history_add(history, position);
  }

  std::array<int32_t, 3> scene_safe = scope.candidate;
  CameraMeshHitDiagnostic diagnostic;
  scope.mesh_contact = ClipThirdPersonOrbitAgainstSceneObjects(
      scope.focus, scope.candidate, &scene_safe, &diagnostic,
      &g_third_person_orbit_state.mesh_contact_preference, false);
  if (!scope.mesh_contact) {
    return g_original_camera_history_add(history, position);
  }
  scope.diagnostic = diagnostic;

  // This hook is a narrow veto for a native position-ring candidate that is
  // positively inside a qualified scene mesh. Native room/wall shaping keeps
  // ownership for every ordinary candidate; replacing all ring samples with
  // the submitted spring-arm endpoint bypasses that shaping and lets the
  // camera cross walls.
  scope.replacement_valid = ValidateCameraPreHistoryMeshReplacement(
      scope.controller, scope.focus, scene_safe, &scope.replacement);
  scope.used_submitted_fallback = false;

  // Candidate-derived correction remains the primary owner. If the complete
  // native+scene constraint set rejects it, validate the exact endpoint from
  // the start of this configure transaction. Roll back only when validation
  // returns that endpoint unchanged. This is narrower than the rejected
  // 0.0.161 fallback, which could accept a different submitted-side point and
  // alternate collision owners.
  if (!scope.replacement_valid) {
    scope.submitted_validation_attempted = true;
    scope.submitted_validation_valid =
        ValidateCameraPreHistoryMeshReplacement(
            scope.controller, scope.focus, scope.submitted,
            &scope.submitted_validation_result);
    scope.submitted_validation_unchanged =
        scope.submitted_validation_valid &&
        scope.submitted_validation_result == scope.submitted;
    if (CameraPreHistoryUsesSubmittedFixedPoint(
            scope.replacement_valid, scope.submitted_validation_valid,
            scope.submitted_validation_unchanged)) {
      scope.replacement = scope.submitted;
      scope.replacement_valid = true;
      scope.used_submitted_fallback = true;
    }
  }

  if (!CameraPreHistoryMeshVetoMayApply(
          scope.active, scope.controller != nullptr,
          reinterpret_cast<uintptr_t>(history) == expected_history,
          scope.mesh_contact, scope.replacement_valid)) {
    return g_original_camera_history_add(history, position);
  }

  scope.replacement_applied = true;
  return g_original_camera_history_add(history, scope.replacement.data());
}

void __cdecl HookCameraLookAt(const int32_t* camera_position,
                              const int32_t* look_target,
                              int32_t* angles) {
  if (g_camera_look_at_capture.active) {
    g_camera_look_at_capture.valid =
        camera_position && look_target &&
        SafeRead(camera_position,
                 g_camera_look_at_capture.camera_position.data(),
                 sizeof(g_camera_look_at_capture.camera_position)) &&
        SafeRead(look_target,
                 g_camera_look_at_capture.look_target.data(),
                 sizeof(g_camera_look_at_capture.look_target));
  }
  if (g_original_camera_look_at) {
    g_original_camera_look_at(camera_position, look_target, angles);
  }
}

bool CallConfigureCamera(void* controller,
                         const std::array<int32_t, 3>& target,
                         uintptr_t room_or_sector) {
  if (!controller || !g_configure_camera || !room_or_sector) {
    return false;
  }
  bool configured = false;
  g_camera_look_at_capture = {};
  g_camera_look_at_capture.active = true;
  g_camera_pre_history_mesh_veto = {};
  g_camera_pre_history_mesh_veto.controller = controller;
  g_camera_pre_history_mesh_veto.submitted = target;
  g_camera_pre_history_mesh_veto.active =
      ReadCameraFocusPosition(
          controller, &g_camera_pre_history_mesh_veto.focus);
  __try {
    g_configure_camera(controller, target[0], target[1], target[2],
                       room_or_sector, 1);
    configured = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    configured = false;
  }
  g_camera_pre_history_mesh_veto.active = false;
  g_camera_look_at_capture.active = false;
  return configured;
}

bool ReadPublishedCameraPosition(std::array<int32_t, 3>* position) {
  if (!position || !g_dungeon_base) {
    return false;
  }
  Matrix3x4 published{};
  if (!SafeRead(g_dungeon_base + kPublishedCameraMatrixRva,
                &published, sizeof(published))) {
    return false;
  }
  *position = {
      published.values[9], published.values[10], published.values[11]};
  return true;
}

bool ConfigureCameraWithSceneMeshPushout(
    void* controller, const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& submitted, uintptr_t room_or_sector,
    CameraMeshPushoutResult* result) {
  if (!result) {
    return false;
  }
  *result = {};
  result->accepted_target = submitted;

  // Runtime 0.0.87 proved that repeated 0x2F380 calls inside one source tick
  // do not advance this controller. Configure exactly once.
  if (!CallConfigureCamera(controller, submitted, room_or_sector)) {
    return false;
  }
  result->configured = true;
  result->passes = 1u;
  result->native_resolved_valid = SafeRead(
      reinterpret_cast<const uint8_t*>(controller) +
          kCameraControllerResolvedPositionOffset,
      result->native_resolved.data(), sizeof(result->native_resolved));
  if (!ReadPublishedCameraPosition(&result->initial_published)) {
    return false;
  }
  result->final_published = result->initial_published;

  const CameraPreHistoryMeshVetoScope& pre_history =
      g_camera_pre_history_mesh_veto;
  if (pre_history.candidate_seen && pre_history.mesh_contact) {
    if (g_debug_log) {
      AppendNativeLog(
          "camera_pre_history_mesh state=%s candidate=%d/%d/%d "
          "replacement=%d/%d/%d submitted=%d/%d/%d "
          "submitted_validated=%d/%d/%d published=%d/%d/%d resource=%llu "
          "submitted_fallback=%u submitted_check=%u/%u/%u",
          pre_history.used_submitted_fallback
              ? "FIXED_POINT_ROLLBACK"
              : (pre_history.replacement_applied ? "APPLIED"
                                                 : "FALLBACK_POST"),
          pre_history.candidate[0], pre_history.candidate[1],
          pre_history.candidate[2], pre_history.replacement[0],
          pre_history.replacement[1], pre_history.replacement[2],
          pre_history.submitted[0], pre_history.submitted[1],
          pre_history.submitted[2],
          pre_history.submitted_validation_result[0],
          pre_history.submitted_validation_result[1],
          pre_history.submitted_validation_result[2],
          result->initial_published[0], result->initial_published[1],
          result->initial_published[2],
          static_cast<unsigned long long>(
              pre_history.diagnostic.resource),
          pre_history.used_submitted_fallback ? 1u : 0u,
          pre_history.submitted_validation_attempted ? 1u : 0u,
          pre_history.submitted_validation_valid ? 1u : 0u,
          pre_history.submitted_validation_unchanged ? 1u : 0u);
    }
    if (pre_history.replacement_applied && pre_history.mesh_contact) {
      result->mesh_contact = true;
      result->correction_applied = true;
      result->pre_history_clipped = true;
      result->accepted_target = result->initial_published;
      result->diagnostic = pre_history.diagnostic;
      if ((pre_history.diagnostic.overlap_pushout ||
           pre_history.diagnostic.near_pivot_escape) &&
          pre_history.diagnostic.node &&
          pre_history.diagnostic.resource &&
          pre_history.diagnostic.pushout_axis < 3u) {
        g_third_person_orbit_state.mesh_contact_preference = {
            pre_history.diagnostic.node,
            pre_history.diagnostic.resource,
            pre_history.diagnostic.pushout_axis, true};
      }
      const double constrained_radius = CameraPositionDistance(
          focus, result->initial_published);
      if (std::isfinite(constrained_radius)) {
        g_third_person_orbit_state.collision_radius =
            std::min(g_third_person_orbit_state.collision_radius,
                     constrained_radius);
        g_third_person_orbit_state.collision_clear_ticks = 0;
        g_third_person_orbit_state.collision_blocked_release_ticks = 0;
        g_third_person_orbit_state.collision_blocked_candidate_distance =
            constrained_radius;
        g_third_person_orbit_state.collision_blocker_key =
            CameraSpringBlockerKey(
                false, true, &pre_history.diagnostic);
      }
    }
  }

  std::array<int32_t, 3> mesh_safe = result->initial_published;
  CameraMeshHitDiagnostic diagnostic;
  if (!ClipThirdPersonOrbitAgainstSceneObjects(
          focus, result->initial_published, &mesh_safe, &diagnostic,
          &g_third_person_orbit_state.mesh_contact_preference)) {
    return true;
  }

  result->mesh_contact = true;
  result->pre_history_clipped = false;
  result->diagnostic = diagnostic;
  if (diagnostic.overlap_pushout ||
      diagnostic.near_pivot_escape) {
    bool room_blocked = true;
    if (!ModernCameraFootprintBlocked(
            controller, focus, mesh_safe, &room_blocked)) {
      result->exhausted = true;
      AppendNativeLog(
          "camera_mesh_overlap_pushout validation=unavailable "
          "resource=%llu",
          static_cast<unsigned long long>(diagnostic.resource));
      return true;
    }
    if (room_blocked) {
      std::array<int32_t, 3> room_safe{};
      if (!ClipThirdPersonOrbitAgainstNativeWorld(
              controller, focus, mesh_safe, &room_safe)) {
        result->exhausted = true;
        AppendNativeLog(
            "camera_mesh_overlap_pushout validation=failed resource=%llu",
            static_cast<unsigned long long>(diagnostic.resource));
        return true;
      }
      mesh_safe = room_safe;
      AppendNativeLog(
          "camera_mesh_overlap_pushout validation=clipped "
          "resource=%llu target=%d/%d/%d",
          static_cast<unsigned long long>(diagnostic.resource),
          mesh_safe[0], mesh_safe[1], mesh_safe[2]);
    }
  }

  // The post-native pass observes a position already shifted by the retail
  // history resolver. In a multi-block squeeze that shifted segment can be
  // shorter than the usable expanded-OBB escape selected before 0x2F380.
  // Never turn that secondary contact into a 0--119 unit exact commit at the
  // player pivot. The original submitted point has already passed both the
  // complete scene-mesh arm query and the native room-volume validation in
  // the caller, so restore it as the contact-only publication target.
  if (!CameraTargetMeetsMinimumDistance(
          focus, mesh_safe, kThirdPersonMinimumCameraDistance)) {
    bool submitted_native_blocked = true;
    const bool submitted_usable =
        CameraTargetMeetsMinimumDistance(
            focus, submitted, kThirdPersonMinimumCameraDistance) &&
        ModernCameraFootprintBlocked(
            controller, focus, submitted, &submitted_native_blocked) &&
        !submitted_native_blocked &&
        CameraEndpointClearOfSceneObjectTriangles(submitted);
    if (!submitted_usable) {
      result->exhausted = true;
      AppendNativeLog(
          "camera_mesh_minimum_restore result=REJECTED "
          "resource=%llu unsafe=%d/%d/%d submitted=%d/%d/%d",
          static_cast<unsigned long long>(diagnostic.resource),
          mesh_safe[0], mesh_safe[1], mesh_safe[2],
          submitted[0], submitted[1], submitted[2]);
      return true;
    }
    AppendNativeLog(
        "camera_mesh_minimum_restore result=OK resource=%llu "
        "unsafe=%d/%d/%d restored=%d/%d/%d",
        static_cast<unsigned long long>(diagnostic.resource),
        mesh_safe[0], mesh_safe[1], mesh_safe[2],
        submitted[0], submitted[1], submitted[2]);
    mesh_safe = submitted;
    result->minimum_distance_restored = true;
  }
  if (g_debug_log) {
    AppendNativeLog(
        "camera_post_native_mesh published=%d/%d/%d "
        "safe_target=%d/%d/%d node=%08llX resource=%llu tri=%llu "
        "overlap_pushout=%u near_pivot_escape=%u motion=%.1f",
        result->initial_published[0], result->initial_published[1],
        result->initial_published[2], mesh_safe[0], mesh_safe[1],
        mesh_safe[2], static_cast<unsigned long long>(diagnostic.node),
        static_cast<unsigned long long>(diagnostic.resource),
        static_cast<unsigned long long>(diagnostic.triangle_index),
        diagnostic.overlap_pushout ? 1u : 0u,
        diagnostic.near_pivot_escape ? 1u : 0u,
        diagnostic.bounds_motion);
  }

  const double correction_distance = CameraPositionDistance(
      result->initial_published, mesh_safe);
  if (CameraPostNativeMeshContactIsIdempotent(
          result->mesh_contact, correction_distance)) {
    // A zero-motion boundary result is already the current publication. An
    // exact transaction cannot move it to a safer point; it can only replace
    // the position-ring history with the same old point, suppress orbit input
    // and keep collision_radius/clear_ticks permanently latched. Preserve the
    // positive contact for diagnostics, but make the state transition a true
    // no-op so the next native configure can advance toward current input.
    result->idempotent_contact = true;
    result->accepted_target = result->initial_published;
    if (g_debug_log) {
      AppendNativeLog(
          "camera_post_native_mesh action=IDEMPOTENT_NOOP "
          "published=%d/%d/%d resource=%llu motion=%.2f",
          result->initial_published[0], result->initial_published[1],
          result->initial_published[2],
          static_cast<unsigned long long>(diagnostic.resource),
          correction_distance);
    }
    return true;
  }

  if ((diagnostic.overlap_pushout || diagnostic.near_pivot_escape) &&
      diagnostic.node && diagnostic.resource &&
      diagnostic.pushout_axis < 3u) {
    g_third_person_orbit_state.mesh_contact_preference = {
        diagnostic.node, diagnostic.resource, diagnostic.pushout_axis, true};
  }

  // This is the object-collision phase after the native room/LOS phase. A
  // normal contact remains a shorter point on the native-resolved segment. If
  // the expanded object contains the pivot, radial contraction is undefined;
  // the target is instead the deterministic nearest horizontal OBB face and
  // has passed the native room-volume validation above. Clear ticks, authored
  // shots and ordinary room geometry that pass the mesh sweep never enter this
  // direct publication path.
  result->accepted_target = mesh_safe;
  std::array<int32_t, 3> committed{};
  if (!CommitValidatedModernCameraEndpoint(
          controller, focus, mesh_safe, true, "SCENE_MESH", &committed)) {
    result->exhausted = true;
    return true;
  }
  result->correction_applied = true;
  result->exact_committed = true;
  result->final_published = committed;
  const double constrained_radius = CameraPositionDistance(focus, committed);
  if (std::isfinite(constrained_radius)) {
    if (result->minimum_distance_restored) {
      g_third_person_orbit_state.collision_radius =
          constrained_radius;
    } else {
      g_third_person_orbit_state.collision_radius =
          std::min(g_third_person_orbit_state.collision_radius,
                   constrained_radius);
    }
    g_third_person_orbit_state.collision_clear_ticks = 0;
    g_third_person_orbit_state.collision_blocked_release_ticks = 0;
    g_third_person_orbit_state.collision_blocked_candidate_distance =
        constrained_radius;
    g_third_person_orbit_state.collision_blocker_key =
        CameraSpringBlockerKey(false, true, &diagnostic);
  }
  return true;
}

void PublishImmersiveFirstPersonHeadingIntent();

void __cdecl HookMode3Camera(void* controller) {
  if (!g_original_mode3_camera || !g_configure_camera ||
      !g_resolve_camera_sector ||
      !g_camera_volume_visible || !controller) {
    if (g_original_mode3_camera) {
      g_original_mode3_camera(controller);
    }
    return;
  }

  // Camera mode 3 is revisited by cache refreshes and every synthetic render
  // phase. Only the first invocation for an engine frame may integrate input,
  // advance the spring arm or update cinematic arbitration. Duplicate calls
  // deliberately retain the already-published camera state.
  if (!BeginMode3SourceTick(controller)) {
    return;
  }
  g_third_person_orbit_state.collision_constrained_this_tick = false;
  g_last_mode3_source_tick_ms.store(GetTickCount64(),
                                     std::memory_order_release);

  const uintptr_t base = reinterpret_cast<uintptr_t>(controller);
  std::array<int32_t, 3> before_original{};
  const bool before_original_valid = SafeRead(
      reinterpret_cast<const uint8_t*>(controller) +
          kCameraControllerResolvedPositionOffset,
      before_original.data(), sizeof(before_original));
  uintptr_t retail_owner = 0;
  ReadRetailCameraOwner(controller, &retail_owner);
  // Obtain the untouched retail candidate once. Ordinary room/fixed-camera
  // ownership never takes control, but a recent explicit operate command may
  // temporarily expose an authored lever/switch reveal if the native camera
  // independently starts travelling while the player is stationary.
  RetailCameraProbeSnapshot probe_snapshot{};
  CaptureRetailCameraProbeState(controller, &probe_snapshot);
  g_original_mode3_camera(controller);
  const bool scripted = before_original_valid &&
      EvaluateRetailCameraTakeover(controller, retail_owner,
                                   before_original);
  g_scripted_camera_override_active.store(scripted,
                                           std::memory_order_release);
  if (scripted) {
    g_custom_head_publication_active.store(false,
                                            std::memory_order_release);
    g_custom_head_last_publication_ms.store(0,
                                             std::memory_order_release);
    if (g_third_person_orbit_state.owned_publication_active) {
      g_third_person_orbit_state.owned_publication_active = false;
      g_owned_camera_presentation_cut_pending.store(
          true, std::memory_order_release);
    }
    if (g_third_person_orbit_state.engaged &&
        !g_third_person_orbit_state.suspended) {
      g_third_person_orbit_state.suspended = true;
      g_third_person_orbit_state.filtered_input_x = 0.0;
      g_third_person_orbit_state.filtered_input_y = 0.0;
      AppendNativeLog("camera_orbit suspend reason=explicit_reveal");
    }
    return;
  }

  std::array<int32_t, 3> native{};
  if (!SafeRead(reinterpret_cast<const void*>(
                    base + kCameraControllerDesiredPositionOffset),
                native.data(), sizeof(native))) {
    ResetThirdPersonOrbit("native_desired_position");
    return;
  }
  if (probe_snapshot.valid &&
      !RestoreRetailCameraProbeState(controller, probe_snapshot)) {
    ResetThirdPersonOrbit("retail_probe_restore");
    return;
  }

  const bool previous_modern_sample_valid =
      before_original_valid &&
      g_third_person_orbit_state.engaged &&
      g_third_person_orbit_state.controller == controller;
  const std::array<int32_t, 3> previous_focus =
      g_third_person_orbit_state.previous_player;
  std::array<int32_t, 3> orbit{};
  if (!BuildThirdPersonOrbitPosition(controller, native, &orbit)) {
    return;
  }
  std::array<int32_t, 3> camera_focus{};
  if (!ReadCameraFocusPosition(controller, &camera_focus)) {
    ResetThirdPersonOrbit("camera_focus_after_retail");
    return;
  }

  uintptr_t room_holder = 0;
  uintptr_t room_or_sector = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(
                         base + kCameraControllerRoomPointerOffset),
                     &room_holder) ||
      !room_holder ||
      !SafeReadValue(reinterpret_cast<const void*>(room_holder),
                     &room_or_sector) ||
      !room_or_sector) {
    ResetThirdPersonOrbit("room_pointer");
    return;
  }

  // Diagnostic-only contract check for the full-ownership camera. It reads
  // each newly visited sector once and does not participate in the hybrid
  // 0.0.172 collision or publication path.
  LogCameraRoomSectorSnapshot(camera_focus, room_or_sector);
  if (CustomHeadViewSelected()) {
    const uint64_t head_now_ms = GetTickCount64();
    const uint64_t previous_head_ms =
        g_custom_head_last_publication_ms.load(std::memory_order_acquire);
    const bool transition_cut =
        !g_custom_head_publication_active.load(std::memory_order_acquire) ||
        !previous_head_ms || head_now_ms - previous_head_ms > 200u;
    if (PublishImmersiveFirstPersonEndpoint(
            controller, camera_focus, room_or_sector, transition_cut)) {
      // The body-course writer belongs to a successfully published head view,
      // not merely to the requested mode.  This prevents a missing skeleton
      // joint from locking keyboard steering while the fail-closed third-
      // person endpoint is visible.
      PublishImmersiveFirstPersonHeadingIntent();
      g_custom_head_publication_active.store(true,
                                              std::memory_order_release);
      g_custom_head_last_publication_ms.store(head_now_ms,
                                               std::memory_order_release);
      g_third_person_orbit_state.owned_publication_active = true;
      g_third_person_orbit_state.collision_constrained_this_tick = false;
      return;
    }
    // Keep the normal fully-owned third-person transaction as a fail-closed
    // fallback for a missing room graph/snapshot instead of exposing the
    // untouched retail fixed camera for one frame.
    if (g_custom_head_publication_active.exchange(
            false, std::memory_order_acq_rel)) {
      g_owned_camera_presentation_cut_pending.store(
          true, std::memory_order_release);
    }
    g_custom_head_last_publication_ms.store(0,
                                             std::memory_order_release);
    g_xinput_camera_relative_intent.store(false,
                                           std::memory_order_release);
    g_xinput_movement_magnitude_milli.store(0,
                                             std::memory_order_release);
    g_xinput_movement_controller.store(0, std::memory_order_release);
    g_xinput_camera_relative_started_ms.store(0,
                                               std::memory_order_release);
    AppendNativeLog(
        "camera_immersive_first_person fallback=MODERN_THIRD_PERSON");
  } else if (g_custom_head_publication_active.exchange(
                 false, std::memory_order_acq_rel)) {
    g_custom_head_last_publication_ms.store(0,
                                             std::memory_order_release);
    g_owned_camera_presentation_cut_pending.store(
        true, std::memory_order_release);
  }
  deathtrap_camera::RoomSweepResult owned_room_sweep;
  deathtrap_camera::RoomSweepResult owned_room_applied_sweep;
  deathtrap_camera::RoomOrbitPlan owned_room_plan;
  std::array<int32_t, 3> owned_room_safe = orbit;
  size_t owned_room_start_index = std::numeric_limits<size_t>::max();
  const bool owned_room_query_valid =
      SweepOwnedCameraAgainstRooms(camera_focus, orbit, room_or_sector,
                                   &owned_room_sweep, &owned_room_safe,
                                   &owned_room_plan,
                                   g_third_person_orbit_state
                                       .owned_room_shadow_candidate_index,
                                   &owned_room_start_index);
  OwnedCameraCombinedPlan owned_combined_plan;
  const double owned_minimum_transition_distance =
      kCameraCollisionSphereRadius * 3.0;
  const bool owned_combined_plan_valid = owned_room_query_valid &&
      SelectOwnedCameraCombinedPlan(
          g_previous_snapshot, g_older_snapshot, camera_focus,
          owned_room_plan, kThirdPersonMinimumCameraDistance,
          kCameraCollisionSphereRadius,
          kThirdPersonMinimumCameraDistance,
          g_third_person_orbit_state.owned_room_shadow_candidate_index,
          &g_third_person_orbit_state.owned_room_direct_clear_ticks,
          &owned_combined_plan);
  bool owned_room_applied_valid = false;
  bool owned_room_applied_safe_cut = false;
  OwnedCameraSceneSweepResult owned_scene_sweep;
  bool owned_scene_query_valid = false;
  if (owned_combined_plan_valid) {
    owned_room_plan.selected_index = owned_combined_plan.selected_index;
    owned_room_plan.retained_previous =
        owned_combined_plan.retained_previous;
    owned_room_plan.avoidance_required =
        owned_combined_plan.selected_index != 0u;
    g_third_person_orbit_state.owned_room_shadow_candidate_index =
        owned_room_plan.selected_index;
    const auto& target =
        owned_room_plan.candidates[owned_room_plan.selected_index].offset;
    if (!g_third_person_orbit_state
             .owned_room_shadow_offset_initialized) {
      g_third_person_orbit_state.owned_room_shadow_applied_yaw = 0.0;
      g_third_person_orbit_state.owned_room_shadow_applied_pitch = 0.0;
      g_third_person_orbit_state.owned_room_shadow_offset_initialized = true;
    }
    constexpr double kMaximumOwnedAngularStep =
        30.0 * kOrbitPi / 180.0;
    const double yaw_error = std::remainder(
        target.yaw_radians -
            g_third_person_orbit_state.owned_room_shadow_applied_yaw,
        2.0 * kOrbitPi);
    g_third_person_orbit_state.owned_room_shadow_applied_yaw +=
        std::clamp(yaw_error, -kMaximumOwnedAngularStep,
                   kMaximumOwnedAngularStep);
    g_third_person_orbit_state.owned_room_shadow_applied_yaw =
        std::remainder(
            g_third_person_orbit_state.owned_room_shadow_applied_yaw,
            2.0 * kOrbitPi);
    const double pitch_error = target.pitch_radians -
        g_third_person_orbit_state.owned_room_shadow_applied_pitch;
    g_third_person_orbit_state.owned_room_shadow_applied_pitch +=
        std::clamp(pitch_error, -kMaximumOwnedAngularStep,
                   kMaximumOwnedAngularStep);
    const deathtrap_camera::RoomVec3 room_focus{
        static_cast<double>(camera_focus[0]),
        static_cast<double>(camera_focus[1]),
        static_cast<double>(camera_focus[2])};
    const deathtrap_camera::RoomVec3 room_orbit{
        static_cast<double>(orbit[0]), static_cast<double>(orbit[1]),
        static_cast<double>(orbit[2])};
    const deathtrap_camera::RoomVec3 applied_requested =
        deathtrap_camera::RotateRoomOrbit(
            room_focus, room_orbit,
            {g_third_person_orbit_state.owned_room_shadow_applied_yaw,
             g_third_person_orbit_state.owned_room_shadow_applied_pitch});
    owned_room_applied_sweep = deathtrap_camera::SweepSphereThroughRooms(
        g_runtime_room_graph.sectors, owned_room_start_index, room_focus,
        applied_requested, kCameraCollisionSphereRadius, 8.0, 32u);
    owned_room_applied_valid = owned_room_applied_sweep.valid;
    if (owned_room_applied_valid) {
      const double applied_safe_distance = deathtrap_camera::Length(
          owned_room_applied_sweep.position - room_focus);
      const auto& selected =
          owned_room_plan.candidates[owned_room_plan.selected_index];
      if (deathtrap_camera::ShouldUseRoomOrbitSafetyCut(
              applied_safe_distance, selected.safe_distance,
              owned_minimum_transition_distance)) {
        g_third_person_orbit_state.owned_room_shadow_applied_yaw =
            target.yaw_radians;
        g_third_person_orbit_state.owned_room_shadow_applied_pitch =
            target.pitch_radians;
        owned_room_applied_sweep = selected.sweep;
        owned_room_applied_valid = selected.sweep.valid;
        owned_room_applied_safe_cut = true;
      }
    }
    if (owned_room_applied_valid) {
      const std::array<int32_t, 3> owned_room_applied_position = {
          static_cast<int32_t>(
              std::lround(owned_room_applied_sweep.position.x)),
          static_cast<int32_t>(
              std::lround(owned_room_applied_sweep.position.y)),
          static_cast<int32_t>(
              std::lround(owned_room_applied_sweep.position.z))};
      owned_scene_query_valid =
          SweepOwnedCameraAgainstSceneMeshesInSnapshots(
              g_previous_snapshot, g_older_snapshot, camera_focus,
              owned_room_applied_position, &owned_scene_sweep);
    }
    if (owned_room_applied_valid && owned_scene_query_valid) {
      const double room_applied_safe_distance = deathtrap_camera::Length(
          owned_room_applied_sweep.position - room_focus);
      const double applied_combined_safe_distance =
          owned_scene_sweep.blocked
              ? std::min(room_applied_safe_distance,
                         owned_scene_sweep.safe_distance)
              : room_applied_safe_distance;
      const auto& selected_combined = owned_combined_plan.candidates[
          owned_combined_plan.selected_index];
      if (deathtrap_camera::ShouldUseRoomOrbitSafetyCut(
              applied_combined_safe_distance,
              selected_combined.safe_distance,
              owned_minimum_transition_distance)) {
        const auto& selected_room = owned_room_plan.candidates[
            owned_room_plan.selected_index];
        g_third_person_orbit_state.owned_room_shadow_applied_yaw =
            target.yaw_radians;
        g_third_person_orbit_state.owned_room_shadow_applied_pitch =
            target.pitch_radians;
        owned_room_applied_sweep = selected_room.sweep;
        owned_room_applied_valid = selected_room.sweep.valid;
        owned_scene_sweep = selected_combined.scene;
        owned_scene_query_valid = selected_combined.scene.valid;
        owned_room_applied_safe_cut = true;
      }
    }
    if (owned_room_applied_valid && owned_scene_query_valid) {
      std::array<int32_t, 3> owned_combined_position = {
          static_cast<int32_t>(
              std::lround(owned_room_applied_sweep.position.x)),
          static_cast<int32_t>(
              std::lround(owned_room_applied_sweep.position.y)),
          static_cast<int32_t>(
              std::lround(owned_room_applied_sweep.position.z))};
      if (owned_scene_sweep.blocked) {
        owned_combined_position = owned_scene_sweep.position;
      }
      const double desired_distance = CameraPositionDistance(
          camera_focus, orbit);
      const double combined_safe_distance = CameraPositionDistance(
          camera_focus, owned_combined_position);
      const bool combined_obstructed =
          owned_room_applied_sweep.blocked || owned_scene_sweep.blocked ||
          (std::isfinite(desired_distance) &&
           std::isfinite(combined_safe_distance) &&
           combined_safe_distance + 0.5 < desired_distance);
      const uint64_t combined_blocker_key = CameraSpringBlockerKey(
          owned_room_applied_sweep.blocked, owned_scene_sweep.blocked,
          owned_scene_sweep.blocked ? &owned_scene_sweep.diagnostic
                                    : nullptr);
      const CameraNearPivotModeStep near_pivot_step =
          StepCameraNearPivotMode(
              g_third_person_orbit_state.owned_near_pivot_view_active,
              combined_safe_distance,
              g_third_person_orbit_state
                  .owned_near_pivot_direct_clear_ticks,
              kThirdPersonMinimumCameraDistance,
              owned_minimum_transition_distance, 4u);
      g_third_person_orbit_state.owned_near_pivot_view_active =
          near_pivot_step.active;
      g_third_person_orbit_state.owned_near_pivot_direct_clear_ticks =
          near_pivot_step.direct_clear_ticks;
      std::array<int32_t, 3> owned_look_target = camera_focus;
      std::array<double, 3> owned_view_forward{};
      const std::array<double, 3>* owned_fixed_view_forward = nullptr;
      std::array<int32_t, 3> owned_spring_position{};
      if (near_pivot_step.active) {
        if (BuildOwnedCameraViewForward(
                camera_focus, orbit, &owned_view_forward)) {
          owned_fixed_view_forward = &owned_view_forward;
        }
      }
      const bool owned_spring_valid_initial =
          (!near_pivot_step.active || owned_fixed_view_forward) &&
          ResolveOwnedCameraSpringArm(
              camera_focus, orbit, owned_combined_position,
              combined_obstructed, combined_blocker_key,
              &owned_spring_position);
      bool owned_spring_valid = owned_spring_valid_initial;

      // Radial recovery is another intermediate source pose. Re-run both
      // collision channels on that exact rounded point; a shorter radius is
      // not assumed safe when the focus begins inside a camera-margin plane.
      deathtrap_camera::RoomSweepResult owned_spring_room;
      OwnedCameraSceneSweepResult owned_spring_scene;
      bool owned_spring_scene_valid = false;
      std::array<int32_t, 3> owned_publish_position =
          owned_spring_position;
      if (owned_spring_valid) {
        const deathtrap_camera::RoomVec3 room_spring{
            static_cast<double>(owned_spring_position[0]),
            static_cast<double>(owned_spring_position[1]),
            static_cast<double>(owned_spring_position[2])};
        owned_spring_room = deathtrap_camera::SweepSphereThroughRooms(
            g_runtime_room_graph.sectors, owned_room_start_index, room_focus,
            room_spring, kCameraCollisionSphereRadius, 8.0, 32u);
        owned_spring_valid = owned_spring_room.valid;
      }
      if (owned_spring_valid) {
        owned_publish_position = {
            static_cast<int32_t>(
                std::lround(owned_spring_room.position.x)),
            static_cast<int32_t>(
                std::lround(owned_spring_room.position.y)),
            static_cast<int32_t>(
                std::lround(owned_spring_room.position.z))};
        owned_spring_scene_valid =
            SweepOwnedCameraAgainstSceneMeshesInSnapshots(
                g_previous_snapshot, g_older_snapshot, camera_focus,
                owned_publish_position, &owned_spring_scene);
      }
      owned_spring_valid = owned_spring_valid && owned_spring_scene_valid;
      if (owned_spring_valid && owned_spring_scene.blocked) {
        owned_publish_position = owned_spring_scene.position;
      }

      bool owned_transition_cut = owned_room_applied_safe_cut ||
          near_pivot_step.changed ||
          !g_third_person_orbit_state.owned_publication_active;
      const double revalidated_safe_distance = CameraPositionDistance(
          camera_focus, owned_publish_position);
      if (owned_spring_valid &&
          std::isfinite(revalidated_safe_distance) &&
          std::isfinite(combined_safe_distance) &&
          revalidated_safe_distance < owned_minimum_transition_distance &&
          combined_safe_distance >= owned_minimum_transition_distance) {
        // The damped radial path crosses an unsafe disconnected region.
        // Publish the already verified final shot and cut presentation once.
        owned_publish_position = owned_combined_position;
        g_third_person_orbit_state.owned_collision_radius =
            combined_safe_distance;
        g_third_person_orbit_state.owned_collision_clear_ticks = 0;
        g_third_person_orbit_state
            .owned_collision_blocked_release_ticks = 0;
        g_third_person_orbit_state
            .owned_collision_blocked_candidate_distance =
            combined_safe_distance;
        g_third_person_orbit_state.owned_collision_blocker_key =
            combined_blocker_key;
        owned_transition_cut = true;
      } else if (owned_spring_valid &&
                 std::isfinite(revalidated_safe_distance)) {
        g_third_person_orbit_state.owned_collision_radius =
            revalidated_safe_distance;
      }

      if (g_debug_log && owned_spring_valid) {
        static uint64_t owned_solution_sequence = 0;
        ++owned_solution_sequence;
        const auto& selected_solution = owned_combined_plan.candidates[
            owned_combined_plan.selected_index];
        if (combined_obstructed || owned_spring_room.blocked ||
            owned_spring_scene.blocked || owned_transition_cut ||
            (owned_solution_sequence % 30u) == 1u) {
          AppendNativeLog(
              "camera_owned_solution selected=%llu retained=%u "
              "direct=%.1f selected=%.1f combined=%.1f spring=%.1f "
              "revalidated=%.1f room=%u/%u scene=%u/%u overlap=%u "
              "resource=%llu tri=%llu motion=%.1f blocker=%llu cut=%u "
              "direct_clear=%u near=%u/%u",
              static_cast<unsigned long long>(
                  owned_combined_plan.selected_index),
              owned_combined_plan.retained_previous ? 1u : 0u,
              owned_combined_plan.candidates[0].safe_distance,
              selected_solution.safe_distance, combined_safe_distance,
              CameraPositionDistance(camera_focus, owned_spring_position),
              revalidated_safe_distance,
              owned_room_applied_sweep.blocked ? 1u : 0u,
              owned_spring_room.blocked ? 1u : 0u,
              owned_scene_sweep.blocked ? 1u : 0u,
              owned_spring_scene.blocked ? 1u : 0u,
              (owned_scene_sweep.initial_overlap ||
               owned_spring_scene.initial_overlap) ? 1u : 0u,
              static_cast<unsigned long long>(
                  owned_spring_scene.blocked
                      ? owned_spring_scene.diagnostic.resource
                      : owned_scene_sweep.diagnostic.resource),
              static_cast<unsigned long long>(
                  owned_spring_scene.blocked
                      ? owned_spring_scene.diagnostic.triangle_index
                      : owned_scene_sweep.diagnostic.triangle_index),
              owned_spring_scene.blocked
                  ? owned_spring_scene.diagnostic.bounds_motion
                  : owned_scene_sweep.diagnostic.bounds_motion,
              static_cast<unsigned long long>(combined_blocker_key),
              owned_transition_cut ? 1u : 0u,
              g_third_person_orbit_state.owned_room_direct_clear_ticks,
              near_pivot_step.active ? 1u : 0u,
              near_pivot_step.direct_clear_ticks);
        }
      }

      if (owned_spring_valid && PublishOwnedCameraEndpoint(
              controller, camera_focus, owned_look_target,
              owned_fixed_view_forward,
              owned_publish_position,
              owned_room_start_index, owned_transition_cut)) {
        g_third_person_orbit_state.owned_publication_active = true;
        g_third_person_orbit_state.collision_constrained_this_tick =
            combined_obstructed;
        return;
      }
      if (g_third_person_orbit_state.owned_publication_active) {
        g_third_person_orbit_state.owned_publication_active = false;
        g_owned_camera_presentation_cut_pending.store(
            true, std::memory_order_release);
      }
      AppendNativeLog(
          "camera_owned_publish_live valid=0 reason=%s",
          owned_spring_valid ? "TRANSACTION" : "SPRING_REVALIDATION");
    }
  } else {
    if (g_third_person_orbit_state.owned_publication_active) {
      g_third_person_orbit_state.owned_publication_active = false;
      g_owned_camera_presentation_cut_pending.store(
          true, std::memory_order_release);
    }
    g_third_person_orbit_state.owned_room_shadow_candidate_index =
        std::numeric_limits<size_t>::max();
    g_third_person_orbit_state.owned_room_direct_clear_ticks = 0;
    g_third_person_orbit_state.owned_near_pivot_view_active = false;
    g_third_person_orbit_state.owned_near_pivot_direct_clear_ticks = 0;
    g_third_person_orbit_state.owned_room_shadow_offset_initialized = false;
  }
  // Reaching the hybrid path means the complete owned transaction was not
  // available for this source tick. Treat that as an explicit owner switch,
  // never as two camera owners contributing to one endpoint.
  if (g_third_person_orbit_state.owned_publication_active) {
    g_third_person_orbit_state.owned_publication_active = false;
    g_owned_camera_presentation_cut_pending.store(
        true, std::memory_order_release);
    AppendNativeLog("camera_owned_publish_live owner=HYBRID_FALLBACK");
  }

  // The untouched retail callback runs first so authored-camera arbitration
  // can inspect it. If a transient native query failure prevents a new modern
  // endpoint, do not leave that retail/fixed candidate published for one
  // frame. Translate the preceding verified modern result by the focus delta
  // and feed it through the ordinary native configure + scene-mesh phases.
  // This is a short fail-closed hold, not a direct global camera write.
  auto publish_held_modern_sample = [&](const char* reason) {
    if (!previous_modern_sample_valid) {
      return false;
    }
    std::array<int32_t, 3> held_target = before_original;
    for (size_t axis = 0; axis < held_target.size(); ++axis) {
      held_target[axis] += camera_focus[axis] - previous_focus[axis];
    }
    CameraMeshPushoutResult held_pushout;
    if (!ConfigureCameraWithSceneMeshPushout(
            controller, camera_focus, held_target, room_or_sector,
            &held_pushout)) {
      return false;
    }
    AppendNativeLog(
        "camera_native_spring hold reason=%s target=%d/%d/%d "
        "mesh=%u/%u/%u",
        reason, held_target[0], held_target[1], held_target[2],
        held_pushout.mesh_contact ? 1u : 0u,
        held_pushout.correction_applied ? 1u : 0u,
        held_pushout.exhausted ? 1u : 0u);
    return true;
  };

  // The complete retail mode-3 dispatcher is not a deterministic spring arm:
  // when the requested ray is blocked it calls 0x2F750, whose alternate
  // placement search deliberately changes sectors and sides between ticks.
  // For a player-controlled orbit this produces corner sticking and camera
  // jumps even with an unchanged focus and input. Use the stock seven-trace
  // volume predicate as the collision authority, but resolve a blocked arm
  // only by shortening the exact requested ray. This is the conventional
  // third-person spring-arm model: immediate contraction, delayed bounded
  // release and no lateral fallback state.
  bool orbit_blocked = false;
  const bool visibility_query_valid = ModernCameraFootprintBlocked(
      controller, camera_focus, orbit, &orbit_blocked);
  if (!visibility_query_valid) {
    const bool held = publish_held_modern_sample("query_unavailable");
    AppendNativeLog("camera_native_spring unavailable held=%u",
                    held ? 1u : 0u);
    return;
  }

  std::array<int32_t, 3> hard_safe_endpoint = orbit;
  if (orbit_blocked &&
      !ClipThirdPersonOrbitAgainstNativeWorld(
          controller, camera_focus, orbit, &hard_safe_endpoint)) {
    const bool held = publish_held_modern_sample("clip_failed");
    AppendNativeLog("camera_native_spring clip_failed held=%u",
                    held ? 1u : 0u);
    return;
  }

  if (g_debug_log) {
    static uint64_t owned_room_shadow_sequence = 0;
    ++owned_room_shadow_sequence;
    const double native_safe_radius =
        CameraPositionDistance(camera_focus, hard_safe_endpoint);
    const double owned_safe_radius =
        CameraPositionDistance(camera_focus, owned_room_safe);
    const bool ownership_differs = !owned_room_query_valid ||
        owned_room_sweep.blocked != orbit_blocked ||
        (owned_room_query_valid && std::isfinite(native_safe_radius) &&
         std::isfinite(owned_safe_radius) &&
         std::abs(native_safe_radius - owned_safe_radius) > 16.0);
    if (ownership_differs || owned_room_sweep.blocked || orbit_blocked ||
        (owned_room_shadow_sequence % 30u) == 1u) {
      AppendNativeLog(
          "camera_owned_room_shadow valid=%u blocked=%u/%u overlap=%u "
          "transitions=%llu fraction=%.5f safe=%d/%d/%d "
          "native=%d/%d/%d radius=%.1f/%.1f blocker=%llu",
          owned_room_query_valid ? 1u : 0u,
          owned_room_query_valid && owned_room_sweep.blocked ? 1u : 0u,
          orbit_blocked ? 1u : 0u,
          owned_room_query_valid && owned_room_sweep.started_overlapping
              ? 1u : 0u,
          static_cast<unsigned long long>(
              owned_room_query_valid
                  ? owned_room_sweep.portal_transitions
                  : 0u),
          owned_room_query_valid ? owned_room_sweep.fraction : 0.0,
          owned_room_safe[0], owned_room_safe[1], owned_room_safe[2],
          hard_safe_endpoint[0], hard_safe_endpoint[1],
          hard_safe_endpoint[2], owned_safe_radius, native_safe_radius,
          static_cast<unsigned long long>(
              owned_room_query_valid ? owned_room_sweep.blocker_key : 0u));
    }
    if (owned_combined_plan_valid && owned_room_plan.valid &&
        owned_room_plan.selected_index < owned_room_plan.candidates.size()) {
      const auto& selected =
          owned_room_plan.candidates[owned_room_plan.selected_index];
      const double applied_room_safe_distance = owned_room_applied_valid
          ? deathtrap_camera::Length(
                owned_room_applied_sweep.position -
                deathtrap_camera::RoomVec3{
                    static_cast<double>(camera_focus[0]),
                    static_cast<double>(camera_focus[1]),
                    static_cast<double>(camera_focus[2])})
          : 0.0;
      const double applied_safe_distance =
          owned_scene_query_valid && owned_scene_sweep.blocked
              ? std::min(applied_room_safe_distance,
                         owned_scene_sweep.safe_distance)
              : applied_room_safe_distance;
      const double selected_combined_safe_distance =
          owned_combined_plan.candidates[
              owned_combined_plan.selected_index].safe_distance;
      if (owned_room_plan.avoidance_required || owned_room_sweep.blocked ||
          (owned_room_shadow_sequence % 30u) == 1u) {
        AppendNativeLog(
            "camera_owned_shot_shadow selected=%llu retained=%u "
            "offset=%.1f/%.1f direct=%.1f selected=%.1f/%.1f "
            "applied=%.1f/%.1f applied_safe=%.1f/%u cut=%u "
            "blocked=%u transitions=%llu endpoint=%.1f/%.1f/%.1f",
            static_cast<unsigned long long>(owned_room_plan.selected_index),
            owned_room_plan.retained_previous ? 1u : 0u,
            selected.offset.yaw_radians * 180.0 / kOrbitPi,
            selected.offset.pitch_radians * 180.0 / kOrbitPi,
            owned_room_plan.candidates[0].safe_distance,
            selected.safe_distance, selected_combined_safe_distance,
            g_third_person_orbit_state.owned_room_shadow_applied_yaw *
                180.0 / kOrbitPi,
            g_third_person_orbit_state.owned_room_shadow_applied_pitch *
                180.0 / kOrbitPi,
            applied_safe_distance,
            owned_room_applied_valid && owned_room_applied_sweep.blocked
                ? 1u : 0u,
            owned_room_applied_safe_cut ? 1u : 0u,
            selected.sweep.blocked ? 1u : 0u,
            static_cast<unsigned long long>(
                selected.sweep.portal_transitions),
            selected.sweep.position.x, selected.sweep.position.y,
            selected.sweep.position.z);
      }
    }
    if (owned_room_applied_valid &&
        (owned_scene_sweep.blocked || !owned_scene_query_valid ||
         (owned_room_shadow_sequence % 30u) == 1u)) {
      AppendNativeLog(
          "camera_owned_scene_shadow valid=%u blocked=%u overlap=%u "
          "requested=%.1f contact=%.1f safe=%.1f endpoint=%d/%d/%d "
          "node=%llu resource=%llu tri=%llu motion=%.1f",
          owned_scene_query_valid ? 1u : 0u,
          owned_scene_query_valid && owned_scene_sweep.blocked ? 1u : 0u,
          owned_scene_query_valid && owned_scene_sweep.initial_overlap
              ? 1u : 0u,
          owned_scene_query_valid ? owned_scene_sweep.requested_distance
                                  : 0.0,
          owned_scene_query_valid ? owned_scene_sweep.contact_distance : 0.0,
          owned_scene_query_valid ? owned_scene_sweep.safe_distance : 0.0,
          owned_scene_sweep.position[0], owned_scene_sweep.position[1],
          owned_scene_sweep.position[2],
          static_cast<unsigned long long>(
              owned_scene_sweep.diagnostic.node),
          static_cast<unsigned long long>(
              owned_scene_sweep.diagnostic.resource),
          static_cast<unsigned long long>(
              owned_scene_sweep.diagnostic.triangle_index),
          owned_scene_sweep.diagnostic.bounds_motion);
    }
  }

  // Treat a qualified scene mesh as the same persistent spring-arm
  // obstruction as native room geometry. Version 0.0.89 only corrected the
  // final published point, so the next source tick considered the desired arm
  // clear, extended by 64 units and drove back into the same block. Query the
  // complete desired arm before stepping the spring and retain the nearer of
  // the native and scene-mesh endpoints. The post-native pass below remains a
  // safety net for lateral/vertical shifts introduced by 0x2F380.
  std::array<int32_t, 3> mesh_safe_endpoint = orbit;
  CameraMeshHitDiagnostic mesh_orbit_diagnostic;
  const bool mesh_orbit_blocked =
      ClipThirdPersonOrbitAgainstSceneObjects(
          camera_focus, orbit, &mesh_safe_endpoint,
          &mesh_orbit_diagnostic,
          &g_third_person_orbit_state.mesh_contact_preference);
  if (mesh_orbit_blocked &&
      (mesh_orbit_diagnostic.overlap_pushout ||
       mesh_orbit_diagnostic.near_pivot_escape) &&
      mesh_orbit_diagnostic.node && mesh_orbit_diagnostic.resource &&
      mesh_orbit_diagnostic.pushout_axis < 3u) {
    g_third_person_orbit_state.mesh_contact_preference = {
        mesh_orbit_diagnostic.node, mesh_orbit_diagnostic.resource,
        mesh_orbit_diagnostic.pushout_axis, true};
  } else {
    g_third_person_orbit_state.mesh_contact_preference = {};
  }
  if (mesh_orbit_blocked) {
    if (mesh_orbit_diagnostic.overlap_pushout ||
        mesh_orbit_diagnostic.near_pivot_escape) {
      // A contained or near-surface pivot has no usable radial "near side".
      // Preserve the deterministic expanded-OBB escape and let the native
      // room-volume validation below reject or clip it if necessary.
      hard_safe_endpoint = mesh_safe_endpoint;
    } else {
      const double native_safe_radius =
          CameraPositionDistance(camera_focus, hard_safe_endpoint);
      const double mesh_safe_radius =
          CameraPositionDistance(camera_focus, mesh_safe_endpoint);
      if (std::isfinite(mesh_safe_radius) &&
          (!std::isfinite(native_safe_radius) ||
           mesh_safe_radius < native_safe_radius)) {
        hard_safe_endpoint = mesh_safe_endpoint;
      }
    }
  }
  const bool spring_arm_blocked = orbit_blocked || mesh_orbit_blocked;
  const uint64_t spring_blocker_key = CameraSpringBlockerKey(
      orbit_blocked, mesh_orbit_blocked,
      mesh_orbit_blocked ? &mesh_orbit_diagnostic : nullptr);
  const double desired_radius =
      CameraPositionDistance(camera_focus, orbit);
  const bool non_radial_escape_available =
      mesh_orbit_blocked &&
      (mesh_orbit_diagnostic.overlap_pushout ||
       mesh_orbit_diagnostic.near_pivot_escape);
  bool pre_native_escape_selected = false;

  // Arkham keeps collision distance/history inside the camera state. It does
  // not preserve an old world-space endpoint as a second orbit centre. The
  // former previous-arm/tangent/latch arbitration did exactly that and was
  // responsible for stuck cameras and rotation around an invisible point.
  // A blocked desired arm now has one answer: the nearest safe point on the
  // current requested arm (or the deterministic near-pivot OBB escape).
  std::array<int32_t, 3> submitted{};
  if (non_radial_escape_available) {
    // A non-radial escape is rebuilt from the current focus, requested orbit
    // and current object transform. No delayed native publication is allowed
    // to retain collision ownership after the current sweeps are clear.
    pre_native_escape_selected = true;
    submitted = hard_safe_endpoint;
    const double pushed_radius =
        CameraPositionDistance(camera_focus, submitted);
    if (!std::isfinite(pushed_radius)) {
      AppendNativeLog("camera_mesh_overlap_pushout invalid");
      return;
    }
    g_third_person_orbit_state.collision_radius = pushed_radius;
    g_third_person_orbit_state.collision_clear_ticks = 0;
    g_third_person_orbit_state.collision_blocked_release_ticks = 0;
    g_third_person_orbit_state.collision_blocked_candidate_distance =
        pushed_radius;
    g_third_person_orbit_state.collision_blocker_key = spring_blocker_key;
    AppendNativeLog(
        "camera_mesh_overlap_pushout target=%d/%d/%d radius=%.1f "
        "resource=%llu overlap=%u escape=%u axis=%llu",
        submitted[0], submitted[1], submitted[2], pushed_radius,
        static_cast<unsigned long long>(
            mesh_orbit_diagnostic.resource),
        mesh_orbit_diagnostic.overlap_pushout ? 1u : 0u,
        mesh_orbit_diagnostic.near_pivot_escape ? 1u : 0u,
        static_cast<unsigned long long>(
            mesh_orbit_diagnostic.pushout_axis));
  } else {
    if (!ResolveThirdPersonSpringArm(
            camera_focus, orbit, hard_safe_endpoint, spring_arm_blocked,
            spring_blocker_key, &submitted)) {
      AppendNativeLog("camera_native_spring resolve_failed");
      return;
    }
  }
  const double submitted_radius =
      CameraPositionDistance(camera_focus, submitted);
  g_third_person_orbit_state.collision_constrained_this_tick =
      spring_arm_blocked ||
      (std::isfinite(desired_radius) &&
       std::isfinite(submitted_radius) &&
       submitted_radius + 0.5 < desired_radius);

  // Release candidates are generated from a previously contracted radius.
  // Validate the actual rounded endpoint too; a portal boundary need not be
  // perfectly monotonic after integer conversion.
  bool submitted_blocked = true;
  if (!ModernCameraFootprintBlocked(controller, camera_focus, submitted,
                                    &submitted_blocked)) {
    const bool held = publish_held_modern_sample("validation_unavailable");
    AppendNativeLog("camera_native_spring validation_unavailable held=%u",
                    held ? 1u : 0u);
    return;
  }
  if (submitted_blocked) {
    std::array<int32_t, 3> reclipped{};
    if (!ClipThirdPersonOrbitAgainstNativeWorld(
            controller, camera_focus, submitted, &reclipped)) {
      const bool held = publish_held_modern_sample("validation_failed");
      AppendNativeLog("camera_native_spring validation_failed held=%u",
                      held ? 1u : 0u);
      return;
    }
    submitted = reclipped;
    g_third_person_orbit_state.collision_radius =
        CameraPositionDistance(camera_focus, submitted);
    g_third_person_orbit_state.collision_clear_ticks = 0;
    g_third_person_orbit_state.collision_blocked_release_ticks = 0;
    g_third_person_orbit_state.collision_blocked_candidate_distance =
        g_third_person_orbit_state.collision_radius;
    g_third_person_orbit_state.collision_blocker_key =
        CameraSpringBlockerKey(true, false, nullptr);
  }

  // First resolve against native rooms/walls/floors, then push the published
  // camera out of any qualified large scene mesh. This matches the separation
  // used by modernized classic-camera engines: room LOS and item collision are
  // distinct phases. Thin levers remain nonblocking by the two-axis size rule.
  CameraMeshPushoutResult mesh_pushout;
  if (!ConfigureCameraWithSceneMeshPushout(
          controller, camera_focus, submitted, room_or_sector,
          &mesh_pushout)) {
    AppendNativeLog("camera_native_mesh_pushout configure_failed");
    return;
  }
  g_third_person_orbit_state.collision_constrained_this_tick |=
      mesh_pushout.mesh_contact;

  // The desired-arm scene sweep is the collision authority for moving blocks
  // and other render meshes that do not belong to the native room graph. If
  // it selected a safe shortened endpoint, do not let the downstream retail
  // fixed-camera resolver replace that result with a different height/radius.
  // Revalidate the actual rounded spring endpoint against the complete room
  // footprint and current scene occupancy after the one ordinary configure
  // call, then publish it as one exact transaction. A conflicting post-native
  // scene contact retains ownership and prevents this path.
  bool pre_native_scene_query_available = false;
  bool pre_native_scene_native_clear = false;
  bool pre_native_scene_endpoint_clear = false;
  if (mesh_orbit_blocked && !mesh_pushout.mesh_contact &&
      !mesh_pushout.exact_committed && !mesh_pushout.exhausted) {
    pre_native_scene_query_available = NativeCameraVolumeFullyClear(
        controller, camera_focus, submitted,
        &pre_native_scene_native_clear);
    pre_native_scene_endpoint_clear =
        pre_native_scene_query_available && pre_native_scene_native_clear &&
        CameraEndpointClearOfSceneObjectTriangles(submitted);
  }
  const bool pre_native_scene_may_commit =
      CameraPreNativeSceneContactMayOwnFinalPosition(
          mesh_orbit_blocked, mesh_pushout.mesh_contact,
          mesh_pushout.exact_committed, mesh_pushout.exhausted,
          CameraTargetMeetsMinimumDistance(
              camera_focus, submitted,
              kThirdPersonMinimumCameraDistance),
          pre_native_scene_query_available,
          pre_native_scene_native_clear,
          pre_native_scene_endpoint_clear);
  bool pre_native_scene_committed = false;
  if (pre_native_scene_may_commit) {
    std::array<int32_t, 3> committed{};
    pre_native_scene_committed = CommitValidatedModernCameraEndpoint(
        controller, camera_focus, submitted, true, "PRE_NATIVE_SCENE",
        &committed);
    if (pre_native_scene_committed) {
      mesh_pushout.final_published = committed;
      mesh_pushout.accepted_target = committed;
      mesh_pushout.correction_applied = true;
      mesh_pushout.exact_committed = true;
      const double committed_radius =
          CameraPositionDistance(camera_focus, committed);
      if (std::isfinite(committed_radius)) {
        g_third_person_orbit_state.collision_radius = committed_radius;
        g_third_person_orbit_state.collision_blocked_candidate_distance =
            committed_radius;
      }
    }
    if (g_debug_log) {
      AppendNativeLog(
          "camera_pre_native_scene_commit result=%s resource=%llu "
          "target=%d/%d/%d strict=%u/%u endpoint=%u",
          pre_native_scene_committed ? "OK" : "FAILED",
          static_cast<unsigned long long>(mesh_orbit_diagnostic.resource),
          submitted[0], submitted[1], submitted[2],
          pre_native_scene_query_available ? 1u : 0u,
          pre_native_scene_native_clear ? 1u : 0u,
          pre_native_scene_endpoint_clear ? 1u : 0u);
    }
  }

  // Both the scoped pre-history replacement and the post-configure sweep are
  // current-call collision evidence. If either accepts a target different
  // from the exceptional pre-native escape, the escape is incompatible with
  // another member of the current constraint set. Retail history lag cannot
  // turn that positive contact into permission to overwrite the safe result.
  const bool post_native_result_consistent =
      CameraConfiguredMeshResultAllowsPreNativeEscape(
          mesh_pushout.mesh_contact,
          mesh_pushout.idempotent_contact,
          mesh_pushout.correction_applied,
          mesh_pushout.accepted_target == submitted);
  bool pre_native_escape_committed = false;
  if (!pre_native_scene_committed &&
      CameraPreNativeEscapeOwnsFinalTarget(
          pre_native_escape_selected,
          mesh_orbit_diagnostic.overlap_pushout,
          mesh_orbit_diagnostic.near_pivot_escape,
          CameraTargetMeetsMinimumDistance(
              camera_focus, submitted,
              kThirdPersonMinimumCameraDistance),
          true, post_native_result_consistent)) {
    std::array<int32_t, 3> committed{};
    pre_native_escape_committed = CommitValidatedModernCameraEndpoint(
        controller, camera_focus, submitted, true, "PRE_NATIVE_ESCAPE",
        &committed);
    if (pre_native_escape_committed) {
      mesh_pushout.final_published = committed;
      mesh_pushout.accepted_target = committed;
      mesh_pushout.correction_applied = true;
      mesh_pushout.exact_committed = true;
      const double committed_radius =
          CameraPositionDistance(camera_focus, committed);
      if (std::isfinite(committed_radius)) {
        g_third_person_orbit_state.collision_radius = committed_radius;
        g_third_person_orbit_state.collision_blocked_candidate_distance =
            committed_radius;
      }
    }
    if (g_debug_log) {
      AppendNativeLog(
          "camera_pre_native_escape_commit result=%s resource=%llu "
          "target=%d/%d/%d",
          pre_native_escape_committed ? "OK" : "FAILED",
          static_cast<unsigned long long>(
              mesh_orbit_diagnostic.resource),
          submitted[0], submitted[1], submitted[2]);
    }
  }
  if (g_debug_log && pre_native_escape_selected &&
      (mesh_orbit_diagnostic.overlap_pushout ||
       mesh_orbit_diagnostic.near_pivot_escape) &&
      !post_native_result_consistent) {
    AppendNativeLog(
        "camera_pre_native_escape_commit result=POST_MESH_CONFLICT "
        "resource=%llu submitted=%d/%d/%d corrected=%d/%d/%d",
        static_cast<unsigned long long>(
            mesh_orbit_diagnostic.resource),
        submitted[0], submitted[1], submitted[2],
        mesh_pushout.accepted_target[0], mesh_pushout.accepted_target[1],
        mesh_pushout.accepted_target[2]);
  }

  // 0x2F380 still runs once for sector bookkeeping and the resolver-owned
  // look target/orientation. Its fixed-camera position resolver must not,
  // however, deform a completely clear modern orbit into a repeatable
  // non-circular path. The old 0.0.158 attempt authorized this with 0x30910,
  // whose any-one-of-seven success rule let partially occluded endpoints
  // cross walls. Exact clear ownership now requires every underlying room
  // trace plus the scene endpoint check; all constrained and ambiguous ticks
  // keep the ordinary native result.
  const bool full_radius_request =
      !spring_arm_blocked && submitted == orbit;
  bool strict_native_clear = false;
  bool strict_native_query_available = false;
  bool strict_scene_endpoint_clear = false;
  if (mesh_pushout.configured && !mesh_pushout.mesh_contact &&
      !mesh_pushout.exact_committed && !mesh_pushout.exhausted &&
      full_radius_request) {
    strict_native_query_available = NativeCameraVolumeFullyClear(
        controller, camera_focus, submitted, &strict_native_clear);
    strict_scene_endpoint_clear =
        strict_native_query_available && strict_native_clear &&
        CameraEndpointClearOfSceneObjectTriangles(submitted);
  }
  const bool modern_endpoint_owns_final =
      CameraModernEndpointMayOwnFinalPosition(
          mesh_pushout.configured, mesh_pushout.mesh_contact,
          mesh_pushout.exact_committed, mesh_pushout.exhausted,
          full_radius_request, strict_native_query_available,
          strict_native_clear, strict_scene_endpoint_clear);
  if (modern_endpoint_owns_final) {
    std::array<int32_t, 3> committed{};
    if (CommitValidatedModernCameraEndpoint(
            controller, camera_focus, submitted, true, "STRICT_CLEAR",
            &committed)) {
      mesh_pushout.final_published = committed;
      mesh_pushout.accepted_target = committed;
      mesh_pushout.exact_committed = true;
    } else if (g_debug_log) {
      AppendNativeLog(
          "camera_modern_endpoint_commit result=FAILED target=%d/%d/%d",
          submitted[0], submitted[1], submitted[2]);
    }
  }

  // ConfigureCameraWithSceneMeshPushout has already recorded any positive
  // current-tick contact in the single spring-arm state. A clear post-native
  // result deliberately has no state transition: the normal spring-arm
  // release policy consumes current sweep evidence on the next source tick.
  // In particular, the lagged four-position retail publication is never fed
  // back as a collision ceiling.

  if (g_debug_log) {
    std::array<int32_t, 3> configured_desired{};
    std::array<int32_t, 3> configured_resolved{};
    Matrix3x4 published{};
    const bool desired_valid = SafeRead(
        reinterpret_cast<const void*>(
            base + kCameraControllerDesiredPositionOffset),
        configured_desired.data(), sizeof(configured_desired));
    const bool resolved_valid = SafeRead(
        reinterpret_cast<const void*>(
            base + kCameraControllerResolvedPositionOffset),
        configured_resolved.data(), sizeof(configured_resolved));
    const bool published_valid = g_dungeon_base && SafeRead(
        g_dungeon_base + kPublishedCameraMatrixRva,
        &published, sizeof(published));
    static uint32_t diagnostic_sequence = 0;
    ++diagnostic_sequence;
    if (spring_arm_blocked || submitted != orbit ||
        mesh_pushout.mesh_contact ||
        (diagnostic_sequence & 15u) == 0u) {
      AppendNativeLog(
          "camera_native_mesh_pushout focus=%d/%d/%d orbit=%d/%d/%d "
          "safe=%d/%d/%d submitted=%d/%d/%d "
          "after_desired=%d/%d/%d after_resolved=%d/%d/%d "
          "published=%d/%d/%d valid=%u/%u/%u "
          "native_candidate=%d/%d/%d candidate_valid=%u "
          "blocked=%u/%u "
          "mesh=%u/%u/%u exact=%u idempotent=%u pre_history=%u passes=%u "
          "strict=%u/%u/%u/%u "
          "initial_published=%d/%d/%d radius=%.1f clear_ticks=%u "
          "blocked_release_ticks=%u "
          "blocker=%llu candidate=%.1f "
          "pre_resource=%llu resource=%llu tri=%llu motion=%.1f",
          camera_focus[0], camera_focus[1], camera_focus[2],
          orbit[0], orbit[1], orbit[2],
          hard_safe_endpoint[0], hard_safe_endpoint[1],
          hard_safe_endpoint[2],
          submitted[0], submitted[1], submitted[2],
          configured_desired[0], configured_desired[1],
          configured_desired[2], configured_resolved[0],
          configured_resolved[1], configured_resolved[2],
          published.values[9], published.values[10], published.values[11],
          desired_valid ? 1u : 0u, resolved_valid ? 1u : 0u,
          published_valid ? 1u : 0u,
          mesh_pushout.native_resolved[0],
          mesh_pushout.native_resolved[1],
          mesh_pushout.native_resolved[2],
          mesh_pushout.native_resolved_valid ? 1u : 0u,
          orbit_blocked ? 1u : 0u,
          mesh_orbit_blocked ? 1u : 0u,
          mesh_pushout.mesh_contact ? 1u : 0u,
          mesh_pushout.correction_applied ? 1u : 0u,
          mesh_pushout.exhausted ? 1u : 0u,
          mesh_pushout.exact_committed ? 1u : 0u,
          mesh_pushout.idempotent_contact ? 1u : 0u,
          mesh_pushout.pre_history_clipped ? 1u : 0u,
          mesh_pushout.passes,
          strict_native_query_available ? 1u : 0u,
          strict_native_clear ? 1u : 0u,
          strict_scene_endpoint_clear ? 1u : 0u,
          modern_endpoint_owns_final ? 1u : 0u,
          mesh_pushout.initial_published[0],
          mesh_pushout.initial_published[1],
          mesh_pushout.initial_published[2],
          g_third_person_orbit_state.collision_radius,
          g_third_person_orbit_state.collision_clear_ticks,
          g_third_person_orbit_state.collision_blocked_release_ticks,
          static_cast<unsigned long long>(
              g_third_person_orbit_state.collision_blocker_key),
          g_third_person_orbit_state.collision_blocked_candidate_distance,
          static_cast<unsigned long long>(
              mesh_orbit_diagnostic.resource),
          static_cast<unsigned long long>(
              mesh_pushout.diagnostic.resource),
          static_cast<unsigned long long>(
              mesh_pushout.diagnostic.triangle_index),
          mesh_pushout.diagnostic.bounds_motion);
    }
  }
}

WORD VibrationMotorValue(uint32_t percent, double channel_scale) {
  const double scaled = std::clamp(
      static_cast<double>(percent) * channel_scale, 0.0, 100.0);
  return static_cast<WORD>(std::lround(
      scaled * static_cast<double>(std::numeric_limits<WORD>::max()) / 100.0));
}

void StartEventVibration(std::atomic<uint64_t>* deadline,
                         std::atomic<uint32_t>* strength, uint64_t now,
                         uint32_t duration_ms, uint32_t percent);

void ApplyControllerVibration(WORD left_motor, WORD right_motor) {
  if (left_motor == g_applied_vibration_left &&
      right_motor == g_applied_vibration_right) {
    return;
  }
  if (!g_xinput_set_state) {
    g_applied_vibration_left = 0;
    g_applied_vibration_right = 0;
    return;
  }
  XINPUT_VIBRATION vibration = {};
  vibration.wLeftMotorSpeed = left_motor;
  vibration.wRightMotorSpeed = right_motor;
  if (g_xinput_set_state(g_xinput_controller_index, &vibration) ==
      ERROR_SUCCESS) {
    g_applied_vibration_left = left_motor;
    g_applied_vibration_right = right_motor;
  }
}

void StopControllerVibration() {
  g_block_vibration_until_ms = 0;
  g_melee_swing_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_successful_block_vibration_until_ms.store(0,
                                               std::memory_order_relaxed);
  g_spell_cast_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_ranged_shot_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_healing_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_selector_tick_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_landing_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_heavy_damage_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_player_melee_attack_window_active.store(false,
                                             std::memory_order_relaxed);
  g_last_controller_attack_ms.store(0, std::memory_order_relaxed);
  g_hit_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_damage_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_death_vibration_until_ms.store(0, std::memory_order_relaxed);
  g_hit_vibration_percent.store(0, std::memory_order_relaxed);
  g_damage_vibration_percent.store(0, std::memory_order_relaxed);
  g_landing_vibration_percent.store(0, std::memory_order_relaxed);
  g_previous_xinput_left_trigger = 0;
  ApplyControllerVibration(0, 0);
}

void ClearPendingControllerVibrationEvents() {
  g_successful_block_vibration_pending.store(false,
                                               std::memory_order_release);
  g_spell_cast_vibration_pending.store(false, std::memory_order_release);
  g_ranged_shot_vibration_pending.store(false, std::memory_order_release);
  g_healing_vibration_pending.store(false, std::memory_order_release);
  g_selector_tick_vibration_pending.store(false, std::memory_order_release);
  g_landing_vibration_pending.store(false, std::memory_order_release);
}

void UpdateControllerVibration(const XINPUT_GAMEPAD& pad, bool gameplay,
                               bool selector_captures_controls) {
  if (!g_xinput_vibration_enabled || !gameplay || !g_xinput_set_state) {
    StopControllerVibration();
    return;
  }

  const uint64_t now = GetTickCount64();
  const bool selector_tick_started =
      g_selector_tick_vibration_pending.exchange(false,
                                                   std::memory_order_acq_rel);
  if (selector_tick_started) {
    StartEventVibration(&g_selector_tick_vibration_until_ms, nullptr, now,
                        g_xinput_selector_tick_vibration_ms,
                        g_xinput_vibration_strength_percent);
  }
  if (selector_captures_controls) {
    WORD left_motor = 0;
    WORD right_motor = 0;
    if (now < g_selector_tick_vibration_until_ms.load(
                  std::memory_order_acquire)) {
      left_motor = VibrationMotorValue(
          g_xinput_vibration_strength_percent, 0.10);
      right_motor = VibrationMotorValue(
          g_xinput_vibration_strength_percent, 0.24);
    }
    ApplyControllerVibration(left_motor, right_motor);
    g_previous_xinput_left_trigger = pad.bLeftTrigger;
    return;
  }
  const bool successful_block_started =
      g_successful_block_vibration_pending.exchange(
          false, std::memory_order_acq_rel);
  if (successful_block_started) {
    // Event hooks may run after the last XInput poll of a simulation tick.
    // Start the envelope only when the XInput owner consumes the request so
    // synchronous game logging or a slow frame can never consume its entire
    // duration before a motor command is submitted.
    StartEventVibration(&g_successful_block_vibration_until_ms, nullptr, now,
                        g_xinput_successful_block_vibration_ms,
                        g_xinput_vibration_strength_percent);
  }
  const bool spell_cast_started =
      g_spell_cast_vibration_pending.exchange(false,
                                                std::memory_order_acq_rel);
  if (spell_cast_started) {
    StartEventVibration(&g_spell_cast_vibration_until_ms, nullptr, now,
                        g_xinput_spell_cast_vibration_ms,
                        g_xinput_vibration_strength_percent);
  }
  const bool ranged_shot_started =
      g_ranged_shot_vibration_pending.exchange(false,
                                                 std::memory_order_acq_rel);
  if (ranged_shot_started) {
    StartEventVibration(&g_ranged_shot_vibration_until_ms, nullptr, now,
                        g_xinput_ranged_shot_vibration_ms,
                        g_xinput_vibration_strength_percent);
  }
  const bool healing_started =
      g_healing_vibration_pending.exchange(false,
                                             std::memory_order_acq_rel);
  if (healing_started) {
    StartEventVibration(&g_healing_vibration_until_ms, nullptr, now,
                        g_xinput_healing_vibration_ms,
                        g_xinput_vibration_strength_percent);
  }
  const bool landing_started =
      g_landing_vibration_pending.exchange(false,
                                             std::memory_order_acq_rel);
  if (landing_started) {
    StartEventVibration(&g_landing_vibration_until_ms,
                        &g_landing_vibration_percent, now,
                        g_xinput_landing_vibration_ms,
                        g_landing_vibration_percent.load(
                            std::memory_order_relaxed));
  }
  const bool block_pressed =
      pad.bLeftTrigger >= g_xinput_trigger_threshold &&
      g_previous_xinput_left_trigger < g_xinput_trigger_threshold;
  if (block_pressed) {
    g_block_vibration_until_ms = now + g_xinput_block_vibration_ms;
    AppendNativeLog("xinput vibration block strength=%u duration_ms=%u",
                    g_xinput_vibration_strength_percent,
                    g_xinput_block_vibration_ms);
  }
  g_previous_xinput_left_trigger = pad.bLeftTrigger;

  WORD left_motor = 0;
  WORD right_motor = 0;
  if (now < g_block_vibration_until_ms) {
    // This is only acknowledgement that LT entered the retail block action.
    // Keep it very subtle; the strong envelope belongs exclusively to an
    // enemy impact that the engine has accepted as a successful block.
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.38));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.14));
  }
  const uint64_t melee_swing_until =
      g_melee_swing_vibration_until_ms.load(std::memory_order_acquire);
  if (now < melee_swing_until) {
    // This is the engine-confirmed active/downstroke phase, not a delay from
    // the trigger press. Make it substantially heavier than the action
    // acknowledgement, while still allowing collision feedback to overlap.
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 1.00));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 1.00));
  }
  const uint64_t successful_block_until =
      g_successful_block_vibration_until_ms.load(std::memory_order_acquire);
  if (now < successful_block_until) {
    // A confirmed weapon-to-parry contact is a heavier low-frequency event
    // than merely pressing LT. It is raised only by the engine's successful
    // parry branch after facing, animation-window and geometry checks pass.
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 1.00));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.70));
  }
  const uint64_t spell_cast_until =
      g_spell_cast_vibration_until_ms.load(std::memory_order_acquire);
  if (now < spell_cast_until) {
    // Spell launch favours the high-frequency motor, giving it a distinct
    // texture from melee, damage and a successful defensive impact.
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.70));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 1.00));
  }
  const uint64_t hit_until =
      g_hit_vibration_until_ms.load(std::memory_order_acquire);
  if (now < hit_until) {
    const uint32_t percent =
        g_hit_vibration_percent.load(std::memory_order_relaxed);
    left_motor = std::max(left_motor, VibrationMotorValue(percent, 0.60));
    right_motor = std::max(right_motor, VibrationMotorValue(percent, 1.00));
  }
  const uint64_t ranged_shot_until =
      g_ranged_shot_vibration_until_ms.load(std::memory_order_acquire);
  if (now < ranged_shot_until) {
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.42));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.86));
  }
  const uint64_t healing_until =
      g_healing_vibration_until_ms.load(std::memory_order_acquire);
  if (now < healing_until) {
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.36));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.52));
  }
  const uint64_t selector_tick_until =
      g_selector_tick_vibration_until_ms.load(std::memory_order_acquire);
  if (now < selector_tick_until) {
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.10));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.24));
  }
  const uint64_t landing_until =
      g_landing_vibration_until_ms.load(std::memory_order_acquire);
  if (now < landing_until) {
    const uint32_t percent =
        g_landing_vibration_percent.load(std::memory_order_relaxed);
    left_motor = std::max(left_motor, VibrationMotorValue(percent, 1.00));
    right_motor = std::max(right_motor, VibrationMotorValue(percent, 0.28));
  }
  const uint64_t heavy_damage_until =
      g_heavy_damage_vibration_until_ms.load(std::memory_order_acquire);
  if (now < heavy_damage_until) {
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 1.00));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.92));
  }
  const uint64_t damage_until =
      g_damage_vibration_until_ms.load(std::memory_order_acquire);
  if (now < damage_until) {
    const uint32_t percent =
        g_damage_vibration_percent.load(std::memory_order_relaxed);
    left_motor = std::max(left_motor, VibrationMotorValue(percent, 1.00));
    right_motor = std::max(right_motor, VibrationMotorValue(percent, 0.65));
  }
  const uint64_t death_until =
      g_death_vibration_until_ms.load(std::memory_order_acquire);
  if (now < death_until) {
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 1.00));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.45));
  }
  ApplyControllerVibration(left_motor, right_motor);
  // Log only after SetState has received the motor command. File I/O must not
  // shorten an event envelope or postpone its first visible sample.
  if (successful_block_started) {
    AppendNativeLog("xinput vibration successful_block consumed "
                    "duration_ms=%u",
                    g_xinput_successful_block_vibration_ms);
  }
  if (spell_cast_started) {
    AppendNativeLog("xinput vibration spell_cast consumed duration_ms=%u",
                    g_xinput_spell_cast_vibration_ms);
  }
  if (ranged_shot_started) {
    AppendNativeLog("xinput vibration ranged_shot consumed duration_ms=%u",
                    g_xinput_ranged_shot_vibration_ms);
  }
  if (healing_started) {
    AppendNativeLog("xinput vibration healing consumed duration_ms=%u",
                    g_xinput_healing_vibration_ms);
  }
  if (landing_started) {
    AppendNativeLog("xinput vibration landing consumed duration_ms=%u "
                    "strength=%u",
                    g_xinput_landing_vibration_ms,
                    g_landing_vibration_percent.load(
                        std::memory_order_relaxed));
  }
}

bool ReadEntityHealth(void* target, int32_t* health) {
  if (!target || !health) {
    return false;
  }
  uintptr_t entity_data = 0;
  return SafeReadValue(reinterpret_cast<const uint8_t*>(target) +
                           kEntityDataOffset,
                       &entity_data) &&
         entity_data &&
         SafeReadValue(reinterpret_cast<const void*>(entity_data +
                                                      kEntityHealthOffset),
                       health);
}

uint32_t EventVibrationPercent(int32_t applied_damage, uint32_t base_percent,
                               uint32_t percent_per_hp) {
  const uint32_t whole_hp = static_cast<uint32_t>(std::max<int64_t>(
      1, (static_cast<int64_t>(applied_damage) + kHealthFixedScale - 1) /
             kHealthFixedScale));
  const uint32_t event_percent =
      std::min(100u, base_percent + whole_hp * percent_per_hp);
  return static_cast<uint32_t>((static_cast<uint64_t>(event_percent) *
                                g_xinput_vibration_strength_percent +
                                50u) /
                               100u);
}

void StartEventVibration(std::atomic<uint64_t>* deadline,
                         std::atomic<uint32_t>* strength, uint64_t now,
                         uint32_t duration_ms, uint32_t percent) {
  if (strength) {
    // Each confirmed event owns its intensity. The deadline still extends
    // monotonically, but a stale stronger hit must not permanently pin all
    // later impacts to the same motor level.
    strength->store(percent, std::memory_order_release);
  }
  const uint64_t desired = now + duration_ms;
  uint64_t observed = deadline->load(std::memory_order_relaxed);
  while (observed < desired &&
         !deadline->compare_exchange_weak(observed, desired,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
  }
}

int __cdecl HookMeleeAttackWindow(void* actor) {
  const int active = g_original_melee_attack_window(actor);
  uintptr_t player = 0;
  SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player);
  if (!player || reinterpret_cast<uintptr_t>(actor) != player) {
    return active;
  }

  const bool in_window = active != 0;
  const bool was_in_window = g_player_melee_attack_window_active.exchange(
      in_window, std::memory_order_acq_rel);
  if (in_window && !was_in_window) {
    const uint64_t now = GetTickCount64();
    // Attribution starts at the engine-confirmed downstroke, never at the raw
    // RT edge. This prevents attack rumble while the player is still blocking.
    g_last_controller_attack_ms.store(now, std::memory_order_release);
    StartEventVibration(&g_melee_swing_vibration_until_ms, nullptr, now,
                        g_xinput_melee_swing_vibration_ms,
                        g_xinput_vibration_strength_percent);

    uintptr_t attack_descriptor = 0;
    uint8_t start_frame = 0xFFu;
    uint8_t end_frame = 0xFFu;
    uint16_t animation_frame = 0xFFFFu;
    uintptr_t model_owner = 0;
    uintptr_t model = 0;
    if (SafeReadValue(reinterpret_cast<const uint8_t*>(actor) + 0x10u,
                      &model_owner) &&
        model_owner &&
        SafeReadValue(reinterpret_cast<const void*>(model_owner), &model) &&
        model) {
      SafeReadValue(reinterpret_cast<const void*>(model + 0x58u),
                    &animation_frame);
    }
    if (SafeReadValue(reinterpret_cast<const uint8_t*>(actor) + 0x10Cu,
                      &attack_descriptor) &&
        attack_descriptor) {
      SafeReadValue(reinterpret_cast<const void*>(attack_descriptor + 0x09u),
                    &start_frame);
      SafeReadValue(reinterpret_cast<const void*>(attack_descriptor + 0x0Au),
                    &end_frame);
    }
    int32_t weapon_id = -1;
    SafeReadValue(g_dungeon_base + kActiveCloseCombatWeaponRva, &weapon_id);
    AppendNativeLog(
        "game_event melee_downstroke actor=%08llX weapon=%d frame=%u "
        "window=%u..%u duration_ms=%u",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(actor)),
        weapon_id, static_cast<unsigned>(animation_frame),
        static_cast<unsigned>(start_frame), static_cast<unsigned>(end_frame),
        g_xinput_melee_swing_vibration_ms);
  }
  return active;
}

void __cdecl HookSuccessfulBlockImpact(void* actor) {
  // The retail collision resolver calls this only after its block-state
  // predicate, facing and geometry checks have accepted the contact. The
  // original routine installs block-impact animation 0x61; merely holding or
  // pressing LT never reaches this function.
  g_original_successful_block_impact(actor);
  uintptr_t player = 0;
  SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player);
  if (!player || reinterpret_cast<uintptr_t>(actor) != player) {
    return;
  }
  const bool was_pending = g_successful_block_vibration_pending.exchange(
      true, std::memory_order_acq_rel);
  if (was_pending) {
    return;
  }
  AppendNativeLog(
      "game_event successful_block queued actor=%08llX animation=97 "
      "source_rva=%08llX duration_ms=%u",
      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(actor)),
      static_cast<unsigned long long>(kSuccessfulBlockImpactRva),
      g_xinput_successful_block_vibration_ms);
}

void* __cdecl HookOffensiveSpellLaunch(void* actor, void* launch_context,
                                       void* launch_output) {
  int32_t spell_id = -1;
  SafeReadValue(g_dungeon_base + kActiveSpellRva, &spell_id);
  void* const projectile = g_original_offensive_spell_launch(
      actor, launch_context, launch_output);

  uintptr_t player = 0;
  SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player);
  const bool offensive_spell =
      spell_id >= kFirstOffensiveSpellId &&
      spell_id <= kLastOffensiveSpellId;
  if (!projectile || !player || reinterpret_cast<uintptr_t>(actor) != player ||
      !offensive_spell) {
    return projectile;
  }

  const uint64_t now = GetTickCount64();
  const bool was_pending = g_spell_cast_vibration_pending.exchange(
      true, std::memory_order_acq_rel);
  g_last_controller_attack_ms.store(now, std::memory_order_release);
  if (was_pending) {
    return projectile;
  }
  AppendNativeLog(
      "game_event offensive_spell_launch queued actor=%08llX spell=%d "
      "projectile=%08llX source_rva=%08llX duration_ms=%u",
      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(actor)),
      spell_id,
      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(projectile)),
      static_cast<unsigned long long>(kOffensiveSpellLaunchRva),
      g_xinput_spell_cast_vibration_ms);
  return projectile;
}

void* __cdecl HookRangedWeaponLaunch(void* actor, void* launch_context,
                                     void* launch_output) {
  int32_t weapon_id = -1;
  SafeReadValue(g_dungeon_base + kActiveCloseCombatWeaponRva, &weapon_id);
  void* const projectile = g_original_ranged_weapon_launch(
      actor, launch_context, launch_output);

  uintptr_t player = 0;
  SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player);
  if (!projectile || !player || reinterpret_cast<uintptr_t>(actor) != player) {
    return projectile;
  }

  const uint64_t now = GetTickCount64();
  g_last_controller_attack_ms.store(now, std::memory_order_release);
  const bool was_pending = g_ranged_shot_vibration_pending.exchange(
      true, std::memory_order_acq_rel);
  if (!was_pending) {
    AppendNativeLog(
        "game_event ranged_projectile queued actor=%08llX weapon=%d "
        "projectile=%08llX source_rva=%08llX duration_ms=%u",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(actor)),
        weapon_id,
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(projectile)),
        static_cast<unsigned long long>(kRangedWeaponLaunchRva),
        g_xinput_ranged_shot_vibration_ms);
  }
  return projectile;
}

void __cdecl HookUseConsumable(int32_t item_id) {
  uintptr_t player = 0;
  SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player);
  int32_t before = 0;
  const bool before_valid =
      player && ReadEntityHealth(reinterpret_cast<void*>(player), &before);

  g_original_use_consumable(item_id);

  int32_t after = 0;
  const bool after_valid =
      player && ReadEntityHealth(reinterpret_cast<void*>(player), &after);
  if (!before_valid || !after_valid || after <= before) {
    return;
  }

  const int32_t healed = after - before;
  const bool was_pending = g_healing_vibration_pending.exchange(
      true, std::memory_order_acq_rel);
  if (!was_pending) {
    AppendNativeLog(
        "game_event healing_consumable queued item=%d healed_q14=%d "
        "hp=%d->%d source_rva=%08llX duration_ms=%u",
        item_id, healed, before / kHealthFixedScale,
        after / kHealthFixedScale,
        static_cast<unsigned long long>(kUseConsumableRva),
        g_xinput_healing_vibration_ms);
  }
}

int __cdecl HookDamageHandler(void* target, int32_t requested_damage,
                              uintptr_t damage_flags, uintptr_t impact_event,
                              uintptr_t source) {
  int32_t before = 0;
  const bool before_valid = ReadEntityHealth(target, &before);
  const int result = g_original_damage_handler(
      target, requested_damage, damage_flags, impact_event, source);
  int32_t after = 0;
  const bool after_valid = ReadEntityHealth(target, &after);
  if (!before_valid || !after_valid || after >= before) {
    return result;
  }

  const int32_t applied_damage = before - after;
  uintptr_t player = 0;
  SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player);
  const bool player_damaged = player != 0 &&
                              reinterpret_cast<uintptr_t>(target) == player;
  const uint64_t now = GetTickCount64();
  const uint32_t before_hp = static_cast<uint32_t>(
      std::max<int64_t>(0, static_cast<int64_t>(before)) /
      kHealthFixedScale);
  const uint32_t after_hp = static_cast<uint32_t>(
      std::max<int64_t>(0, static_cast<int64_t>(after)) /
      kHealthFixedScale);

  if (player_damaged) {
    const uint32_t percent =
        EventVibrationPercent(applied_damage, 72u, 3u);
    StartEventVibration(&g_damage_vibration_until_ms,
                        &g_damage_vibration_percent, now,
                        g_xinput_damage_vibration_ms, percent);
    const uint32_t whole_damage_hp = static_cast<uint32_t>(
        (static_cast<int64_t>(applied_damage) + kHealthFixedScale - 1) /
        kHealthFixedScale);
    const bool heavy_damage =
        whole_damage_hp >= g_xinput_heavy_damage_threshold_hp;
    if (heavy_damage) {
      StartEventVibration(&g_heavy_damage_vibration_until_ms, nullptr, now,
                          g_xinput_heavy_damage_vibration_ms,
                          g_xinput_vibration_strength_percent);
      AppendNativeLog(
          "game_event heavy_player_impact hp=%u threshold=%u "
          "duration_ms=%u source=%08llX flags=%08llX",
          whole_damage_hp, g_xinput_heavy_damage_threshold_hp,
          g_xinput_heavy_damage_vibration_ms,
          static_cast<unsigned long long>(source),
          static_cast<unsigned long long>(damage_flags));
    }
    if (before > 0 && after <= 0) {
      StartEventVibration(&g_death_vibration_until_ms, nullptr, now,
                          g_xinput_death_vibration_ms,
                          g_xinput_vibration_strength_percent);
    }
    AppendNativeLog(
        "game_event player_damage target=%08llX requested_q14=%d "
        "applied_q14=%d hp=%u->%u death=%u result=%d source=%08llX "
        "impact_event=%08llX damage_flags=%08llX",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)),
        requested_damage, applied_damage, before_hp, after_hp,
        after <= 0 ? 1u : 0u, result,
        static_cast<unsigned long long>(source),
        static_cast<unsigned long long>(impact_event),
        static_cast<unsigned long long>(damage_flags));
    return result;
  }

  const uint64_t last_attack =
      g_last_controller_attack_ms.load(std::memory_order_acquire);
  // Attribute non-player damage to the controller only while a recent native
  // attack or spell action can plausibly have produced it. This prevents
  // ambient traps and enemy-on-enemy damage from vibrating the player's pad.
  if (last_attack != 0 && now >= last_attack && now - last_attack <= 1250u) {
    const uint32_t percent =
        EventVibrationPercent(applied_damage, 68u, 4u);
    StartEventVibration(&g_hit_vibration_until_ms, &g_hit_vibration_percent,
                        now, g_xinput_hit_vibration_ms, percent);
    AppendNativeLog(
        "game_event confirmed_hit target=%08llX requested_q14=%d "
        "applied_q14=%d hp=%u->%u result=%d source=%08llX "
        "impact_event=%08llX damage_flags=%08llX age_ms=%llu",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)),
        requested_damage, applied_damage, before_hp, after_hp, result,
        static_cast<unsigned long long>(source),
        static_cast<unsigned long long>(impact_event),
        static_cast<unsigned long long>(damage_flags),
        static_cast<unsigned long long>(now - last_attack));
  }
  return result;
}

void ReleaseInjectedControllerInput() {
  g_xinput_camera_relative_intent.store(false,
                                        std::memory_order_release);
  g_xinput_native_joystick_active.store(false,
                                         std::memory_order_release);
  g_xinput_native_joystick_x_milli.store(0, std::memory_order_release);
  g_xinput_native_joystick_y_milli.store(0, std::memory_order_release);
  g_xinput_camera_relative_was_active = false;
  g_xinput_camera_relative_runtime_failed = false;
  g_xinput_movement_controller.store(0, std::memory_order_release);
  g_immersive_xinput_movement_x_milli.store(
      0, std::memory_order_release);
  g_immersive_xinput_movement_y_milli.store(
      0, std::memory_order_release);
  for (size_t i = 0; i < g_injected_keys.size(); ++i) {
    InjectVirtualKey(static_cast<InjectedKey>(i), false);
  }
  InjectMouseLeft(false);
  InjectMouseRight(false);
  SubmitDeathtrapXInputMouseState(0, 0, false, false);
  SubmitDeathtrapXInputCombatState(false, false, false, false, false);
  g_xinput_first_person_toggled = false;
  g_xinput_run_active = false;
  g_xinput_menu_mode.store(true, std::memory_order_release);
  g_xinput_previous_native_gameplay = false;
  g_previous_xinput_buttons = 0;
  StopControllerVibration();
  ClearPendingControllerVibrationEvents();
}

bool LoadXInputRuntime() {
  if (g_xinput_get_state) {
    return true;
  }
  constexpr std::array<const wchar_t*, 3> kCandidates = {
      L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
  for (const wchar_t* candidate : kCandidates) {
    HMODULE module = LoadLibraryW(candidate);
    if (!module) {
      continue;
    }
    auto function = reinterpret_cast<DynamicXInputGetStateFn>(
        GetProcAddress(module, "XInputGetState"));
    if (function) {
      g_xinput_module = module;
      g_xinput_get_state = function;
      g_xinput_set_state = reinterpret_cast<DynamicXInputSetStateFn>(
          GetProcAddress(module, "XInputSetState"));
      return true;
    }
    FreeLibrary(module);
  }
  return false;
}

void PublishControllerSelector(bool visible, uint32_t category,
                               uint32_t slot, bool available,
                               bool confirmation_required) {
  uint32_t packed = 0;
  if (visible) {
    packed |= 1u;
  }
  if (available) {
    packed |= 2u;
  }
  if (confirmation_required) {
    packed |= 4u;
  }
  packed |= (category & 0xFu) << 8u;
  packed |= (slot & 0xFu) << 16u;
  g_controller_selector_overlay.store(packed, std::memory_order_release);
}

void SetNativeSelectorMode(uint8_t mode) {
  if (!g_dungeon_base) {
    return;
  }
  SafeWrite(g_dungeon_base + kUiSelectorModeRva, &mode, sizeof(mode));
}

#pragma pack(push, 1)
struct NativeInventorySlotDrawState {
  int16_t x = 0;
  int16_t y = 0;
  uint8_t icon = 0xFFu;
  uint8_t selected = 0;
  uint8_t available = 0;
  uint8_t slot = 0;
  uint8_t blank = 0;
  uint8_t reserved = 0;
  uint16_t quantity = 0;
};
#pragma pack(pop)
static_assert(sizeof(NativeInventorySlotDrawState) == 12u);

void __cdecl HookInventorySlotDraw(void* raw_slot) {
  if (!g_original_inventory_slot_draw || !raw_slot) {
    return;
  }
  if (!g_controller_selector.row_open ||
      g_controller_selector.cancelled) {
    g_original_inventory_slot_draw(raw_slot);
    return;
  }

  NativeInventorySlotDrawState slot = {};
  std::memcpy(&slot, raw_slot, sizeof(slot));
  if (slot.slot >= 8u) {
    g_original_inventory_slot_draw(raw_slot);
    return;
  }

  // The retail selector renderer uses a centered 640x480-style coordinate
  // space and 32-pixel cells. Reposition its own complete slot draw (frame,
  // icon, highlight, slot number and quantity) into a radial layout. No game
  // textures are copied, replaced or reimplemented here.
  constexpr double kPi = 3.14159265358979323846;
  const double angle = static_cast<double>(slot.slot) * kPi / 4.0;
  slot.x = static_cast<int16_t>(std::lround(
      std::sin(angle) * static_cast<double>(g_xinput_selector_radius) - 16.0));
  // Native selector Y grows upward from the bottom edge (unlike screen/D3D
  // coordinates), so positive cosine places stick-up at the top of the ring.
  slot.y = static_cast<int16_t>(std::lround(
      static_cast<double>(g_xinput_selector_center_y) +
      std::cos(angle) * static_cast<double>(g_xinput_selector_radius) - 16.0));
  slot.selected = slot.slot == g_controller_selector.slot ? 1u : 0u;
  g_original_inventory_slot_draw(&slot);
}

bool NativeInventoryItemAvailable(int32_t item_id) {
  using InventoryLookupFn = void*(__cdecl*)(int32_t);
  void* item = nullptr;
  __try {
    item = reinterpret_cast<InventoryLookupFn>(
        g_dungeon_base + kInventoryLookupRva)(item_id);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    item = nullptr;
  }
  return item != nullptr;
}

bool ControllerSlotAvailable(uint32_t category, uint32_t slot) {
  if (slot >= 8u) {
    return false;
  }
  switch (category) {
    case 1:
      return NativeWeaponAvailable(static_cast<int32_t>(slot));
    case 2:
      // The retail F2 selector exposes six ranged inventory objects, leaves
      // slot 7 empty and draws a dedicated chalk entry in slot 8.
      return (slot < 6u &&
              NativeInventoryItemAvailable(8 + static_cast<int32_t>(slot))) ||
             slot == 7u;
    case 3:
      return NativeInventoryItemAvailable(0x0E +
                                          static_cast<int32_t>(slot));
    case 4:
      return NativeInventoryItemAvailable(0x16 +
                                          static_cast<int32_t>(slot));
    default:
      return false;
  }
}

bool UseNativeChalk() {
  uintptr_t game_root = 0;
  uintptr_t gameplay_owner = 0;
  if (!SafeReadValue(g_dungeon_base + kGameRootPointerRva, &game_root) ||
      !game_root ||
      !SafeReadValue(reinterpret_cast<const void*>(game_root + 0x114u),
                     &gameplay_owner) ||
      !gameplay_owner) {
    AppendNativeLog("xinput chalk F2+8 owner unavailable");
    return false;
  }
  __try {
    reinterpret_cast<void(__cdecl*)(void*)>(
        g_dungeon_base + kUseChalkRva)(
        reinterpret_cast<void*>(gameplay_owner));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    AppendNativeLog("xinput chalk F2+8 native call failed owner=%08llX",
                    static_cast<unsigned long long>(gameplay_owner));
    return false;
  }
  ++g_xinput_chalk_actions_asserted;
  AppendNativeLog("xinput chalk F2+8 native call owner=%08llX total=%llu",
                  static_cast<unsigned long long>(gameplay_owner),
                  static_cast<unsigned long long>(
                      g_xinput_chalk_actions_asserted));
  return true;
}

uint32_t CurrentControllerSlot(uint32_t category) {
  int32_t active = -1;
  if (category == 1 || category == 2) {
    SafeReadValue(g_dungeon_base + kActiveCloseCombatWeaponRva, &active);
    if (category == 1 && active >= 0 && active <= 7) {
      return static_cast<uint32_t>(active);
    }
    if (category == 2 && active >= 8 && active <= 13) {
      return static_cast<uint32_t>(active - 8);
    }
  } else if (category == 3) {
    SafeReadValue(g_dungeon_base + 0x001D8A6Cu, &active);
    if (active >= 0x0E && active <= 0x15) {
      return static_cast<uint32_t>(active - 0x0E);
    }
  }
  return g_controller_selector.remembered_slot[category - 1u] & 7u;
}

bool CommitControllerSlot(uint32_t category, uint32_t slot) {
  if (!ControllerSlotAvailable(category, slot)) {
    return false;
  }
  __try {
    switch (category) {
      case 1: {
        constexpr std::array<int32_t, 8> kActions = {1, 2, 4, 5,
                                                     6, 7, 8, 0};
        reinterpret_cast<void(__cdecl*)(int32_t, int32_t)>(
            g_dungeon_base + kSelectCloseCombatWeaponRva)(kActions[slot], 1);
        break;
      }
      case 2: {
        if (slot == 7u) {
          // Match the retail F2+8 selector exactly. Its eighth entry does not
          // select a ranged inventory ID and does not use ACTION_CHALK_CROSS;
          // it invokes the dedicated chalk routine with the gameplay owner.
          if (!UseNativeChalk()) {
            return false;
          }
          break;
        }
        constexpr std::array<int32_t, 6> kActions = {
            0x10, 0x0E, 0x0F, 0x11, 0x0C, 0x0D};
        if (slot >= kActions.size()) {
          return false;
        }
        reinterpret_cast<void(__cdecl*)(int32_t, int32_t)>(
            g_dungeon_base + kSelectRangedWeaponRva)(kActions[slot], 1);
        break;
      }
      case 3:
        reinterpret_cast<void(__cdecl*)(int32_t)>(
            g_dungeon_base + kSelectSpellRva)(0x0E +
                                              static_cast<int32_t>(slot));
        break;
      case 4:
        reinterpret_cast<void(__cdecl*)(int32_t)>(
            g_dungeon_base + kUseConsumableRva)(0x16 +
                                                static_cast<int32_t>(slot));
        break;
      default:
        return false;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  g_controller_selector.remembered_slot[category - 1u] = slot;
  AppendNativeLog("xinput selector commit category=%u slot=%u", category,
                  slot + 1u);
  return true;
}

uint32_t NextAvailableControllerSlot(uint32_t category, uint32_t current) {
  for (uint32_t distance = 1; distance <= 8u; ++distance) {
    const uint32_t candidate = (current + distance) & 7u;
    if (ControllerSlotAvailable(category, candidate)) {
      return candidate;
    }
  }
  return current & 7u;
}

uint32_t DpadCategory(WORD buttons) {
  if (buttons & XINPUT_GAMEPAD_DPAD_UP) {
    return 1;
  }
  if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) {
    return 2;
  }
  if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) {
    return 3;
  }
  if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) {
    return 4;
  }
  return 0;
}

uint32_t RightStickSlot(const XINPUT_GAMEPAD& pad, uint32_t fallback) {
  const double x = static_cast<double>(pad.sThumbRX);
  const double y = static_cast<double>(pad.sThumbRY);
  if (x * x + y * y <
      static_cast<double>(g_xinput_right_deadzone) *
          static_cast<double>(g_xinput_right_deadzone)) {
    return fallback;
  }
  constexpr double kPi = 3.14159265358979323846;
  double angle = std::atan2(x, y);
  if (angle < 0.0) {
    angle += 2.0 * kPi;
  }
  return static_cast<uint32_t>(
             std::floor(angle / (kPi / 4.0) + 0.5)) &
         7u;
}

void CloseControllerSelector() {
  if (g_controller_selector.row_open) {
    SetNativeSelectorMode(0);
  }
  PublishControllerSelector(false, 0, 0, false, false);
  g_controller_selector = {};
}

void UpdateControllerSelector(const XINPUT_GAMEPAD& pad, bool gameplay) {
  const uint32_t category = gameplay ? DpadCategory(pad.wButtons) : 0u;
  const uint64_t now = GetTickCount64();
  if (!g_controller_selector.direction_down && category != 0u) {
    uint8_t native_mode = 0;
    if (!SafeReadValue(g_dungeon_base + kUiSelectorModeRva, &native_mode)) {
      return;
    }
    if (native_mode != 0) {
      // A keyboard-opened F1-F4 row remains latched after its key is released.
      // D-pad input explicitly takes ownership so the controller selector is
      // never permanently blocked by the old row.
      SetNativeSelectorMode(0);
      AppendNativeLog("xinput selector takeover native_mode=%u category=%u",
                      native_mode, category);
    }
    g_controller_selector.direction_down = true;
    g_controller_selector.category = category;
    g_controller_selector.slot = CurrentControllerSlot(category);
    g_controller_selector.pressed_ms = now;
    return;
  }
  if (!g_controller_selector.direction_down) {
    return;
  }
  if (category != 0u && category != g_controller_selector.category) {
    CloseControllerSelector();
    return;
  }
  if (category == 0u) {
    const uint64_t duration = now - g_controller_selector.pressed_ms;
    if (!g_controller_selector.row_open &&
        !g_controller_selector.cancelled && duration < g_xinput_selector_hold_ms &&
        g_controller_selector.category != 4u) {
      const uint32_t next = NextAvailableControllerSlot(
          g_controller_selector.category, g_controller_selector.slot);
      CommitControllerSlot(g_controller_selector.category, next);
    } else if (g_controller_selector.row_open &&
               !g_controller_selector.cancelled &&
               g_controller_selector.category != 2u &&
               g_controller_selector.category != 4u) {
      CommitControllerSlot(g_controller_selector.category,
                           g_controller_selector.slot);
    }
    CloseControllerSelector();
    return;
  }

  if (!g_controller_selector.row_open && !g_controller_selector.cancelled &&
      now - g_controller_selector.pressed_ms >= g_xinput_selector_hold_ms) {
    g_controller_selector.row_open = true;
    SetNativeSelectorMode(static_cast<uint8_t>(g_controller_selector.category));
    AppendNativeLog("xinput selector open category=%u",
                    g_controller_selector.category);
  }
  if (!g_controller_selector.row_open || g_controller_selector.cancelled) {
    return;
  }
  const uint32_t previous_slot = g_controller_selector.slot;
  g_controller_selector.slot =
      RightStickSlot(pad, g_controller_selector.slot);
  if (g_controller_selector.slot != previous_slot) {
    g_selector_tick_vibration_pending.store(true,
                                             std::memory_order_release);
    AppendNativeLog("game_event selector_tick category=%u slot=%u->%u "
                    "duration_ms=%u",
                    g_controller_selector.category, previous_slot,
                    g_controller_selector.slot,
                    g_xinput_selector_tick_vibration_ms);
  }
  const bool available = ControllerSlotAvailable(
      g_controller_selector.category, g_controller_selector.slot);
  PublishControllerSelector(true, g_controller_selector.category,
                            g_controller_selector.slot, available,
                            g_controller_selector.category == 2u ||
                                g_controller_selector.category == 4u);
  if (pad.wButtons & XINPUT_GAMEPAD_B) {
    g_controller_selector.cancelled = true;
    SetNativeSelectorMode(0);
    PublishControllerSelector(false, 0, 0, false, false);
    AppendNativeLog("xinput selector cancel category=%u",
                    g_controller_selector.category);
  } else if ((g_controller_selector.category == 2u ||
              g_controller_selector.category == 4u) &&
             available && !g_controller_selector.selection_confirmed &&
             (pad.wButtons & XINPUT_GAMEPAD_A)) {
    g_controller_selector.selection_confirmed = CommitControllerSlot(
        g_controller_selector.category, g_controller_selector.slot);
    g_controller_selector.cancelled = true;
    SetNativeSelectorMode(0);
    PublishControllerSelector(false, 0, 0, false, false);
  }
}

double NormalizedStick(SHORT value, int32_t deadzone) {
  const int32_t signed_value = static_cast<int32_t>(value);
  const int32_t magnitude = std::abs(signed_value);
  if (magnitude <= deadzone) {
    return 0.0;
  }
  const double normalized = static_cast<double>(magnitude - deadzone) /
                            static_cast<double>(32767 - deadzone);
  return signed_value < 0 ? -normalized : normalized;
}

struct NormalizedStick2 {
  double x = 0.0;
  double y = 0.0;
  double magnitude = 0.0;
};

NormalizedStick2 NormalizeCircularStick(SHORT raw_x, SHORT raw_y,
                                        int32_t deadzone) {
  const double x = static_cast<double>(raw_x);
  const double y = static_cast<double>(raw_y);
  const double raw_magnitude = std::hypot(x, y);
  if (!std::isfinite(raw_magnitude) || raw_magnitude <= deadzone ||
      raw_magnitude <= 0.0) {
    return {};
  }
  const double magnitude = std::clamp(
      (raw_magnitude - static_cast<double>(deadzone)) /
          static_cast<double>(32767 - deadzone),
      0.0, 1.0);
  return {x / raw_magnitude * magnitude,
          y / raw_magnitude * magnitude, magnitude};
}

int32_t NormalizePlayerHeading(int32_t heading) {
  heading %= kPlayerHeadingUnitsPerTurn;
  if (heading < 0) {
    heading += kPlayerHeadingUnitsPerTurn;
  }
  return heading;
}

int32_t PlayerHeadingDelta(int32_t target, int32_t current) {
  int32_t delta = NormalizePlayerHeading(target) -
                  NormalizePlayerHeading(current);
  if (delta > kPlayerHeadingUnitsPerTurn / 2) {
    delta -= kPlayerHeadingUnitsPerTurn;
  } else if (delta < -kPlayerHeadingUnitsPerTurn / 2) {
    delta += kPlayerHeadingUnitsPerTurn;
  }
  return delta;
}

bool ResolveLivePlayerMovementController(uintptr_t* outer_player,
                                         uintptr_t* controller) {
  if (!controller || !g_dungeon_base) {
    return false;
  }
  uintptr_t player = 0;
  uintptr_t movement_controller = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player) ||
      !player ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         player + kPlayerMovementControllerOffset),
                     &movement_controller) ||
      !movement_controller) {
    return false;
  }
  if (outer_player) {
    *outer_player = player;
  }
  *controller = movement_controller;
  return true;
}

bool ReadLivePlayerHeading(int32_t* heading,
                           uintptr_t* live_controller = nullptr) {
  if (!heading || !g_dungeon_base) {
    return false;
  }
  uintptr_t controller = 0;
  uintptr_t render_link = 0;
  uintptr_t render_node = 0;
  int32_t value = 0;
  if (!ResolveLivePlayerMovementController(nullptr, &controller) ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         controller + kPlayerRenderLinkOffset),
                     &render_link) ||
      !render_link ||
      !SafeReadValue(reinterpret_cast<const void*>(render_link),
                     &render_node) ||
      !render_node ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         render_node + kPlayerHeadingOffset),
                     &value)) {
    return false;
  }
  *heading = NormalizePlayerHeading(value);
  if (live_controller) {
    *live_controller = controller;
  }
  return true;
}

bool CameraRelativeDesiredHeading(const NormalizedStick2& stick,
                                  int32_t* heading) {
  if (!heading || stick.magnitude <= 0.0 ||
      !g_third_person_heading_reference_valid.load(
          std::memory_order_acquire)) {
    return false;
  }
  const double camera_yaw =
      static_cast<double>(g_third_person_heading_reference_microradians.load(
          std::memory_order_acquire)) /
      1000000.0;
  const CameraRelativeHeadingTarget target = CameraRelativeHeadingFromOrbit(
      camera_yaw, stick.x, stick.y, kPlayerHeadingUnitsPerTurn,
      g_xinput_camera_relative_invert_y);
  if (!target.valid) {
    return false;
  }
  *heading = target.heading;
  return true;
}

void PublishCameraRelativeMovementIntent(bool active, int32_t heading,
                                          double magnitude) {
  if (active && !g_xinput_camera_relative_was_active) {
    g_xinput_camera_relative_started_ms.store(GetTickCount64(),
                                               std::memory_order_release);
  } else if (!active) {
    g_xinput_camera_relative_started_ms.store(0,
                                               std::memory_order_release);
    g_xinput_movement_controller.store(0, std::memory_order_release);
  }
  g_xinput_desired_heading.store(NormalizePlayerHeading(heading),
                                 std::memory_order_release);
  g_xinput_movement_magnitude_milli.store(
      static_cast<int32_t>(std::lround(
          std::clamp(magnitude, 0.0, 1.0) * 1000.0)),
      std::memory_order_release);
  g_xinput_camera_relative_intent.store(active,
                                        std::memory_order_release);
  if (g_debug_log && active != g_xinput_camera_relative_was_active) {
    AppendNativeLog("xinput movement camera_relative=%u target=%d magnitude=%.3f",
                    active ? 1u : 0u, NormalizePlayerHeading(heading),
                    magnitude);
  }
  g_xinput_camera_relative_was_active = active;
}

void PublishImmersiveFirstPersonHeadingIntent() {
  int32_t current_heading = 0;
  uintptr_t controller = 0;
  if (!ReadLivePlayerHeading(&current_heading, &controller) || !controller) {
    return;
  }
  const CameraRelativeHeadingTarget target = CameraRelativeHeadingFromOrbit(
      g_third_person_orbit_state.yaw, 0.0, 1.0,
      kPlayerHeadingUnitsPerTurn, false);
  if (!target.valid) {
    return;
  }
  g_xinput_movement_controller.store(controller,
                                      std::memory_order_release);
  // Keep the body and equipped weapon aligned with the eye yaw even while
  // stationary. The existing dispatcher applies the bounded canonical heading
  // writer on the next player tick; movement, animation and collision remain
  // fully retail-owned.
  PublishCameraRelativeMovementIntent(true, target.heading, 1.0);
}

void PublishNativeJoystickMovement(bool active, double x, double y) {
  g_xinput_native_joystick_x_milli.store(
      static_cast<int32_t>(std::lround(std::clamp(x, -1.0, 1.0) * 1000.0)),
      std::memory_order_release);
  g_xinput_native_joystick_y_milli.store(
      static_cast<int32_t>(std::lround(std::clamp(y, -1.0, 1.0) * 1000.0)),
      std::memory_order_release);
  g_xinput_native_joystick_active.store(active,
                                         std::memory_order_release);
}

int __cdecl HookNativeJoystickPoll(int32_t* x, int32_t* y,
                                   uint32_t* buttons) {
  const int retail_result = g_original_native_joystick_poll
                                ? g_original_native_joystick_poll(x, y,
                                                                  buttons)
                                : 0;
  if (!x || !y || !buttons || !g_xinput_enabled ||
      !g_xinput_base_bindings ||
      !g_xinput_controller_present.load(std::memory_order_acquire) ||
      !g_xinput_native_joystick_active.load(std::memory_order_acquire)) {
    return retail_result;
  }

  constexpr int32_t kAxisCentre = 0x2000;
  constexpr int32_t kAxisExtent = 0x2000;
  const int32_t normalized_x = std::clamp(
      g_xinput_native_joystick_x_milli.load(std::memory_order_acquire),
      -1000, 1000);
  const int32_t normalized_y = std::clamp(
      g_xinput_native_joystick_y_milli.load(std::memory_order_acquire),
      -1000, 1000);
  *x = kAxisCentre + normalized_x * kAxisExtent / 1000;
  // DirectInput's low Y value is JOY_VERT_FORWARDS; XInput's positive Y is
  // physical stick-up, so the native axis is intentionally inverted here.
  *y = kAxisCentre - normalized_y * kAxisExtent / 1000;
  if (!retail_result) {
    *buttons = 0;
  }
  return 1;
}

int __cdecl HookNativeJoystickEnabled() {
  if (g_xinput_enabled && g_xinput_base_bindings &&
      g_xinput_controller_present.load(std::memory_order_acquire)) {
    return 1;
  }
  return g_original_native_joystick_enabled
             ? g_original_native_joystick_enabled()
             : 0;
}

bool ApplyCanonicalPlayerHeadingDelta(uintptr_t controller,
                                       int32_t heading_delta) {
  if (!controller || !g_player_turn_writer || heading_delta == 0) {
    return heading_delta == 0;
  }
  uintptr_t original_source = 0;
  int32_t original_limit = 0;
  int32_t original_idle_turn = 0;
  const uintptr_t temporary_source = controller + kPlayerIdleTurnOffset;
  const int32_t no_limit = 0;
  if (!SafeReadValue(reinterpret_cast<const void*>(
                         controller + kPlayerTurnSourcePointerOffset),
                     &original_source) ||
      !original_source ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         controller + kPlayerTurnLimitOffset),
                     &original_limit) ||
      !SafeReadValue(reinterpret_cast<const void*>(temporary_source),
                     &original_idle_turn)) {
    return false;
  }
  const bool source_written = SafeWrite(
      reinterpret_cast<void*>(controller + kPlayerTurnSourcePointerOffset),
      &temporary_source, sizeof(temporary_source));
  const bool limit_written = source_written && SafeWrite(
      reinterpret_cast<void*>(controller + kPlayerTurnLimitOffset),
      &no_limit, sizeof(no_limit));
  const bool delta_written = limit_written && SafeWrite(
      reinterpret_cast<void*>(temporary_source), &heading_delta,
      sizeof(heading_delta));
  if (delta_written) {
    // 0x44DD0 publishes the same heading to the render node and the native
    // actor/collision orientation. Never move or rotate only one copy.
    g_player_turn_writer(reinterpret_cast<void*>(controller));
  }
  SafeWrite(reinterpret_cast<void*>(temporary_source), &original_idle_turn,
            sizeof(original_idle_turn));
  SafeWrite(reinterpret_cast<void*>(controller + kPlayerTurnLimitOffset),
            &original_limit, sizeof(original_limit));
  SafeWrite(reinterpret_cast<void*>(controller +
                                    kPlayerTurnSourcePointerOffset),
            &original_source, sizeof(original_source));
  return delta_written;
}

bool ApplyCanonicalPlayerHeading(uintptr_t controller, int32_t target) {
  int32_t current = 0;
  uintptr_t live_controller = 0;
  if (!ReadLivePlayerHeading(&current, &live_controller) ||
      live_controller != controller) {
    return false;
  }
  return ApplyCanonicalPlayerHeadingDelta(
      controller, PlayerHeadingDelta(target, current));
}

bool ResolveImmersiveLocomotionPlan(ImmersiveLocomotionPlan* plan) {
  if (!plan || !CustomHeadViewSelected()) {
    return false;
  }
  double lateral = 0.0;
  double longitudinal = 0.0;
  bool native_backward = false;
  const int32_t keyboard_x = g_immersive_keyboard_movement_x.load(
      std::memory_order_acquire);
  const int32_t keyboard_y = g_immersive_keyboard_movement_y.load(
      std::memory_order_acquire);
  if (keyboard_x != 0 || keyboard_y != 0) {
    // Dungeon's scene-cache root publication uses the opposite horizontal
    // handedness from the physical head-view screen axis. The complete live
    // v0.0.212 endpoint proves A and D are mirrored while W/S are correct.
    // Mirror only the requested lateral component at the shared keyboard/
    // stick intent boundary so pure and diagonal movement retain one mapping.
    lateral = -static_cast<double>(std::clamp(keyboard_x, -1, 1));
    longitudinal = static_cast<double>(std::clamp(keyboard_y, -1, 1));
    native_backward = keyboard_y < 0;
  } else {
    lateral = -static_cast<double>(
        g_immersive_xinput_movement_x_milli.load(
            std::memory_order_acquire)) / 1000.0;
    longitudinal = static_cast<double>(
        g_immersive_xinput_movement_y_milli.load(
            std::memory_order_acquire)) / 1000.0;
    native_backward =
        longitudinal < -g_xinput_movement_threshold;
  }
  const double magnitude = std::min(1.0, std::hypot(lateral, longitudinal));
  if (magnitude <= 0.000001) {
    return false;
  }
  lateral /= magnitude;
  longitudinal /= magnitude;
  const CameraRelativeHeadingTarget motion_target =
      CameraRelativeHeadingFromOrbit(
          g_third_person_orbit_state.yaw, lateral, longitudinal,
          kPlayerHeadingUnitsPerTurn, false);
  if (!motion_target.valid) {
    return false;
  }
  *plan = BuildImmersiveLocomotionPlan(
      motion_target.heading, native_backward, magnitude,
      kPlayerHeadingUnitsPerTurn);
  return plan->active;
}

void __cdecl RouteImmersivePlayerRootMotion(void* node, int32_t local_x,
                                             int32_t local_y,
                                             int32_t local_z) {
  if (!g_player_root_motion_transform) {
    return;
  }

  const ImmersiveLocomotionPlan plan =
      g_immersive_locomotion_transaction_plan;
  uintptr_t controller = 0;
  uintptr_t render_link = 0;
  uintptr_t live_node = 0;
  int32_t matrix_xx = 0;
  int32_t matrix_zx = 0;
  int32_t matrix_xz = 0;
  int32_t matrix_zz = 0;
  const bool live_player_node =
      node && g_immersive_locomotion_transaction_active && plan.active &&
      ResolveLivePlayerMovementController(nullptr, &controller) &&
      SafeReadValue(reinterpret_cast<const void*>(
                        controller + kPlayerRenderLinkOffset),
                    &render_link) &&
      render_link &&
      SafeReadValue(reinterpret_cast<const void*>(render_link), &live_node) &&
      live_node == reinterpret_cast<uintptr_t>(node);
  if (live_player_node &&
      SafeReadValue(reinterpret_cast<const void*>(
                        live_node + kPlayerRootBasisXxOffset),
                    &matrix_xx) &&
      SafeReadValue(reinterpret_cast<const void*>(
                        live_node + kPlayerRootBasisZxOffset),
                    &matrix_zx) &&
      SafeReadValue(reinterpret_cast<const void*>(
                        live_node + kPlayerRootBasisXzOffset),
                    &matrix_xz) &&
      SafeReadValue(reinterpret_cast<const void*>(
                        live_node + kPlayerRootBasisZzOffset),
                    &matrix_zz)) {
    const ImmersiveRootMotionInput routed = BuildImmersiveRootMotionInput(
        local_x, local_z, matrix_xx, matrix_zx, matrix_xz, matrix_zz,
        plan.motion_heading, kPlayerHeadingUnitsPerTurn);
    if (routed.active) {
      g_player_root_motion_transform(node, routed.local_x, local_y,
                                     routed.local_z);
      ++g_immersive_root_motion_routes;
      if (g_debug_log && (g_immersive_root_motion_routes % 60u) == 1u) {
        AppendNativeLog(
            "immersive root_motion routed local=%d/%d result=%d/%d "
            "heading=%d basis=%d/%d/%d/%d node=%p total=%llu",
            local_x, local_z, routed.local_x, routed.local_z,
            plan.motion_heading, matrix_xx, matrix_zx, matrix_xz, matrix_zz,
            node, static_cast<unsigned long long>(
                      g_immersive_root_motion_routes));
      }
      return;
    }
  } else if (CustomHeadViewSelected()) {
    ++g_immersive_root_motion_rejections;
    if (g_debug_log &&
        (g_immersive_root_motion_rejections % 60u) == 1u) {
      AppendNativeLog(
          "immersive root_motion passthrough node=%p live_node=%p "
          "controller=%p total=%llu",
          node, reinterpret_cast<void*>(live_node),
          reinterpret_cast<void*>(controller),
          static_cast<unsigned long long>(
              g_immersive_root_motion_rejections));
    }
  }
  g_player_root_motion_transform(node, local_x, local_y, local_z);
}

struct ImmersiveLocomotionTransaction {
  ImmersiveLocomotionPlan plan{};
  int32_t preserved_body_heading = 0;
  uintptr_t controller = 0;
  bool active = false;
};

ImmersiveLocomotionTransaction BeginImmersiveLocomotionTransaction() {
  ImmersiveLocomotionTransaction transaction;
  const uintptr_t expected_controller =
      g_xinput_movement_controller.load(std::memory_order_acquire);
  transaction.active =
      !g_immersive_locomotion_transaction_active && expected_controller &&
      g_xinput_camera_relative_intent.load(std::memory_order_acquire) &&
      ResolveImmersiveLocomotionPlan(&transaction.plan) &&
      ReadLivePlayerHeading(&transaction.preserved_body_heading,
                            &transaction.controller) &&
      transaction.controller == expected_controller &&
      ApplyCanonicalPlayerHeading(expected_controller,
                                  transaction.plan.transaction_heading);
  g_immersive_locomotion_transaction_plan = transaction.plan;
  g_immersive_locomotion_transaction_active = transaction.active;
  return transaction;
}

void EndImmersiveLocomotionTransaction(
    const ImmersiveLocomotionTransaction& transaction) {
  g_immersive_locomotion_transaction_active = false;
  g_immersive_locomotion_transaction_plan = {};
  if (transaction.active) {
    ApplyCanonicalPlayerHeading(transaction.controller,
                                transaction.preserved_body_heading);
  }
}

uintptr_t __cdecl HookImmersivePlayerMovementStage() {
  if (!g_original_player_movement_stage) {
    return 0;
  }

  const ImmersiveLocomotionTransaction transaction =
      BeginImmersiveLocomotionTransaction();
  const uintptr_t result = g_original_player_movement_stage();
  EndImmersiveLocomotionTransaction(transaction);

  if (transaction.active && g_debug_log &&
      (g_source_ticks.load(std::memory_order_relaxed) % 60u) == 1u) {
    AppendNativeLog(
        "immersive movement_stage transaction motion_heading=%d "
        "collision_heading=%d restored_heading=%d native_axis=%d "
        "controller=%p",
        transaction.plan.motion_heading,
        transaction.plan.transaction_heading,
        transaction.preserved_body_heading,
        transaction.plan.native_axis_milli,
        reinterpret_cast<void*>(transaction.controller));
  }
  return result;
}

void __cdecl HookPlayerStateDispatcher(void* outer_player) {
  if (!g_original_player_state_dispatcher) {
    return;
  }
  // The complete 0x810A0 movement stage owns immersive heading/root routing.
  // Do not let this nested dispatcher start a shorter transaction or steer
  // the temporary collision course back toward the visible body heading.
  if (g_immersive_locomotion_transaction_active) {
    g_original_player_state_dispatcher(outer_player);
    return;
  }
  uintptr_t validated_controller = 0;
  if (outer_player && g_player_turn_writer &&
      g_xinput_camera_relative_intent.load(std::memory_order_acquire)) {
    uintptr_t expected_outer = 0;
    uintptr_t controller = 0;
    uintptr_t live_controller = 0;
    int32_t current_heading = 0;
    if (SafeReadValue(g_dungeon_base + kUiOwnerPointerRva,
                      &expected_outer) &&
        expected_outer == reinterpret_cast<uintptr_t>(outer_player) &&
        SafeReadValue(reinterpret_cast<const void*>(
                          expected_outer + kPlayerMovementControllerOffset),
                      &controller) &&
        controller == g_xinput_movement_controller.load(
                          std::memory_order_acquire) &&
        ReadLivePlayerHeading(&current_heading, &live_controller) &&
        live_controller == controller) {
      validated_controller = controller;
      const int32_t target = g_xinput_desired_heading.load(
          std::memory_order_acquire);
      const double magnitude = static_cast<double>(
          g_xinput_movement_magnitude_milli.load(
              std::memory_order_acquire)) / 1000.0;
      const double scaled_turn_degrees =
          g_xinput_movement_turn_degrees_per_tick *
          (0.55 + 0.45 * std::clamp(magnitude, 0.0, 1.0));
      const int32_t maximum_step = std::max(
          1, static_cast<int32_t>(std::lround(
                 scaled_turn_degrees * kPlayerHeadingUnitsPerTurn / 360.0)));
      const CameraRelativeHeadingStep steering =
          StepCameraRelativeHeading(target, current_heading,
                                    kPlayerHeadingUnitsPerTurn,
                                    maximum_step);
      if (steering.heading_delta != 0) {
        ApplyCanonicalPlayerHeadingDelta(controller,
                                         steering.heading_delta);
      }
      ++g_xinput_camera_relative_turn_calls;
      if (g_debug_log &&
          (g_xinput_camera_relative_turn_calls % 60u) == 1u) {
        AppendNativeLog(
            "xinput movement dispatcher_heading current=%d target=%d "
            "error=%d step=%d magnitude=%.3f controller=%p",
            current_heading, target, steering.heading_error,
            steering.heading_delta, magnitude,
            reinterpret_cast<void*>(controller));
      }
    }
  }

  // The native actor/collision course is resolved at the start of 0x82750,
  // before the animation root is published. Redirecting only the later root
  // vector leaves collision on the old forward course, so the following
  // native cycle pulls nominal A/D motion forward again. Publish both halves
  // as one transaction: temporarily set the canonical actor/collision course,
  // route all verified root contributions to the requested world course, then
  // restore the visible eye-facing body heading before presentation.
  ImmersiveLocomotionPlan locomotion_plan;
  int32_t preserved_body_heading = 0;
  uintptr_t heading_controller = 0;
  const bool route_immersive_motion =
      validated_controller &&
      ResolveImmersiveLocomotionPlan(&locomotion_plan) &&
      ReadLivePlayerHeading(&preserved_body_heading, &heading_controller) &&
      heading_controller == validated_controller &&
      ApplyCanonicalPlayerHeading(validated_controller,
                                  locomotion_plan.transaction_heading);
  g_immersive_locomotion_transaction_plan = locomotion_plan;
  g_immersive_locomotion_transaction_active = route_immersive_motion;
  g_original_player_state_dispatcher(outer_player);
  g_immersive_locomotion_transaction_active = false;
  g_immersive_locomotion_transaction_plan = {};
  if (route_immersive_motion) {
    ApplyCanonicalPlayerHeading(validated_controller,
                                preserved_body_heading);
    if (g_debug_log &&
        (g_source_ticks.load(std::memory_order_relaxed) % 60u) == 1u) {
      AppendNativeLog(
          "immersive locomotion transaction motion_heading=%d "
          "collision_heading=%d restored_heading=%d native_axis=%d "
          "controller=%p",
          locomotion_plan.motion_heading,
          locomotion_plan.transaction_heading, preserved_body_heading,
          locomotion_plan.native_axis_milli,
          reinterpret_cast<void*>(validated_controller));
    }
  }
}

void __cdecl HookPlayerLocomotion(void* controller) {
  if (!g_original_player_locomotion) {
    return;
  }
  if (!controller ||
      !g_xinput_camera_relative_intent.load(std::memory_order_acquire)) {
    g_original_player_locomotion(controller);
    return;
  }

  const uintptr_t live_controller = reinterpret_cast<uintptr_t>(controller);
  const uintptr_t expected_controller =
      g_xinput_movement_controller.load(std::memory_order_acquire);
  int32_t current_heading = 0;
  int32_t original_walk_turn = 0;
  int32_t original_run_turn = 0;
  uintptr_t render_link = 0;
  uintptr_t render_node = 0;
  if (!expected_controller || live_controller != expected_controller ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         live_controller + kPlayerRenderLinkOffset),
                     &render_link) ||
      !render_link ||
      !SafeReadValue(reinterpret_cast<const void*>(render_link),
                     &render_node) ||
      !render_node ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         render_node + kPlayerHeadingOffset),
                     &current_heading) ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         live_controller + kPlayerWalkTurnSourceOffset),
                     &original_walk_turn) ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         live_controller + kPlayerRunTurnSourceOffset),
                     &original_run_turn)) {
    ++g_xinput_camera_relative_rejections;
    if (g_debug_log &&
        (g_xinput_camera_relative_rejections % 60u) == 1u) {
      AppendNativeLog(
          "xinput movement locomotion_reject callback=%p expected=%p "
          "render_link=%p render_node=%p total=%llu",
          controller, reinterpret_cast<void*>(expected_controller),
          reinterpret_cast<void*>(render_link),
          reinterpret_cast<void*>(render_node),
          static_cast<unsigned long long>(
              g_xinput_camera_relative_rejections));
    }
    g_original_player_locomotion(controller);
    return;
  }
  current_heading = NormalizePlayerHeading(current_heading);

  if (g_immersive_locomotion_transaction_active) {
    // The dispatcher has already published the temporary movement course to
    // both actor and collision state. Do not let the ordinary eye-facing turn
    // servo rotate that course back toward the restored body heading before
    // the animation root is consumed.
    const int32_t no_turn = 0;
    const bool walk_written = SafeWrite(
        reinterpret_cast<void*>(live_controller + kPlayerWalkTurnSourceOffset),
        &no_turn, sizeof(no_turn));
    const bool run_written = SafeWrite(
        reinterpret_cast<void*>(live_controller + kPlayerRunTurnSourceOffset),
        &no_turn, sizeof(no_turn));
    g_original_player_locomotion(controller);
    if (walk_written) {
      SafeWrite(reinterpret_cast<void*>(
                    live_controller + kPlayerWalkTurnSourceOffset),
                &original_walk_turn, sizeof(original_walk_turn));
    }
    if (run_written) {
      SafeWrite(reinterpret_cast<void*>(
                    live_controller + kPlayerRunTurnSourceOffset),
                &original_run_turn, sizeof(original_run_turn));
    }
    return;
  }

  const int32_t target =
      g_xinput_desired_heading.load(std::memory_order_acquire);
  const int32_t error = PlayerHeadingDelta(target, current_heading);
  const double magnitude = static_cast<double>(
      g_xinput_movement_magnitude_milli.load(std::memory_order_acquire)) /
      1000.0;
  const double scaled_turn_degrees =
      g_xinput_movement_turn_degrees_per_tick *
      (0.55 + 0.45 * std::clamp(magnitude, 0.0, 1.0));
  const int32_t maximum_step = std::max(
      1, static_cast<int32_t>(std::lround(
             scaled_turn_degrees * kPlayerHeadingUnitsPerTurn / 360.0)));
  const int32_t turn_delta = std::clamp(error, -maximum_step, maximum_step);
  const bool walk_written = SafeWrite(
      reinterpret_cast<void*>(live_controller + kPlayerWalkTurnSourceOffset),
      &turn_delta, sizeof(turn_delta));
  const bool run_written = SafeWrite(
      reinterpret_cast<void*>(live_controller + kPlayerRunTurnSourceOffset),
      &turn_delta, sizeof(turn_delta));
  if (!walk_written || !run_written) {
    if (walk_written) {
      SafeWrite(reinterpret_cast<void*>(
                    live_controller + kPlayerWalkTurnSourceOffset),
                &original_walk_turn, sizeof(original_walk_turn));
    }
    if (run_written) {
      SafeWrite(reinterpret_cast<void*>(
                    live_controller + kPlayerRunTurnSourceOffset),
                &original_run_turn, sizeof(original_run_turn));
    }
    g_original_player_locomotion(controller);
    return;
  }

  g_original_player_locomotion(controller);
  SafeWrite(reinterpret_cast<void*>(
                live_controller + kPlayerWalkTurnSourceOffset),
            &original_walk_turn, sizeof(original_walk_turn));
  SafeWrite(reinterpret_cast<void*>(
                live_controller + kPlayerRunTurnSourceOffset),
            &original_run_turn, sizeof(original_run_turn));
  ++g_xinput_camera_relative_turn_calls;
  g_xinput_last_locomotion_steer_ms.store(GetTickCount64(),
                                           std::memory_order_release);
  if (g_debug_log &&
      (g_xinput_camera_relative_turn_calls % 60u) == 1u) {
    AppendNativeLog(
        "xinput movement locomotion_heading current=%d target=%d error=%d "
        "step=%d magnitude=%.3f controller=%p",
        current_heading, target, error, turn_delta, magnitude, controller);
  }
}

void __cdecl HookPlayerTurn(void* controller) {
  if (!g_original_player_turn) {
    return;
  }
  if (!controller ||
      !g_xinput_camera_relative_intent.load(std::memory_order_acquire)) {
    g_original_player_turn(controller);
    return;
  }

  uintptr_t live_controller = 0;
  int32_t current_heading = 0;
  uintptr_t turn_source = 0;
  int32_t original_turn = 0;
  if (!ReadLivePlayerHeading(&current_heading, &live_controller) ||
      live_controller != reinterpret_cast<uintptr_t>(controller) ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         live_controller + kPlayerTurnSourcePointerOffset),
                     &turn_source) ||
      turn_source < live_controller + 0x100u ||
      turn_source >= live_controller + 0x180u ||
      !SafeReadValue(reinterpret_cast<const void*>(turn_source),
                     &original_turn)) {
    g_original_player_turn(controller);
    return;
  }

  const int32_t target = g_xinput_desired_heading.load(
      std::memory_order_acquire);
  const int32_t error = PlayerHeadingDelta(target, current_heading);
  const double magnitude = static_cast<double>(
      g_xinput_movement_magnitude_milli.load(std::memory_order_acquire)) /
      1000.0;
  const double scaled_turn_degrees =
      g_xinput_movement_turn_degrees_per_tick *
      (0.55 + 0.45 * std::clamp(magnitude, 0.0, 1.0));
  const int32_t maximum_step = std::max(
      1, static_cast<int32_t>(std::lround(
             scaled_turn_degrees * kPlayerHeadingUnitsPerTurn / 360.0)));
  const int32_t turn_delta = std::clamp(error, -maximum_step, maximum_step);
  if (!SafeWrite(reinterpret_cast<void*>(turn_source), &turn_delta,
                 sizeof(turn_delta))) {
    g_original_player_turn(controller);
    return;
  }

  g_original_player_turn(controller);
  SafeWrite(reinterpret_cast<void*>(turn_source), &original_turn,
            sizeof(original_turn));
  ++g_xinput_camera_relative_turn_calls;
  if (g_debug_log &&
      (g_xinput_camera_relative_turn_calls % 60u) == 1u) {
    AppendNativeLog(
        "xinput movement heading current=%d target=%d error=%d step=%d "
        "magnitude=%.3f source=%p",
        current_heading, target, error, turn_delta, magnitude,
        reinterpret_cast<void*>(turn_source));
  }
}

void UpdateControllerBaseBindings(const XINPUT_GAMEPAD& pad, bool gameplay,
                                  bool selector_captures_controls) {
  UpdateControllerVibration(pad, gameplay, selector_captures_controls);
  if (!g_xinput_base_bindings) {
    PublishThirdPersonOrbitInput(0.0, 0.0, false);
    PublishCameraRelativeMovementIntent(false, 0, 0.0);
    PublishNativeJoystickMovement(false, 0.0, 0.0);
    g_xinput_direct_heading_steering_was_active = false;
    ReleaseInjectedControllerInput();
    return;
  }
  const double left_x = NormalizedStick(pad.sThumbLX, g_xinput_left_deadzone);
  const double left_y = NormalizedStick(pad.sThumbLY, g_xinput_left_deadzone);
  const NormalizedStick2 movement_stick = NormalizeCircularStick(
      pad.sThumbLX, pad.sThumbLY, g_xinput_left_deadzone);
  const double right_x =
      NormalizedStick(pad.sThumbRX, g_xinput_right_deadzone);
  const double right_y =
      NormalizedStick(pad.sThumbRY, g_xinput_right_deadzone);
  const WORD buttons = pad.wButtons;
  const WORD pressed = buttons & ~g_previous_xinput_buttons;
  if (gameplay) {
    if (!selector_captures_controls &&
        (pressed & XINPUT_GAMEPAD_X) != 0) {
      NotifyDeathtrapOperateInput();
    }
    const bool strafe_modifier =
        (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    const bool custom_head_movement = CustomHeadViewSelected();
    const bool first_person_movement =
        g_xinput_first_person_toggled.load(std::memory_order_acquire) ||
        RetailFirstPersonActive() || custom_head_movement;
    const bool controller_attack =
        !selector_captures_controls &&
        pad.bRightTrigger >= g_xinput_trigger_threshold;
    const auto observed_attack_direction =
        deathtrap::input::ResolveControllerCombatDirection(
            movement_stick.x, movement_stick.y,
            g_xinput_movement_threshold);
    const bool controller_combat_owns_stick = controller_attack;
    int32_t desired_heading = 0;
    int32_t current_heading = 0;
    uintptr_t current_controller = 0;
    const bool native_joystick_available =
        g_native_joystick_hooks_installed.load(std::memory_order_acquire);
    // While RT is held, the stick is a combat selector rather than a second
    // simultaneous locomotion source. Letting the native joystick keep moving
    // while F+direction was synthesized made side attacks stall and slide.
    const bool movement_requested =
        !controller_combat_owns_stick &&
        movement_stick.magnitude > g_xinput_movement_threshold;
    g_immersive_xinput_movement_x_milli.store(
        custom_head_movement && !selector_captures_controls &&
                movement_requested
            ? static_cast<int32_t>(std::lround(movement_stick.x * 1000.0))
            : 0,
        std::memory_order_release);
    g_immersive_xinput_movement_y_milli.store(
        custom_head_movement && !selector_captures_controls &&
                movement_requested
            ? static_cast<int32_t>(std::lround(movement_stick.y * 1000.0))
            : 0,
        std::memory_order_release);
    bool desired_heading_valid = false;
    if (custom_head_movement) {
      const CameraRelativeHeadingTarget view_heading =
          CameraRelativeHeadingFromOrbit(
              g_third_person_orbit_state.yaw, 0.0, 1.0,
              kPlayerHeadingUnitsPerTurn, false);
      if (view_heading.valid) {
        desired_heading = view_heading.heading;
        desired_heading_valid = true;
      }
    } else {
      desired_heading_valid =
          CameraRelativeDesiredHeading(movement_stick, &desired_heading);
    }
    const bool camera_relative_available =
        g_xinput_camera_relative_movement &&
        native_joystick_available &&
        g_player_state_dispatcher_hook_installed.load(
            std::memory_order_acquire) &&
        (!custom_head_movement ||
         g_immersive_root_motion_hooks_installed.load(
             std::memory_order_acquire)) &&
        !controller_combat_owns_stick &&
        !selector_captures_controls && !strafe_modifier &&
        (!first_person_movement || custom_head_movement) &&
        CustomCameraOwnsMode3() &&
        desired_heading_valid &&
        ReadLivePlayerHeading(&current_heading, &current_controller);
    if (camera_relative_available &&
        (movement_requested || custom_head_movement)) {
      if (g_debug_log && !g_xinput_camera_relative_was_active) {
        AppendNativeLog(
            "xinput movement stick raw=%d/%d normalized=%.3f/%.3f "
            "camera_y_invert=%u",
            static_cast<int>(pad.sThumbLX),
            static_cast<int>(pad.sThumbLY), movement_stick.x,
            movement_stick.y,
            g_xinput_camera_relative_invert_y ? 1u : 0u);
      }
      g_xinput_movement_controller.store(current_controller,
                                          std::memory_order_release);
      PublishCameraRelativeMovementIntent(
          true, desired_heading,
          custom_head_movement ? 1.0 : movement_stick.magnitude);
      // Native Y retains the game's action resolver, root motion, collision,
      // walk/run and animation ownership. In immersive view every lateral or
      // diagonal request supplies a forward/back driver; the verified root
      // transform callsites rotate it into the complete stick vector.
      const bool immersive_backward =
          custom_head_movement &&
          movement_stick.y < -g_xinput_movement_threshold;
      const double native_longitudinal =
          custom_head_movement && movement_requested
              ? (immersive_backward ? -movement_stick.magnitude
                                    : movement_stick.magnitude)
              : (custom_head_movement ? 0.0 : movement_stick.magnitude);
      PublishNativeJoystickMovement(
          true, 0.0, native_longitudinal);
      if (g_debug_log &&
          (!g_xinput_direct_heading_steering_was_active ||
           (g_source_ticks.load(std::memory_order_relaxed) % 60u) == 1u)) {
        AppendNativeLog(
            "xinput movement dispatcher_steering=1 target=%d "
            "current=%d native_axes=0.000/%.3f magnitude=%.3f",
            desired_heading, current_heading,
            native_longitudinal,
            movement_stick.magnitude);
      }
      g_xinput_direct_heading_steering_was_active = true;
      InjectVirtualKey(InjectedKey::kW, false);
      InjectVirtualKey(InjectedKey::kS, false);
      InjectVirtualKey(InjectedKey::kA, false);
      InjectVirtualKey(InjectedKey::kD, false);
      InjectVirtualKey(InjectedKey::kJ, false);
      InjectVirtualKey(InjectedKey::kK, false);
    } else {
      if (g_debug_log && g_xinput_direct_heading_steering_was_active) {
        AppendNativeLog(
            "xinput movement dispatcher_steering=0 reason=%s",
            movement_requested ? "native_context" : "stick_release");
      }
      g_xinput_direct_heading_steering_was_active = false;
      PublishCameraRelativeMovementIntent(false, desired_heading,
                                          movement_stick.magnitude);
      const bool side_step = strafe_modifier || first_person_movement;
      const auto joystick_movement =
          deathtrap::input::ResolveControllerJoystickMovement(
              native_joystick_available, selector_captures_controls,
              controller_combat_owns_stick, side_step, movement_stick.x,
              movement_stick.y);
      if (native_joystick_available) {
        // Keep the hook authoritative while combat owns the stick. Passing
        // active=false falls through to the game's original DirectInput poll,
        // where the same physical pad is still visible and would move Lara.
        PublishNativeJoystickMovement(
            joystick_movement.override_active, joystick_movement.x,
            joystick_movement.y);
        InjectVirtualKey(InjectedKey::kW, false);
        InjectVirtualKey(InjectedKey::kS, false);
        InjectVirtualKey(InjectedKey::kA, false);
        InjectVirtualKey(InjectedKey::kD, false);
      } else {
        // A failed native hook keeps the last verified keyboard-backed tank
        // mapping as a closed fallback instead of dropping movement.
        PublishNativeJoystickMovement(false, 0.0, 0.0);
        InjectVirtualKey(InjectedKey::kW,
                         !controller_combat_owns_stick &&
                             left_y > g_xinput_movement_threshold);
        InjectVirtualKey(InjectedKey::kS,
                         !controller_combat_owns_stick &&
                             left_y < -g_xinput_movement_threshold);
        InjectVirtualKey(InjectedKey::kA,
                         !controller_combat_owns_stick && !side_step &&
                             left_x < -g_xinput_movement_threshold);
        InjectVirtualKey(InjectedKey::kD,
                         !controller_combat_owns_stick && !side_step &&
                             left_x > g_xinput_movement_threshold);
      }
      // First person and LB retain the explicit side-step actions because the
      // retail joystick owns only one horizontal axis.
      InjectVirtualKey(InjectedKey::kJ,
                       !controller_combat_owns_stick && side_step &&
                           left_x < -g_xinput_movement_threshold);
      InjectVirtualKey(InjectedKey::kK,
                       !controller_combat_owns_stick && side_step &&
                           left_x > g_xinput_movement_threshold);
    }
    // In the head view the full two-axis stick selects walk/run. The former
    // abs(Y) test made a fully deflected pure strafe permanently walk.
    const double run_magnitude =
        controller_combat_owns_stick
            ? 0.0
            : custom_head_movement || camera_relative_available
            ? movement_stick.magnitude
            : std::abs(left_y);
    if (run_magnitude >= g_xinput_run_threshold) {
      g_xinput_run_active = true;
    } else if (run_magnitude <= g_xinput_run_release_threshold) {
      g_xinput_run_active = false;
    }
    InjectVirtualKey(InjectedKey::kShift, g_xinput_run_active);
    InjectVirtualKey(InjectedKey::kSpace,
                     !selector_captures_controls &&
                         (buttons & XINPUT_GAMEPAD_A));
    InjectVirtualKey(InjectedKey::kE, buttons & XINPUT_GAMEPAD_X);
    InjectVirtualKey(InjectedKey::kQ, buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
    // Chalk is dispatched only by the radial selector through the exact
    // retail F2+8 routine. Never synthesize the unrelated C binding here.
    InjectVirtualKey(InjectedKey::kC, false);
    // R3 toggles the game's original Tab-driven first-person mode. Keep this
    // separate from the overlay-owned immersive view: retail mode 4 is
    // a complete native gameplay state with its own look and culling path.
    if (!selector_captures_controls &&
        (pressed & XINPUT_GAMEPAD_RIGHT_THUMB) != 0) {
      const bool enabled =
          !g_xinput_first_person_toggled.load(std::memory_order_acquire);
      if (enabled && CustomHeadViewSelected()) {
        SetCustomHeadViewSelected(false, "R3");
      }
      g_xinput_first_person_toggled.store(enabled,
                                           std::memory_order_release);
      AppendNativeLog("xinput first_person=%u source=R3",
                      enabled ? 1u : 0u);
    }
    if (!selector_captures_controls &&
        (pressed & XINPUT_GAMEPAD_BACK) != 0) {
      ToggleCustomHeadView("SELECT");
    }
    const bool first_person_requested =
        g_xinput_first_person_toggled.load(std::memory_order_acquire);
    InjectVirtualKey(InjectedKey::kTab,
                     !selector_captures_controls && first_person_requested);
    SubmitDeathtrapXInputCombatState(
        controller_attack,
        observed_attack_direction.forward,
        observed_attack_direction.backward,
        observed_attack_direction.left,
        observed_attack_direction.right);
    // RT now publishes a virtual combat command instead of masquerading as a
    // physical mouse click. Mouse and controller still converge in the same
    // DirectInput F+direction translator.
    InjectMouseLeft(false);
    InjectMouseRight(!selector_captures_controls &&
                     pad.bLeftTrigger >= g_xinput_trigger_threshold);
    SubmitDeathtrapXInputMouseState(0, 0, false, false);
    InjectVirtualKey(InjectedKey::kUp, false);
    InjectVirtualKey(InjectedKey::kDown, false);
    InjectVirtualKey(InjectedKey::kLeft, false);
    InjectVirtualKey(InjectedKey::kRight, false);
    InjectVirtualKey(InjectedKey::kEnter, false);

    const bool first_person_active =
        first_person_requested || RetailFirstPersonActive();
    if (!selector_captures_controls && first_person_active) {
      InjectRelativeMouseMove(
          CurvedStick(right_x, g_xinput_right_stick_curve),
          CurvedStick(right_y, g_xinput_right_stick_curve),
          g_xinput_first_person_pixels);
    }
    PublishThirdPersonOrbitInput(
        right_x, right_y,
        !selector_captures_controls && !first_person_active &&
            CustomCameraOwnsMode3());
  } else {
    PublishCameraRelativeMovementIntent(false, 0, 0.0);
    PublishNativeJoystickMovement(false, 0.0, 0.0);
    g_xinput_direct_heading_steering_was_active = false;
    PublishThirdPersonOrbitInput(0.0, 0.0, false);
    g_xinput_first_person_toggled = false;
    g_xinput_run_active = false;
    g_immersive_xinput_movement_x_milli.store(
        0, std::memory_order_release);
    g_immersive_xinput_movement_y_milli.store(
        0, std::memory_order_release);
    InjectVirtualKey(InjectedKey::kW, false);
    InjectVirtualKey(InjectedKey::kS, false);
    InjectVirtualKey(InjectedKey::kA, false);
    InjectVirtualKey(InjectedKey::kD, false);
    InjectVirtualKey(InjectedKey::kJ, false);
    InjectVirtualKey(InjectedKey::kK, false);
    InjectVirtualKey(InjectedKey::kShift, false);
    // A is simultaneously the native mouse click and the keyboard confirm /
    // movie-skip key. X remains an alternate skip key for the retail screens
    // that bind Space but don't expose a clickable target.
    InjectVirtualKey(InjectedKey::kSpace,
                     (buttons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X)) != 0);
    InjectVirtualKey(InjectedKey::kE, false);
    InjectVirtualKey(InjectedKey::kQ, false);
    InjectVirtualKey(InjectedKey::kC, false);
    InjectVirtualKey(InjectedKey::kTab, false);
    SubmitDeathtrapXInputCombatState(false, false, false, false, false);
    InjectMouseLeft(false);
    InjectMouseRight(false);
    InjectVirtualKey(InjectedKey::kUp,
                     left_y > 0.35 || (buttons & XINPUT_GAMEPAD_DPAD_UP));
    InjectVirtualKey(InjectedKey::kDown,
                     left_y < -0.35 || (buttons & XINPUT_GAMEPAD_DPAD_DOWN));
    InjectVirtualKey(InjectedKey::kLeft,
                     left_x < -0.35 || (buttons & XINPUT_GAMEPAD_DPAD_LEFT));
    InjectVirtualKey(InjectedKey::kRight,
                     left_x > 0.35 || (buttons & XINPUT_GAMEPAD_DPAD_RIGHT));
    InjectVirtualKey(InjectedKey::kEnter,
                     (buttons & XINPUT_GAMEPAD_A) != 0);
    const int32_t menu_mouse_x = static_cast<int32_t>(std::lround(
        right_x * static_cast<double>(g_xinput_menu_mouse_pixels)));
    const double menu_y_sign = g_xinput_invert_right_y ? 1.0 : -1.0;
    const int32_t menu_mouse_y = static_cast<int32_t>(std::lround(
        right_y * menu_y_sign *
        static_cast<double>(g_xinput_menu_mouse_pixels)));
    SubmitDeathtrapXInputMouseState(
        menu_mouse_x, menu_mouse_y,
        (buttons & XINPUT_GAMEPAD_A) != 0, false);
  }
  InjectVirtualKey(InjectedKey::kEscape,
                   (buttons & XINPUT_GAMEPAD_START) ||
                       (!gameplay && (buttons & XINPUT_GAMEPAD_B)));
  g_previous_xinput_buttons = buttons;
}

bool RecentMode3CameraCallback() {
  const uint64_t last_mode3_ms =
      g_last_mode3_source_tick_ms.load(std::memory_order_acquire);
  const uint64_t now_ms = GetTickCount64();
  return last_mode3_ms && now_ms >= last_mode3_ms &&
         now_ms - last_mode3_ms <= 200u;
}

void ReconcileXInputFrontendOwnership(bool native_gameplay) {
  // Pause/front-end screens retain the live player pointer, so the presence
  // of a recent mode-3 gameplay camera callback is the authoritative owner.
  // This also repairs transitions made with a physical mouse: no XInput
  // Start edge is required to return the right stick to cursor duty.
  const bool gameplay_camera_active =
      RecentMode3CameraCallback() || RetailFirstPersonActive();
  const bool menu_mode = !native_gameplay || !gameplay_camera_active;
  const bool previous =
      g_xinput_menu_mode.exchange(menu_mode, std::memory_order_acq_rel);
  if (previous != menu_mode && g_debug_log) {
    AppendNativeLog("xinput frontend owner=%s source=camera_watchdog",
                    menu_mode ? "MENU" : "GAMEPLAY");
  }
}

void LogSupportControllerPresence(bool connected) {
  const int32_t current = connected ? 1 : 0;
  const int32_t previous = g_support_last_controller_connected.exchange(
      current, std::memory_order_acq_rel);
  if (previous != current) {
    AppendDeathtrapSupportLog("support_xinput controller=%u connected=%u",
                              g_xinput_controller_index,
                              connected ? 1u : 0u);
  }
}

void UpdateDeathtrapXInput() {
  if (!g_xinput_enabled || !LoadXInputRuntime()) {
    return;
  }
  ScopedXInputPoll poll(g_xinput_poll_guard);
  if (!poll) {
    return;
  }
  XINPUT_STATE state = {};
  const bool connected =
      g_xinput_get_state(g_xinput_controller_index, &state) == ERROR_SUCCESS;
  g_xinput_controller_present.store(connected, std::memory_order_release);
  LogSupportControllerPresence(connected);
  if (!connected || !IsGameForeground()) {
    PublishThirdPersonOrbitInput(0.0, 0.0, false);
    if (g_xinput_was_connected) {
      ReleaseInjectedControllerInput();
      CloseControllerSelector();
    }
    g_xinput_was_connected = connected;
    return;
  }
  if (!g_xinput_was_connected) {
    AppendNativeLog("xinput controller connected index=%u",
                    g_xinput_controller_index);
  }
  g_xinput_was_connected = true;
  const bool native_gameplay = DeathtrapGameplayReady(false);
  const WORD newly_pressed =
      state.Gamepad.wButtons & ~g_previous_xinput_buttons;
  ReconcileXInputFrontendOwnership(native_gameplay);
  g_xinput_previous_native_gameplay = native_gameplay;
  const bool gameplay =
      native_gameplay && !g_xinput_menu_mode.load(std::memory_order_acquire);
  // Frontend DirectInput polling owns movies and menus. The scheduler owns
  // only gameplay so engine actions and the native selector never run from a
  // foreign input thread.
  if (!gameplay) {
    PublishThirdPersonOrbitInput(0.0, 0.0, false);
    return;
  }
  if ((newly_pressed & XINPUT_GAMEPAD_START) != 0) {
    AppendNativeLog("xinput menu request=START dispatch=ESCAPE");
  }
  UpdateControllerSelector(state.Gamepad, gameplay);
  const bool selector_captures_controls =
      g_controller_selector.direction_down &&
      (g_controller_selector.row_open ||
       g_controller_selector.category == 2u ||
       g_controller_selector.category == 4u);
  UpdateControllerBaseBindings(state.Gamepad, gameplay,
                               selector_captures_controls);
}

void PollFrontendXInputInternal() {
  if (!g_xinput_enabled || !LoadXInputRuntime()) {
    return;
  }
  ScopedXInputPoll poll(g_xinput_poll_guard);
  if (!poll) {
    return;
  }
  XINPUT_STATE state = {};
  const bool connected =
      g_xinput_get_state(g_xinput_controller_index, &state) == ERROR_SUCCESS;
  g_xinput_controller_present.store(connected, std::memory_order_release);
  LogSupportControllerPresence(connected);
  if (!connected || !IsGameForeground()) {
    SubmitDeathtrapXInputMouseState(0, 0, false, false);
    SubmitDeathtrapXInputCombatState(false, false, false, false, false);
    return;
  }

  const bool native_gameplay = DeathtrapGameplayReady(false);
  ReconcileXInputFrontendOwnership(native_gameplay);
  const bool menu_mode =
      g_xinput_menu_mode.load(std::memory_order_acquire);
  if (native_gameplay && !menu_mode) {
    return;
  }

  // The frontend consumes the same retail keyboard/mouse actions as a real
  // user: right stick is the pointer, A is both click/confirm and the movie
  // skip key, B/Start is Escape, and the left stick/D-pad provide keyboard
  // navigation for screens that don't expose a mouse target.
  UpdateControllerBaseBindings(state.Gamepad, false, false);
}

// The old V26 experiment detoured complete engine functions. Those functions
// are shared by loading, UI, animation and gameplay paths, so observing them
// globally changed timing and could deadlock startup. This diagnostic instead
// rewrites only the concrete E8 call instructions in the single gameplay main
// loop at 0x10057CD0. The original functions and every other caller remain
// untouched. Probe wrappers only update fixed-size memory counters; file I/O is
// performed later by the already established render hook.
struct MovementStageSample {
  std::array<int32_t, 3> root{};
  std::array<int32_t, 3> cache{};
  std::array<int32_t, 3> bounds_a{};
  std::array<int32_t, 3> bounds_b{};
  uintptr_t player = 0;
  uintptr_t render_link = 0;
  uintptr_t render_node = 0;
  bool root_valid = false;
  bool cache_valid = false;
  bool bounds_a_valid = false;
  bool bounds_b_valid = false;
};

struct HiddenPlayerRootWriteWatch {
  uintptr_t page = 0;
  uintptr_t player_root = 0;
  uintptr_t write_address = 0;
  uintptr_t instruction = 0;
  DWORD original_protection = 0;
  DWORD thread_id = 0;
  DWORD rearm_thread_id = 0;
  bool armed = false;
  bool rearm_after_single_step = false;
  bool hit = false;
};

HiddenPlayerRootWriteWatch g_hidden_root_write_watch{};
PVOID g_hidden_root_write_veh = nullptr;
std::atomic<bool> g_hidden_root_writer_identified{false};
std::atomic<bool> g_hidden_root_watch_status_logged{false};

LONG CALLBACK HiddenPlayerRootWriteVectoredHandler(
    EXCEPTION_POINTERS* exception) {
  if (!exception || !exception->ExceptionRecord ||
      !exception->ContextRecord) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  HiddenPlayerRootWriteWatch& watch = g_hidden_root_write_watch;
  const DWORD code = exception->ExceptionRecord->ExceptionCode;
  if (code == EXCEPTION_SINGLE_STEP &&
      watch.rearm_after_single_step && watch.page &&
      watch.rearm_thread_id == GetCurrentThreadId()) {
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(watch.page), 4096u,
                   PAGE_READONLY, &ignored);
    watch.rearm_after_single_step = false;
    watch.rearm_thread_id = 0;
#if defined(_M_IX86)
    exception->ContextRecord->EFlags &= ~0x100u;
#endif
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (code != EXCEPTION_ACCESS_VIOLATION || !watch.armed || !watch.page ||
      exception->ExceptionRecord->NumberParameters < 2u ||
      exception->ExceptionRecord->ExceptionInformation[0] != 1u) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  const uintptr_t address = static_cast<uintptr_t>(
      exception->ExceptionRecord->ExceptionInformation[1]);
  if (address < watch.page || address >= watch.page + 4096u) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  DWORD ignored = 0;
  VirtualProtect(reinterpret_cast<void*>(watch.page), 4096u,
                 watch.original_protection, &ignored);
  if (address >= watch.player_root &&
      address < watch.player_root + 3u * sizeof(int32_t)) {
    watch.write_address = address;
#if defined(_M_IX86)
    watch.instruction = exception->ContextRecord->Eip;
#endif
    watch.hit = true;
    watch.thread_id = GetCurrentThreadId();
    watch.armed = false;
    watch.rearm_after_single_step = false;
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  watch.rearm_after_single_step = true;
  watch.rearm_thread_id = GetCurrentThreadId();
#if defined(_M_IX86)
  exception->ContextRecord->EFlags |= 0x100u;
#endif
  return EXCEPTION_CONTINUE_EXECUTION;
}

void FlushHiddenPlayerRootWriteWatch() {
  HiddenPlayerRootWriteWatch& watch = g_hidden_root_write_watch;
  if (watch.armed && watch.page) {
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(watch.page), 4096u,
                   watch.original_protection, &ignored);
  }
  watch.armed = false;
  watch.rearm_after_single_step = false;
  if (!watch.hit) {
    watch = {};
    return;
  }

  uintptr_t module_base = 0;
  const char* module_name = "UNKNOWN";
  const uintptr_t dungeon_base =
      reinterpret_cast<uintptr_t>(g_dungeon_base);
  const uintptr_t executable_base = reinterpret_cast<uintptr_t>(
      GetModuleHandleW(nullptr));
  if (watch.instruction >= dungeon_base &&
      watch.instruction < dungeon_base + 0x00400000u) {
    module_base = dungeon_base;
    module_name = "DUNGEON";
  } else if (executable_base && watch.instruction >= executable_base &&
             watch.instruction < executable_base + 0x01000000u) {
    module_base = executable_base;
    module_name = "DD_CD";
  }
  AppendNativeLog(
      "immersive hidden_root_writer instruction=%p module=%s rva=%08llX "
      "address=%p offset=%llu thread=%lu",
      reinterpret_cast<void*>(watch.instruction), module_name,
      static_cast<unsigned long long>(
          module_base ? watch.instruction - module_base : 0u),
      reinterpret_cast<void*>(watch.write_address),
      static_cast<unsigned long long>(
          watch.write_address - watch.player_root),
      static_cast<unsigned long>(watch.thread_id));
  g_hidden_root_writer_identified.store(true, std::memory_order_release);
  watch = {};
}

void ArmHiddenPlayerRootWriteWatch() {
  if (!g_debug_log || !CustomHeadViewSelected() ||
      g_hidden_root_writer_identified.load(std::memory_order_acquire)) {
    return;
  }
  ImmersiveLocomotionPlan plan;
  if (!ResolveImmersiveLocomotionPlan(&plan)) {
    return;
  }
  uintptr_t controller = 0;
  uintptr_t render_link = 0;
  uintptr_t player_root = 0;
  if (!ResolveLivePlayerMovementController(nullptr, &controller) ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         controller + kPlayerRenderLinkOffset),
                     &render_link) ||
      !render_link ||
      !SafeReadValue(reinterpret_cast<const void*>(render_link),
                     &player_root) ||
      !player_root) {
    return;
  }

  if (!g_hidden_root_write_veh) {
    g_hidden_root_write_veh = AddVectoredExceptionHandler(
        1u, HiddenPlayerRootWriteVectoredHandler);
  }
  if (!g_hidden_root_write_veh) {
    if (!g_hidden_root_watch_status_logged.exchange(
            true, std::memory_order_acq_rel)) {
      AppendNativeLog("immersive hidden_root_watch status=VEH_FAILED error=%lu",
                      static_cast<unsigned long>(GetLastError()));
    }
    return;
  }

  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  const uintptr_t page_size = system_info.dwPageSize;
  if (page_size != 4096u) {
    if (!g_hidden_root_watch_status_logged.exchange(
            true, std::memory_order_acq_rel)) {
      AppendNativeLog(
          "immersive hidden_root_watch status=PAGE_SIZE_UNSUPPORTED size=%llu",
          static_cast<unsigned long long>(page_size));
    }
    return;
  }
  HiddenPlayerRootWriteWatch& watch = g_hidden_root_write_watch;
  watch = {};
  watch.page = player_root & ~(page_size - 1u);
  watch.player_root = player_root;
  DWORD old_protection = 0;
  if (!VirtualProtect(reinterpret_cast<void*>(watch.page), page_size,
                      PAGE_READONLY, &old_protection)) {
    if (!g_hidden_root_watch_status_logged.exchange(
            true, std::memory_order_acq_rel)) {
      AppendNativeLog(
          "immersive hidden_root_watch status=PROTECT_FAILED page=%p "
          "error=%lu",
          reinterpret_cast<void*>(watch.page),
          static_cast<unsigned long>(GetLastError()));
    }
    watch = {};
    return;
  }
  watch.original_protection = old_protection;
  watch.armed = true;
  if (!g_hidden_root_watch_status_logged.exchange(
          true, std::memory_order_acq_rel)) {
    AppendNativeLog(
        "immersive hidden_root_watch status=ARMED page=%p root=%p "
        "protection=%08lX",
        reinterpret_cast<void*>(watch.page),
        reinterpret_cast<void*>(watch.player_root),
        static_cast<unsigned long>(watch.original_protection));
  }
}

struct MovementStageProbeStats {
  uint64_t calls = 0;
  uint64_t root_changes = 0;
  uint64_t cache_changes = 0;
  uint64_t bounds_a_changes = 0;
  uint64_t bounds_b_changes = 0;
  uint64_t root_reversals = 0;
  std::array<uint64_t, 3> root_absolute_delta{};
  std::array<uint32_t, 3> root_maximum_delta{};
  std::array<int32_t, 3> previous_root_delta{};
  std::array<int32_t, 3> last_root_before{};
  std::array<int32_t, 3> last_root_after{};
  bool previous_root_delta_valid = false;
};

std::array<MovementStageProbeStats, kMovementStageProbeCount>
    g_movement_stage_probe_stats{};

struct MovementCallbackProbeStats {
  uintptr_t target = 0;
  uintptr_t entry = 0;
  MovementStageProbeStats motion;
};

std::array<MovementCallbackProbeStats, kMovementCallbackProbeCount>
    g_movement_callback_probe_stats{};

bool CaptureMovementStageSample(MovementStageSample* sample) {
  if (!sample || !g_dungeon_base) {
    return false;
  }
  *sample = {};
  uintptr_t player = 0;
  uintptr_t render_link = 0;
  uintptr_t render_node = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player) ||
      !player) {
    return false;
  }
  sample->player = player;
  if (SafeReadValue(reinterpret_cast<const void*>(player + 0x10u),
                    &render_link) &&
      render_link &&
      SafeReadValue(reinterpret_cast<const void*>(render_link),
                    &render_node) &&
      render_node) {
    sample->render_link = render_link;
    sample->render_node = render_node;
    sample->root_valid = SafeRead(
        reinterpret_cast<const void*>(render_node), sample->root.data(),
        sizeof(sample->root));
  }
  sample->cache_valid = SafeRead(
      reinterpret_cast<const void*>(player + 0x70u), sample->cache.data(),
      sizeof(sample->cache));
  sample->bounds_a_valid = SafeRead(
      reinterpret_cast<const void*>(player + 0xA4u), sample->bounds_a.data(),
      sizeof(sample->bounds_a));
  sample->bounds_b_valid = SafeRead(
      reinterpret_cast<const void*>(player + 0xC4u), sample->bounds_b.data(),
      sizeof(sample->bounds_b));
  return sample->root_valid || sample->cache_valid ||
         sample->bounds_a_valid || sample->bounds_b_valid;
}

struct ImmersiveMovementUpdateTrace {
  MovementStageSample previous_tail{};
  MovementStageSample update_entry{};
  MovementStageSample stage_before{};
  MovementStageSample stage_after{};
  bool previous_tail_valid = false;
  bool active = false;
  bool stage_seen = false;
};

thread_local ImmersiveMovementUpdateTrace g_immersive_movement_update_trace{};

void BeginImmersiveMovementUpdateTrace(
    const MovementStageSample& update_entry) {
  ImmersiveMovementUpdateTrace& trace = g_immersive_movement_update_trace;
  if (!g_debug_log || !CustomHeadViewSelected()) {
    trace = {};
    return;
  }
  trace.update_entry = update_entry;
  trace.active = true;
  trace.stage_seen = false;
}

void RecordImmersiveMovementStagePhase(
    const MovementStageSample& before,
    const MovementStageSample& after) {
  ImmersiveMovementUpdateTrace& trace = g_immersive_movement_update_trace;
  if (!trace.active) {
    return;
  }
  trace.stage_before = before;
  trace.stage_after = after;
  trace.stage_seen = before.root_valid && after.root_valid;
}

void EndImmersiveMovementUpdateTrace(
    const MovementStageSample& update_tail) {
  ImmersiveMovementUpdateTrace& trace = g_immersive_movement_update_trace;
  if (!trace.active) {
    return;
  }
  ImmersiveLocomotionPlan plan;
  const bool plan_valid = ResolveImmersiveLocomotionPlan(&plan);
  const int32_t keyboard_x = g_immersive_keyboard_movement_x.load(
      std::memory_order_acquire);
  const int32_t keyboard_y = g_immersive_keyboard_movement_y.load(
      std::memory_order_acquire);
  const int32_t stick_x = g_immersive_xinput_movement_x_milli.load(
      std::memory_order_acquire);
  const int32_t stick_y = g_immersive_xinput_movement_y_milli.load(
      std::memory_order_acquire);

  static thread_local int32_t previous_keyboard_x = 0;
  static thread_local int32_t previous_keyboard_y = 0;
  static thread_local int32_t previous_stick_x = 0;
  static thread_local int32_t previous_stick_y = 0;
  static thread_local uint64_t active_samples = 0;
  ++active_samples;
  const bool input_changed =
      keyboard_x != previous_keyboard_x ||
      keyboard_y != previous_keyboard_y || stick_x != previous_stick_x ||
      stick_y != previous_stick_y;
  previous_keyboard_x = keyboard_x;
  previous_keyboard_y = keyboard_y;
  previous_stick_x = stick_x;
  previous_stick_y = stick_y;

  if (update_tail.root_valid && plan_valid &&
      (input_changed || (active_samples % 15u) == 1u)) {
    auto delta = [](const MovementStageSample& from,
                    const MovementStageSample& to,
                    size_t axis) -> long long {
      return from.root_valid && to.root_valid
                 ? static_cast<long long>(to.root[axis]) - from.root[axis]
                 : 0;
    };
    AppendNativeLog(
        "immersive update_truth tick=%llu keyboard=%d/%d stick=%d/%d "
        "course=%d stage_seen=%u between=%lld/%lld/%lld "
        "pre_stage=%lld/%lld/%lld stage=%lld/%lld/%lld "
        "post_stage=%lld/%lld/%lld total=%lld/%lld/%lld "
        "node=%p/%p/%p/%p root=%d/%d/%d->%d/%d/%d",
        static_cast<unsigned long long>(
            g_source_ticks.load(std::memory_order_relaxed)),
        keyboard_x, keyboard_y, stick_x, stick_y, plan.motion_heading,
        trace.stage_seen ? 1u : 0u,
        trace.previous_tail_valid
            ? delta(trace.previous_tail, trace.update_entry, 0u)
            : 0,
        trace.previous_tail_valid
            ? delta(trace.previous_tail, trace.update_entry, 1u)
            : 0,
        trace.previous_tail_valid
            ? delta(trace.previous_tail, trace.update_entry, 2u)
            : 0,
        trace.stage_seen
            ? delta(trace.update_entry, trace.stage_before, 0u)
            : 0,
        trace.stage_seen
            ? delta(trace.update_entry, trace.stage_before, 1u)
            : 0,
        trace.stage_seen
            ? delta(trace.update_entry, trace.stage_before, 2u)
            : 0,
        trace.stage_seen ? delta(trace.stage_before, trace.stage_after, 0u)
                         : 0,
        trace.stage_seen ? delta(trace.stage_before, trace.stage_after, 1u)
                         : 0,
        trace.stage_seen ? delta(trace.stage_before, trace.stage_after, 2u)
                         : 0,
        trace.stage_seen
            ? delta(trace.stage_after, update_tail, 0u)
            : delta(trace.update_entry, update_tail, 0u),
        trace.stage_seen
            ? delta(trace.stage_after, update_tail, 1u)
            : delta(trace.update_entry, update_tail, 1u),
        trace.stage_seen
            ? delta(trace.stage_after, update_tail, 2u)
            : delta(trace.update_entry, update_tail, 2u),
        delta(trace.update_entry, update_tail, 0u),
        delta(trace.update_entry, update_tail, 1u),
        delta(trace.update_entry, update_tail, 2u),
        reinterpret_cast<void*>(trace.previous_tail.render_node),
        reinterpret_cast<void*>(trace.update_entry.render_node),
        reinterpret_cast<void*>(trace.stage_before.render_node),
        reinterpret_cast<void*>(update_tail.render_node),
        trace.update_entry.root[0], trace.update_entry.root[1],
        trace.update_entry.root[2], update_tail.root[0],
        update_tail.root[1], update_tail.root[2]);
  }
  trace.previous_tail = update_tail;
  trace.previous_tail_valid = update_tail.root_valid;
  trace.active = false;
  trace.stage_seen = false;
}

void RecordMovementProbeStats(MovementStageProbeStats& stats,
                              const MovementStageSample& before,
                              const MovementStageSample& after) {
  ++stats.calls;
  if (before.cache_valid && after.cache_valid &&
      before.cache != after.cache) {
    ++stats.cache_changes;
  }
  if (before.bounds_a_valid && after.bounds_a_valid &&
      before.bounds_a != after.bounds_a) {
    ++stats.bounds_a_changes;
  }
  if (before.bounds_b_valid && after.bounds_b_valid &&
      before.bounds_b != after.bounds_b) {
    ++stats.bounds_b_changes;
  }
  if (!before.root_valid || !after.root_valid ||
      before.root == after.root) {
    return;
  }

  ++stats.root_changes;
  std::array<int32_t, 3> delta{};
  int64_t direction_dot = 0;
  for (size_t axis = 0; axis < delta.size(); ++axis) {
    const int64_t wide_delta =
        static_cast<int64_t>(after.root[axis]) - before.root[axis];
    delta[axis] = static_cast<int32_t>(std::clamp<int64_t>(
        wide_delta, std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max()));
    const uint32_t magnitude = static_cast<uint32_t>(
        std::min<int64_t>(std::llabs(wide_delta),
                          std::numeric_limits<uint32_t>::max()));
    stats.root_absolute_delta[axis] += magnitude;
    stats.root_maximum_delta[axis] =
        std::max(stats.root_maximum_delta[axis], magnitude);
    if (stats.previous_root_delta_valid) {
      direction_dot += static_cast<int64_t>(delta[axis]) *
                       stats.previous_root_delta[axis];
    }
  }
  if (stats.previous_root_delta_valid && direction_dot < 0) {
    ++stats.root_reversals;
  }
  stats.previous_root_delta = delta;
  stats.previous_root_delta_valid = true;
  stats.last_root_before = before.root;
  stats.last_root_after = after.root;
}

void RecordMovementStageProbe(size_t index,
                              const MovementStageSample& before,
                              const MovementStageSample& after) {
  if (index >= g_movement_stage_probe_stats.size()) {
    return;
  }
  RecordMovementProbeStats(g_movement_stage_probe_stats[index], before,
                           after);
}

void LogImmersiveMovementTruth(const MovementStageSample& before,
                               const MovementStageSample& after) {
  if (!g_debug_log || !before.root_valid || !after.root_valid ||
      !CustomHeadViewSelected()) {
    return;
  }

  const int32_t keyboard_x = g_immersive_keyboard_movement_x.load(
      std::memory_order_acquire);
  const int32_t keyboard_y = g_immersive_keyboard_movement_y.load(
      std::memory_order_acquire);
  const int32_t stick_x = g_immersive_xinput_movement_x_milli.load(
      std::memory_order_acquire);
  const int32_t stick_y = g_immersive_xinput_movement_y_milli.load(
      std::memory_order_acquire);
  const bool keyboard_active = keyboard_x != 0 || keyboard_y != 0;
  const bool stick_active = stick_x != 0 || stick_y != 0;
  if (!keyboard_active && !stick_active) {
    return;
  }

  ImmersiveLocomotionPlan plan;
  if (!ResolveImmersiveLocomotionPlan(&plan)) {
    return;
  }

  static int32_t previous_keyboard_x = 0;
  static int32_t previous_keyboard_y = 0;
  static int32_t previous_stick_x = 0;
  static int32_t previous_stick_y = 0;
  static uint64_t active_samples = 0;
  ++active_samples;
  const bool input_changed =
      keyboard_x != previous_keyboard_x ||
      keyboard_y != previous_keyboard_y || stick_x != previous_stick_x ||
      stick_y != previous_stick_y;
  previous_keyboard_x = keyboard_x;
  previous_keyboard_y = keyboard_y;
  previous_stick_x = stick_x;
  previous_stick_y = stick_y;
  if (!input_changed && (active_samples % 15u) != 1u) {
    return;
  }

  int32_t body_heading = 0;
  const bool body_heading_valid = ReadLivePlayerHeading(&body_heading);
  const int64_t delta_x =
      static_cast<int64_t>(after.root[0]) - before.root[0];
  const int64_t delta_y =
      static_cast<int64_t>(after.root[1]) - before.root[1];
  const int64_t delta_z =
      static_cast<int64_t>(after.root[2]) - before.root[2];
  auto projection_milli = [delta_x, delta_z](int32_t heading) {
    const double radians = static_cast<double>(heading) *
        2.0 * 3.14159265358979323846 /
        static_cast<double>(kPlayerHeadingUnitsPerTurn);
    return static_cast<long long>(std::llround(
        (std::sin(radians) * static_cast<double>(delta_x) +
         std::cos(radians) * static_cast<double>(delta_z)) *
        1000.0));
  };
  const long long desired_projection =
      projection_milli(plan.motion_heading);
  const long long body_projection = body_heading_valid
      ? projection_milli(body_heading)
      : 0;

  AppendNativeLog(
      "immersive movement_truth tick=%llu keyboard=%d/%d stick=%d/%d "
      "course=%d body=%d/%u root=%d/%d/%d->%d/%d/%d "
      "delta=%lld/%lld/%lld projection_milli=%lld/%lld "
      "cache_delta=%lld/%lld/%lld bounds_a_delta=%lld/%lld/%lld "
      "bounds_b_delta=%lld/%lld/%lld",
      static_cast<unsigned long long>(
          g_source_ticks.load(std::memory_order_relaxed)),
      keyboard_x, keyboard_y, stick_x, stick_y, plan.motion_heading,
      body_heading, body_heading_valid ? 1u : 0u, before.root[0],
      before.root[1], before.root[2], after.root[0], after.root[1],
      after.root[2], static_cast<long long>(delta_x),
      static_cast<long long>(delta_y), static_cast<long long>(delta_z),
      desired_projection, body_projection,
      before.cache_valid && after.cache_valid
          ? static_cast<long long>(after.cache[0]) - before.cache[0]
          : 0,
      before.cache_valid && after.cache_valid
          ? static_cast<long long>(after.cache[1]) - before.cache[1]
          : 0,
      before.cache_valid && after.cache_valid
          ? static_cast<long long>(after.cache[2]) - before.cache[2]
          : 0,
      before.bounds_a_valid && after.bounds_a_valid
          ? static_cast<long long>(after.bounds_a[0]) - before.bounds_a[0]
          : 0,
      before.bounds_a_valid && after.bounds_a_valid
          ? static_cast<long long>(after.bounds_a[1]) - before.bounds_a[1]
          : 0,
      before.bounds_a_valid && after.bounds_a_valid
          ? static_cast<long long>(after.bounds_a[2]) - before.bounds_a[2]
          : 0,
      before.bounds_b_valid && after.bounds_b_valid
          ? static_cast<long long>(after.bounds_b[0]) - before.bounds_b[0]
          : 0,
      before.bounds_b_valid && after.bounds_b_valid
          ? static_cast<long long>(after.bounds_b[1]) - before.bounds_b[1]
          : 0,
      before.bounds_b_valid && after.bounds_b_valid
          ? static_cast<long long>(after.bounds_b[2]) - before.bounds_b[2]
          : 0);
}

MovementCallbackProbeStats* FindMovementCallbackProbeStats(
    uintptr_t target, uintptr_t entry) {
  MovementCallbackProbeStats* empty = nullptr;
  for (MovementCallbackProbeStats& stats : g_movement_callback_probe_stats) {
    if (stats.target == target) {
      stats.entry = entry;
      return &stats;
    }
    if (!stats.target && !empty) {
      empty = &stats;
    }
  }
  if (empty) {
    empty->target = target;
    empty->entry = entry;
  }
  return empty;
}

void RecordPendingContactProjection(const MovementStageSample& before,
                                    const MovementStageSample& after,
                                    uintptr_t target) {
  if (!before.root_valid || !after.root_valid || before.root == after.root) {
    return;
  }
  std::array<int64_t, 3> delta{};
  bool nonzero = false;
  for (size_t axis = 0; axis < delta.size(); ++axis) {
    delta[axis] = static_cast<int64_t>(after.root[axis]) - before.root[axis];
    // A genuine 0x54D00 projection observed in V36 was only a few dozen
    // fixed-point units.  Reject corrupt/stale pointers without imposing a
    // gameplay-scale threshold on valid collision response.
    if (std::llabs(delta[axis]) > 4096ll) {
      return;
    }
    nonzero = nonzero || delta[axis] != 0;
  }
  if (!nonzero) {
    return;
  }

  const uintptr_t dungeon_base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  const bool resolver_projection =
      dungeon_base && target == dungeon_base + 0x00068390u;
  if (resolver_projection) {
    // V39 validation showed that 0x68390 also performs frequent 1-3 unit
    // bookkeeping adjustments. Promoting those to authoritative collision
    // projections incorrectly clears the proven contact fallback. Ordinary
    // 0x7E530 source motion was measured at up to 14 units per horizontal
    // axis, while the genuine sparse 0x68390 corrections were 52-90 units.
    // Only accept a resolver delta that exceeds that measured ordinary-step
    // envelope. 0x54D00 remains authoritative at any non-zero magnitude.
    const int64_t horizontal_magnitude =
        std::max(std::llabs(delta[0]), std::llabs(delta[2]));
    if (horizontal_magnitude <= 14ll) {
      ++g_contact_projection_68390_small_rejections;
      return;
    }
  }

  std::lock_guard<std::mutex> lock(g_contact_projection_mutex);
  for (size_t axis = 0; axis < delta.size(); ++axis) {
    g_pending_contact_projection[axis] = std::clamp<int64_t>(
        g_pending_contact_projection[axis] + delta[axis], -4096ll, 4096ll);
  }
  ++g_pending_contact_projection_sequence;
  ++g_contact_projection_captures;
  if (resolver_projection) {
    ++g_contact_projection_68390_captures;
  } else {
    ++g_contact_projection_54d00_captures;
  }
}

void ConsumePendingContactProjection(SceneSnapshot* snapshot) {
  if (!snapshot) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_contact_projection_mutex);
  bool nonzero = false;
  for (size_t axis = 0; axis < g_pending_contact_projection.size(); ++axis) {
    snapshot->player_contact_projection[axis] = static_cast<int32_t>(
        g_pending_contact_projection[axis]);
    nonzero = nonzero || g_pending_contact_projection[axis] != 0;
    g_pending_contact_projection[axis] = 0;
  }
  snapshot->player_contact_projection_sequence =
      g_pending_contact_projection_sequence;
  snapshot->player_contact_projection_valid =
      nonzero && snapshot->player != 0 && snapshot->player_position_valid;
  if (snapshot->player_contact_projection_valid) {
    ++g_contact_projection_consumes;
  }
}

uintptr_t __cdecl DispatchMovementCallbackProbe(uintptr_t target,
                                                 uintptr_t entry) {
  using Fn = uintptr_t(__cdecl*)(uintptr_t);
  const bool contact_projection_callback =
      g_dungeon_base &&
      (target == reinterpret_cast<uintptr_t>(g_dungeon_base) + 0x00054D00u ||
       target == reinterpret_cast<uintptr_t>(g_dungeon_base) + 0x00068390u);
  // Production keeps only the two measured collision projections. The broad
  // movement investigation is complete and must not add coordinate reads or
  // statistics to unrelated animation callbacks merely because compact
  // support logging is enabled.
  if (!contact_projection_callback) {
    return reinterpret_cast<Fn>(target)(entry);
  }
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  const uintptr_t result = reinterpret_cast<Fn>(target)(entry);
  CaptureMovementStageSample(&after);
  if (contact_projection_callback) {
    RecordPendingContactProjection(before, after, target);
  }
  return result;
}

#if defined(_M_IX86)
// At 0x100810CE the engine has already pushed the dispatcher entry and put
// its dynamic callback target in EAX. The original five bytes are
//   call eax; add esp, 4
// so this thunk duplicates those exact cdecl semantics and returns with the
// original argument removed. No callback is globally detoured.
__declspec(naked) uintptr_t MovementCallbackProbeThunk() {
  __asm {
    mov ecx, dword ptr [esp + 4]
    push ecx
    push eax
    call DispatchMovementCallbackProbe
    add esp, 8
    ret 4
  }
}

// At 0x10057789 the object update has pushed its controller argument and EDI
// points at the object vtable holder. The original six bytes are
//   call dword ptr [edi+18h]; add esp, 4
// This callsite-specific thunk preserves EDI and the exact cdecl cleanup
// while routing the dynamically selected target through the same read-only
// statistics collector.
__declspec(naked) uintptr_t MovementVtableCallbackProbeThunk() {
  __asm {
    mov eax, dword ptr [edi + 0x18]
    mov ecx, dword ptr [esp + 4]
    push ecx
    push eax
    call DispatchMovementCallbackProbe
    add esp, 8
    ret 4
  }
}
#endif

template <size_t Index, uintptr_t TargetRva>
uintptr_t __cdecl HookMovementStageNoArg() {
  if constexpr (Index == 2u) {
    FlushHiddenPlayerRootWriteWatch();
  }
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  if constexpr (Index == 0u) {
    BeginImmersiveMovementUpdateTrace(before);
  }
  using Fn = uintptr_t(__cdecl*)();
  const uintptr_t result =
      reinterpret_cast<Fn>(g_dungeon_base + TargetRva)();
  CaptureMovementStageSample(&after);
  RecordMovementStageProbe(Index, before, after);
  if constexpr (Index == 2u) {
    RecordImmersiveMovementStagePhase(before, after);
    LogImmersiveMovementTruth(before, after);
  }
  if constexpr (Index == 39u) {
    EndImmersiveMovementUpdateTrace(after);
  }
  return result;
}

template <size_t Index, uintptr_t TargetRva>
uintptr_t __cdecl HookMovementStageOneArg(uintptr_t argument) {
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  using Fn = uintptr_t(__cdecl*)(uintptr_t);
  const uintptr_t result =
      reinterpret_cast<Fn>(g_dungeon_base + TargetRva)(argument);
  CaptureMovementStageSample(&after);
  RecordMovementStageProbe(Index, before, after);
  if constexpr (Index == 29u) {
    ArmHiddenPlayerRootWriteWatch();
  }
  return result;
}

template <size_t Index, uintptr_t TargetRva>
uintptr_t __cdecl HookMovementStageTwoArgs(uintptr_t first,
                                           uintptr_t second) {
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  using Fn = uintptr_t(__cdecl*)(uintptr_t, uintptr_t);
  const uintptr_t result =
      reinterpret_cast<Fn>(g_dungeon_base + TargetRva)(first, second);
  CaptureMovementStageSample(&after);
  RecordMovementStageProbe(Index, before, after);
  return result;
}

template <size_t Index, uintptr_t TargetRva>
uintptr_t __cdecl HookMovementStageThreeArgs(uintptr_t first,
                                             uintptr_t second,
                                             uintptr_t third) {
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  using Fn = uintptr_t(__cdecl*)(uintptr_t, uintptr_t, uintptr_t);
  const uintptr_t result = reinterpret_cast<Fn>(
      g_dungeon_base + TargetRva)(first, second, third);
  CaptureMovementStageSample(&after);
  RecordMovementStageProbe(Index, before, after);
  return result;
}

struct MovementStageCallsite {
  uintptr_t callsite_rva;
  uintptr_t target_rva;
  const char* name;
  void* wrapper;
};

#define NOARG_STAGE(Index, Callsite, Target, Name)                         \
  {Callsite, Target, Name,                                                \
   reinterpret_cast<void*>(&HookMovementStageNoArg<Index, Target>)}
#define ONEARG_STAGE(Index, Callsite, Target, Name)                        \
  {Callsite, Target, Name,                                                \
   reinterpret_cast<void*>(&HookMovementStageOneArg<Index, Target>)}
#define TWOARG_STAGE(Index, Callsite, Target, Name)                        \
  {Callsite, Target, Name,                                                \
   reinterpret_cast<void*>(&HookMovementStageTwoArgs<Index, Target>)}
#define THREEARG_STAGE(Index, Callsite, Target, Name)                      \
  {Callsite, Target, Name,                                                \
   reinterpret_cast<void*>(&HookMovementStageThreeArgs<Index, Target>)}

const std::array<MovementStageCallsite, kMovementStageProbeCount>
    kMovementStageCallsites = {{
        NOARG_STAGE(0, 0x00057CF1u, 0x00007CA0u, "pre_07CA0"),
        NOARG_STAGE(1, 0x00057CF6u, 0x00040420u, "input_40420"),
        NOARG_STAGE(2, 0x00057CFBu, 0x000810A0u, "stage_810A0"),
        NOARG_STAGE(3, 0x00057D0Au, 0x00003D00u, "stage_03D00"),
        NOARG_STAGE(4, 0x00057D0Fu, 0x00059190u, "events_59190"),
        NOARG_STAGE(5, 0x00057D14u, 0x00093FF0u, "stage_93FF0"),
        NOARG_STAGE(6, 0x00057D19u, 0x00027130u, "stage_27130"),
        TWOARG_STAGE(7, 0x00057D22u, 0x00047D80u, "stage_47D80"),
        NOARG_STAGE(8, 0x00057D2Au, 0x000820C0u, "timers_820C0"),
        NOARG_STAGE(9, 0x00057D2Fu, 0x0001BA40u, "stage_1BA40"),
        NOARG_STAGE(10, 0x00057D34u, 0x0001F860u, "stage_1F860"),
        NOARG_STAGE(11, 0x00057D39u, 0x00049FE0u, "pre_render_49FE0"),
        NOARG_STAGE(12, 0x00057D4Du, 0x00049FE0u, "post_render_49FE0"),
        NOARG_STAGE(13, 0x00057D52u, 0x0001B9D0u, "stage_1B9D0"),
        NOARG_STAGE(14, 0x00057D57u, 0x0004DA90u, "effects_4DA90"),
        NOARG_STAGE(15, 0x00057D5Cu, 0x00096580u, "stage_96580"),
        ONEARG_STAGE(16, 0x00057D68u, 0x0001C2D0u, "player_1C2D0"),
        NOARG_STAGE(17, 0x00057D70u, 0x0009D8D0u, "stage_9D8D0"),
        NOARG_STAGE(18, 0x00057D75u, 0x000079C0u, "stage_079C0"),
        NOARG_STAGE(19, 0x00057D7Au, 0x0007D2A0u, "stage_7D2A0"),
        NOARG_STAGE(20, 0x00057D7Fu, 0x000237A0u, "stage_237A0"),
        NOARG_STAGE(21, 0x00057D84u, 0x00046080u, "stage_46080"),
        NOARG_STAGE(22, 0x00057D89u, 0x00040520u, "ui_40520"),
        ONEARG_STAGE(23, 0x00057D9Du, 0x0009A840u, "stage_9A840"),
        NOARG_STAGE(24, 0x00057DA5u, 0x0009A850u, "stage_9A850"),
        NOARG_STAGE(25, 0x00057DAAu, 0x00087A70u, "stage_87A70"),
        ONEARG_STAGE(26, 0x00057DB0u, 0x0000C3A0u, "stage_0C3A0"),
        NOARG_STAGE(27, 0x00057DBFu, 0x0007BB60u, "stage_7BB60"),
        ONEARG_STAGE(28, 0x00057DD3u, 0x0006C540u, "stage_6C540"),
        ONEARG_STAGE(29, 0x00057DDCu, 0x0001CCC0u, "cache_1CCC0"),
        ONEARG_STAGE(30, 0x00057DE5u, 0x0001DAB0u, "stage_1DAB0"),
        NOARG_STAGE(31, 0x00057DF3u, 0x00095890u, "stage_95890"),
        NOARG_STAGE(32, 0x00057DF8u, 0x00092BD0u, "stage_92BD0"),
        NOARG_STAGE(33, 0x00057DFDu, 0x0004B660u, "stage_4B660"),
        NOARG_STAGE(34, 0x00057E02u, 0x000878D0u, "stage_878D0"),
        NOARG_STAGE(35, 0x00057E07u, 0x00045770u, "stage_45770"),
        NOARG_STAGE(36, 0x00057E0Cu, 0x0009B290u, "stage_9B290"),
        NOARG_STAGE(37, 0x00057E11u, 0x000986C0u, "stage_986C0"),
        NOARG_STAGE(38, 0x00057E16u, 0x0004D5A0u, "stage_4D5A0"),
        NOARG_STAGE(39, 0x00057E1Bu, 0x00007CF0u, "tail_07CF0"),
        NOARG_STAGE(40, 0x0009001Cu, 0x000403F0u, "player_403F0"),
        ONEARG_STAGE(41, 0x0009002Au, 0x00069890u, "player_69890"),
        NOARG_STAGE(42, 0x0009003Fu, 0x00087000u, "player_87000"),
        NOARG_STAGE(43, 0x00090058u, 0x0002F920u, "player_2F920_a"),
        NOARG_STAGE(44, 0x00090061u, 0x0002F930u, "player_2F930_a"),
        NOARG_STAGE(45, 0x0009006Au, 0x0002F920u, "player_2F920_b"),
        NOARG_STAGE(46, 0x00090073u, 0x0002F930u, "player_2F930_b"),
        ONEARG_STAGE(47, 0x00090082u, 0x000904C0u, "player_904C0"),
        NOARG_STAGE(48, 0x0009008Au, 0x000860E0u, "player_860E0"),
        TWOARG_STAGE(49, 0x000900AFu, 0x00082840u, "player_82840_a"),
        ONEARG_STAGE(50, 0x000900D4u, 0x0008FDE0u, "player_8FDE0"),
        ONEARG_STAGE(51, 0x000900DDu, 0x0008FD20u, "player_8FD20"),
        ONEARG_STAGE(52, 0x000900E6u, 0x0008FC10u, "player_8FC10"),
        ONEARG_STAGE(53, 0x000900F4u, 0x000451C0u, "player_451C0"),
        ONEARG_STAGE(54, 0x000900FDu, 0x00053DD0u, "player_53DD0"),
        ONEARG_STAGE(55, 0x00090106u, 0x00083690u, "player_83690"),
        TWOARG_STAGE(56, 0x00090114u, 0x00082840u, "player_82840_b"),
        ONEARG_STAGE(57, 0x00090137u, 0x00082C70u, "player_82C70_a"),
        ONEARG_STAGE(58, 0x0009014Au, 0x00082C90u, "player_82C90"),
        TWOARG_STAGE(59, 0x00090160u, 0x00082840u, "player_82840_c"),
        ONEARG_STAGE(60, 0x0009016Du, 0x00083340u, "player_83340_a"),
        THREEARG_STAGE(61, 0x000901D4u, 0x0001D210u, "player_1D210"),
        TWOARG_STAGE(62, 0x000901F5u, 0x00082840u, "player_82840_d"),
        TWOARG_STAGE(63, 0x00090207u, 0x00082840u, "player_82840_e"),
        TWOARG_STAGE(64, 0x00090216u, 0x0001C480u, "player_1C480_a"),
        TWOARG_STAGE(65, 0x00090232u, 0x00082840u, "player_82840_f"),
        TWOARG_STAGE(66, 0x00090244u, 0x00082840u, "player_82840_g"),
        TWOARG_STAGE(67, 0x00090253u, 0x0001C480u, "player_1C480_b"),
        TWOARG_STAGE(68, 0x00090266u, 0x00082840u, "player_82840_h"),
        TWOARG_STAGE(69, 0x00090278u, 0x00082840u, "player_82840_i"),
        TWOARG_STAGE(70, 0x00090287u, 0x0001C480u, "player_1C480_c"),
        TWOARG_STAGE(71, 0x0009029Au, 0x00082840u, "player_82840_j"),
        TWOARG_STAGE(72, 0x000902ACu, 0x00082840u, "player_82840_k"),
        TWOARG_STAGE(73, 0x000902BBu, 0x0001C480u, "player_1C480_d"),
        TWOARG_STAGE(74, 0x000902CEu, 0x00082840u, "player_82840_l"),
        TWOARG_STAGE(75, 0x000902EFu, 0x00083440u, "player_83440"),
        TWOARG_STAGE(76, 0x00090302u, 0x00082840u, "player_82840_m"),
        ONEARG_STAGE(77, 0x00090313u, 0x00083340u, "player_83340_b"),
        ONEARG_STAGE(78, 0x00090333u, 0x000836D0u, "player_836D0"),
        THREEARG_STAGE(79, 0x00090367u, 0x0001CEC0u, "player_1CEC0_a"),
        THREEARG_STAGE(80, 0x0009039Du, 0x0001EC20u, "player_1EC20"),
        ONEARG_STAGE(81, 0x000903A6u, 0x0004E570u, "player_4E570"),
        THREEARG_STAGE(82, 0x000903F8u, 0x0001CEC0u, "player_1CEC0_b"),
        ONEARG_STAGE(83, 0x0009042Du, 0x00082C70u, "player_82C70_b"),
        TWOARG_STAGE(84, 0x00090455u, 0x00090610u, "player_90610"),
        TWOARG_STAGE(85, 0x0009046Fu, 0x00090740u, "player_90740"),
        ONEARG_STAGE(86, 0x0008275Du, 0x00057760u, "resolver_57760"),
        ONEARG_STAGE(87, 0x00082786u, 0x000833D0u, "resolver_833D0"),
        TWOARG_STAGE(88, 0x000827ADu, 0x000444A0u, "resolver_444A0"),
        THREEARG_STAGE(89, 0x000827BDu, 0x00044470u, "resolver_44470"),
        ONEARG_STAGE(90, 0x0005776Bu, 0x00057610u, "contact_57610"),
        THREEARG_STAGE(91, 0x00057778u, 0x00053CF0u, "contact_53CF0"),
        ONEARG_STAGE(92, 0x00057781u, 0x000528C0u, "contact_528C0"),
        ONEARG_STAGE(93, 0x00057791u, 0x00052990u, "contact_52990"),
        ONEARG_STAGE(94, 0x000577A0u, 0x00053E80u, "contact_53E80"),
    }};

#undef NOARG_STAGE
#undef ONEARG_STAGE
#undef TWOARG_STAGE
#undef THREEARG_STAGE

bool InstallImmersiveRootMotionHooks() {
  if (g_immersive_root_motion_hooks_installed.load(
          std::memory_order_acquire)) {
    return true;
  }

  constexpr std::array<uint8_t, 12> kExpectedWriterPrologue = {
      0x8B, 0x4C, 0x24, 0x04, 0x53, 0x8B,
      0x54, 0x24, 0x0C, 0x56, 0x57, 0x8B};
  constexpr std::array<uint8_t, 12> kExpectedStagePrologue = {
      0x56, 0x57, 0x8B, 0x3D, 0x70, 0x7A,
      0x1D, 0x10, 0x85, 0xFF, 0x74, 0x2E};
  std::array<uint8_t, kExpectedWriterPrologue.size()> writer_prologue{};
  std::array<uint8_t, kExpectedStagePrologue.size()> stage_prologue{};
  void* const writer_target =
      g_dungeon_base + kPlayerRootMotionTransformRva;
  void* const stage_target = g_dungeon_base + kPlayerMovementStageRva;
  const bool signatures_match =
      SafeRead(writer_target, writer_prologue.data(),
               writer_prologue.size()) &&
      writer_prologue == kExpectedWriterPrologue &&
      SafeRead(stage_target, stage_prologue.data(), stage_prologue.size()) &&
      stage_prologue == kExpectedStagePrologue;
  if (!signatures_match) {
    AppendNativeLog(
        "immersive root_motion signature_mismatch writer=%08llX "
        "stage=%08llX fallback=retail",
        static_cast<unsigned long long>(kPlayerRootMotionTransformRva),
        static_cast<unsigned long long>(kPlayerMovementStageRva));
    return false;
  }

  const MH_STATUS create_writer = MH_CreateHook(
      writer_target,
      reinterpret_cast<void*>(&RouteImmersivePlayerRootMotion),
      reinterpret_cast<void**>(&g_player_root_motion_transform));
  const MH_STATUS create_stage = MH_CreateHook(
      stage_target,
      reinterpret_cast<void*>(&HookImmersivePlayerMovementStage),
      reinterpret_cast<void**>(&g_original_player_movement_stage));
  const bool writer_created =
      create_writer == MH_OK || create_writer == MH_ERROR_ALREADY_CREATED;
  const bool stage_created =
      create_stage == MH_OK || create_stage == MH_ERROR_ALREADY_CREATED;
  if (!writer_created || !stage_created) {
    AppendNativeLog(
        "immersive root_motion create_failed writer=%d stage=%d "
        "fallback=retail",
        static_cast<int>(create_writer), static_cast<int>(create_stage));
    return false;
  }

  const MH_STATUS enable_writer = MH_EnableHook(writer_target);
  const MH_STATUS enable_stage = MH_EnableHook(stage_target);
  const bool writer_enabled =
      enable_writer == MH_OK || enable_writer == MH_ERROR_ENABLED;
  const bool stage_enabled =
      enable_stage == MH_OK || enable_stage == MH_ERROR_ENABLED;
  if (!writer_enabled || !stage_enabled) {
    if (writer_enabled) {
      MH_DisableHook(writer_target);
    }
    if (stage_enabled) {
      MH_DisableHook(stage_target);
    }
    AppendNativeLog(
        "immersive root_motion enable_failed writer=%d stage=%d "
        "fallback=retail",
        static_cast<int>(enable_writer), static_cast<int>(enable_stage));
    return false;
  }

  g_immersive_root_motion_hooks_installed.store(
      true, std::memory_order_release);
  AppendNativeLog(
      "immersive root_motion=active writer_rva=%08llX stage_rva=%08llX "
      "scope=LIVE_PLAYER_FULL_MOVEMENT_STAGE",
      static_cast<unsigned long long>(kPlayerRootMotionTransformRva),
      static_cast<unsigned long long>(kPlayerMovementStageRva));
  return true;
}

bool PatchMovementStageCallsite(const MovementStageCallsite& stage) {
  uint8_t* const callsite = g_dungeon_base + stage.callsite_rva;
  uint8_t opcode = 0;
  int32_t old_relative = 0;
  if (!SafeRead(callsite, &opcode, sizeof(opcode)) || opcode != 0xE8u ||
      !SafeRead(callsite + 1u, &old_relative, sizeof(old_relative))) {
    AppendNativeLog("movement_callsite_invalid name=%s callsite=%08llX",
                    stage.name,
                    static_cast<unsigned long long>(stage.callsite_rva));
    return false;
  }
  uint8_t* const old_target = callsite + 5u + old_relative;
  if (old_target != g_dungeon_base + stage.target_rva) {
    AppendNativeLog(
        "movement_callsite_target_mismatch name=%s callsite=%08llX "
        "actual_rva=%08llX expected_rva=%08llX",
        stage.name,
        static_cast<unsigned long long>(stage.callsite_rva),
        static_cast<unsigned long long>(old_target - g_dungeon_base),
        static_cast<unsigned long long>(stage.target_rva));
    return false;
  }
  const intptr_t wide_relative =
      reinterpret_cast<uint8_t*>(stage.wrapper) - (callsite + 5u);
  if (wide_relative < std::numeric_limits<int32_t>::min() ||
      wide_relative > std::numeric_limits<int32_t>::max()) {
    AppendNativeLog("movement_callsite_out_of_range name=%s", stage.name);
    return false;
  }
  const int32_t new_relative = static_cast<int32_t>(wide_relative);
  DWORD old_protection = 0;
  if (!VirtualProtect(callsite + 1u, sizeof(new_relative),
                      PAGE_EXECUTE_READWRITE, &old_protection)) {
    AppendNativeLog("movement_callsite_protect_failed name=%s", stage.name);
    return false;
  }
  const bool wrote = SafeWrite(callsite + 1u, &new_relative,
                               sizeof(new_relative));
  FlushInstructionCache(GetCurrentProcess(), callsite, 5u);
  DWORD ignored = 0;
  VirtualProtect(callsite + 1u, sizeof(new_relative), old_protection,
                 &ignored);
  return wrote;
}

bool InstallMovementStageCallsiteProbes() {
  if (!g_debug_log ||
      g_movement_stage_probes_installed.load(std::memory_order_acquire)) {
    return true;
  }
  size_t installed = 0;
  for (const MovementStageCallsite& stage : kMovementStageCallsites) {
    if (PatchMovementStageCallsite(stage)) {
      ++installed;
    }
  }
  const bool complete = installed == kMovementStageCallsites.size();
  g_movement_stage_probes_installed.store(complete,
                                           std::memory_order_release);
  AppendNativeLog("movement_callsite_probe_install installed=%zu requested=%zu",
                  installed, kMovementStageCallsites.size());
  return complete;
}

bool InstallMovementDispatcherCallbackProbe() {
  if (g_movement_callback_probe_installed.load(std::memory_order_acquire)) {
    return true;
  }
#if !defined(_M_IX86)
  return false;
#else
  constexpr std::array<uint8_t, 5> expected = {
      0xFFu, 0xD0u, 0x83u, 0xC4u, 0x04u};
  size_t installed = 0;
  for (const uintptr_t callback_rva : kMovementDynamicCallbackRvas) {
      const bool resolver_capture_callsite =
          std::find(kMovementResolverCallbackRvas.begin(),
                    kMovementResolverCallbackRvas.end(), callback_rva) !=
          kMovementResolverCallbackRvas.end();
      if (!resolver_capture_callsite) {
        continue;
      }
      uint8_t* const callsite = g_dungeon_base + callback_rva;
      std::array<uint8_t, 5> actual{};
      if (!SafeRead(callsite, actual.data(), actual.size()) ||
          actual != expected) {
        AppendNativeLog(
            "movement_callback_probe_invalid callsite=%08llX "
            "bytes=%02X%02X%02X%02X%02X",
            static_cast<unsigned long long>(callback_rva), actual[0],
            actual[1], actual[2], actual[3], actual[4]);
        continue;
      }
      const intptr_t wide_relative =
          reinterpret_cast<uint8_t*>(&MovementCallbackProbeThunk) -
          (callsite + 5u);
      if (wide_relative < std::numeric_limits<int32_t>::min() ||
          wide_relative > std::numeric_limits<int32_t>::max()) {
        AppendNativeLog(
            "movement_callback_probe_out_of_range callsite=%08llX",
            static_cast<unsigned long long>(callback_rva));
        continue;
      }
      std::array<uint8_t, 5> replacement{};
      replacement[0] = 0xE8u;
      const int32_t relative = static_cast<int32_t>(wide_relative);
      std::memcpy(replacement.data() + 1u, &relative, sizeof(relative));
      DWORD old_protection = 0;
      if (!VirtualProtect(callsite, replacement.size(),
                          PAGE_EXECUTE_READWRITE, &old_protection)) {
        AppendNativeLog(
            "movement_callback_probe_protect_failed callsite=%08llX",
            static_cast<unsigned long long>(callback_rva));
        continue;
      }
      const bool wrote =
          SafeWrite(callsite, replacement.data(), replacement.size());
      FlushInstructionCache(GetCurrentProcess(), callsite,
                            replacement.size());
      DWORD ignored = 0;
      VirtualProtect(callsite, replacement.size(), old_protection, &ignored);
      installed += wrote ? 1u : 0u;
  }
  constexpr std::array<uint8_t, 6> vtable_expected = {
      0xFFu, 0x57u, 0x18u, 0x83u, 0xC4u, 0x04u};
  uint8_t* const vtable_callsite =
      g_dungeon_base + kMovementVtableCallbackRva;
  std::array<uint8_t, 6> vtable_actual{};
  if (!SafeRead(vtable_callsite, vtable_actual.data(), vtable_actual.size()) ||
      vtable_actual != vtable_expected) {
    AppendNativeLog(
        "movement_callback_probe_invalid callsite=%08llX "
        "bytes=%02X%02X%02X%02X%02X%02X",
        static_cast<unsigned long long>(kMovementVtableCallbackRva),
        vtable_actual[0], vtable_actual[1], vtable_actual[2],
        vtable_actual[3], vtable_actual[4], vtable_actual[5]);
  } else {
    const intptr_t wide_relative =
        reinterpret_cast<uint8_t*>(&MovementVtableCallbackProbeThunk) -
        (vtable_callsite + 5u);
    if (wide_relative < std::numeric_limits<int32_t>::min() ||
        wide_relative > std::numeric_limits<int32_t>::max()) {
      AppendNativeLog(
          "movement_callback_probe_out_of_range callsite=%08llX",
          static_cast<unsigned long long>(kMovementVtableCallbackRva));
    } else {
      std::array<uint8_t, 6> replacement{};
      replacement[0] = 0xE8u;
      const int32_t relative = static_cast<int32_t>(wide_relative);
      std::memcpy(replacement.data() + 1u, &relative, sizeof(relative));
      replacement[5] = 0x90u;
      DWORD old_protection = 0;
      if (!VirtualProtect(vtable_callsite, replacement.size(),
                          PAGE_EXECUTE_READWRITE, &old_protection)) {
        AppendNativeLog(
            "movement_callback_probe_protect_failed callsite=%08llX",
            static_cast<unsigned long long>(kMovementVtableCallbackRva));
      } else {
        const bool wrote = SafeWrite(vtable_callsite, replacement.data(),
                                     replacement.size());
        FlushInstructionCache(GetCurrentProcess(), vtable_callsite,
                              replacement.size());
        DWORD ignored = 0;
        VirtualProtect(vtable_callsite, replacement.size(), old_protection,
                       &ignored);
        installed += wrote ? 1u : 0u;
      }
    }
  }
  const size_t requested = kMovementResolverCallbackRvas.size() + 1u;
  const bool complete = installed == requested;
  g_movement_callback_probe_installed.store(complete,
                                             std::memory_order_release);
  AppendNativeLog(
      "movement_callback_probe_install installed=%zu requested=%zu",
      installed, requested);
  return complete;
#endif
}

void FlushMovementStageProbeStats(uint64_t source_tick) {
  for (size_t index = 0; index < g_movement_stage_probe_stats.size();
       ++index) {
    MovementStageProbeStats& stats = g_movement_stage_probe_stats[index];
    if (!stats.root_changes && !stats.cache_changes &&
        !stats.bounds_a_changes && !stats.bounds_b_changes) {
      stats = {};
      continue;
    }
    const MovementStageCallsite& stage = kMovementStageCallsites[index];
    AppendNativeLog(
        "movement_callsite tick=%llu index=%zu name=%s calls=%llu "
        "root=%llu cache=%llu bounds_a=%llu bounds_b=%llu reversals=%llu "
        "abs_root=%llu/%llu/%llu max_root=%u/%u/%u "
        "last_root=%d/%d/%d->%d/%d/%d",
        static_cast<unsigned long long>(source_tick), index, stage.name,
        static_cast<unsigned long long>(stats.calls),
        static_cast<unsigned long long>(stats.root_changes),
        static_cast<unsigned long long>(stats.cache_changes),
        static_cast<unsigned long long>(stats.bounds_a_changes),
        static_cast<unsigned long long>(stats.bounds_b_changes),
        static_cast<unsigned long long>(stats.root_reversals),
        static_cast<unsigned long long>(stats.root_absolute_delta[0]),
        static_cast<unsigned long long>(stats.root_absolute_delta[1]),
        static_cast<unsigned long long>(stats.root_absolute_delta[2]),
        stats.root_maximum_delta[0], stats.root_maximum_delta[1],
        stats.root_maximum_delta[2], stats.last_root_before[0],
        stats.last_root_before[1], stats.last_root_before[2],
        stats.last_root_after[0], stats.last_root_after[1],
        stats.last_root_after[2]);
    stats = {};
  }
}

void FlushMovementCallbackProbeStats(uint64_t source_tick) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  for (size_t index = 0; index < g_movement_callback_probe_stats.size();
       ++index) {
    MovementCallbackProbeStats& callback =
        g_movement_callback_probe_stats[index];
    MovementStageProbeStats& stats = callback.motion;
    if (!callback.target || !stats.calls) {
      continue;
    }
    const uintptr_t target_rva =
        callback.target >= base &&
                callback.target < base + kExpectedImageSize
            ? callback.target - base
            : callback.target;
    uint32_t entry_state = 0xFFFFFFFFu;
    SafeReadValue(reinterpret_cast<const void*>(callback.entry + 0x14u),
                  &entry_state);
    AppendNativeLog(
        "movement_callback tick=%llu slot=%zu target=%08llX entry=%08llX "
        "state=%u calls=%llu root=%llu cache=%llu bounds_a=%llu "
        "bounds_b=%llu reversals=%llu abs_root=%llu/%llu/%llu "
        "max_root=%u/%u/%u last_root=%d/%d/%d->%d/%d/%d",
        static_cast<unsigned long long>(source_tick), index,
        static_cast<unsigned long long>(target_rva),
        static_cast<unsigned long long>(callback.entry), entry_state,
        static_cast<unsigned long long>(stats.calls),
        static_cast<unsigned long long>(stats.root_changes),
        static_cast<unsigned long long>(stats.cache_changes),
        static_cast<unsigned long long>(stats.bounds_a_changes),
        static_cast<unsigned long long>(stats.bounds_b_changes),
        static_cast<unsigned long long>(stats.root_reversals),
        static_cast<unsigned long long>(stats.root_absolute_delta[0]),
        static_cast<unsigned long long>(stats.root_absolute_delta[1]),
        static_cast<unsigned long long>(stats.root_absolute_delta[2]),
        stats.root_maximum_delta[0], stats.root_maximum_delta[1],
        stats.root_maximum_delta[2], stats.last_root_before[0],
        stats.last_root_before[1], stats.last_root_before[2],
        stats.last_root_after[0], stats.last_root_after[1],
        stats.last_root_after[2]);
    stats = {};
  }
}

double Dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Scale(const Vec3& value, double factor) {
  return {value.x * factor, value.y * factor, value.z * factor};
}

Vec3 Subtract(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

double Length(const Vec3& value) {
  return std::sqrt(Dot(value, value));
}

bool Normalize(Vec3* value) {
  const double length = Length(*value);
  if (!std::isfinite(length) || length < 1.0e-6) {
    return false;
  }
  *value = Scale(*value, 1.0 / length);
  return true;
}

Quaternion NormalizeQuaternion(Quaternion value) {
  const double length = std::sqrt(value.w * value.w + value.x * value.x +
                                  value.y * value.y + value.z * value.z);
  if (!std::isfinite(length) || length < 1.0e-8) {
    return {};
  }
  value.w /= length;
  value.x /= length;
  value.y /= length;
  value.z /= length;
  return value;
}

Quaternion QuaternionFromRows(const std::array<Vec3, 3>& rows) {
  const double m00 = rows[0].x;
  const double m01 = rows[0].y;
  const double m02 = rows[0].z;
  const double m10 = rows[1].x;
  const double m11 = rows[1].y;
  const double m12 = rows[1].z;
  const double m20 = rows[2].x;
  const double m21 = rows[2].y;
  const double m22 = rows[2].z;
  Quaternion q;
  const double trace = m00 + m11 + m22;
  if (trace > 0.0) {
    const double s = std::sqrt(trace + 1.0) * 2.0;
    q.w = 0.25 * s;
    q.x = (m21 - m12) / s;
    q.y = (m02 - m20) / s;
    q.z = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
    q.w = (m21 - m12) / s;
    q.x = 0.25 * s;
    q.y = (m01 + m10) / s;
    q.z = (m02 + m20) / s;
  } else if (m11 > m22) {
    const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
    q.w = (m02 - m20) / s;
    q.x = (m01 + m10) / s;
    q.y = 0.25 * s;
    q.z = (m12 + m21) / s;
  } else {
    const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
    q.w = (m10 - m01) / s;
    q.x = (m02 + m20) / s;
    q.y = (m12 + m21) / s;
    q.z = 0.25 * s;
  }
  return NormalizeQuaternion(q);
}

std::array<Vec3, 3> RowsFromQuaternion(const Quaternion& input) {
  const Quaternion q = NormalizeQuaternion(input);
  const double xx = q.x * q.x;
  const double yy = q.y * q.y;
  const double zz = q.z * q.z;
  const double xy = q.x * q.y;
  const double xz = q.x * q.z;
  const double yz = q.y * q.z;
  const double wx = q.w * q.x;
  const double wy = q.w * q.y;
  const double wz = q.w * q.z;
  return {{{1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz),
            2.0 * (xz + wy)},
           {2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz),
            2.0 * (yz - wx)},
           {2.0 * (xz - wy), 2.0 * (yz + wx),
            1.0 - 2.0 * (xx + yy)}}};
}

bool DecodeRigid(const Matrix3x4& matrix, RigidTransform* transform) {
  std::array<Vec3, 3> original{};
  for (size_t row = 0; row < 3; ++row) {
    original[row] = {
        matrix.values[row * 3 + 0] / kMatrixFixedScale,
        matrix.values[row * 3 + 1] / kMatrixFixedScale,
        matrix.values[row * 3 + 2] / kMatrixFixedScale};
  }
  const double sx = Length(original[0]);
  const double sy = Length(original[1]);
  const double sz = Length(original[2]);
  if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) ||
      sx < 0.01 || sy < 0.01 || sz < 0.01 ||
      sx > 100.0 || sy > 100.0 || sz > 100.0) {
    return false;
  }

  const Vec3 normalized0 = Scale(original[0], 1.0 / sx);
  const Vec3 normalized1 = Scale(original[1], 1.0 / sy);
  const Vec3 normalized2 = Scale(original[2], 1.0 / sz);
  if (std::abs(Dot(normalized0, normalized1)) > kMaximumBasisDot ||
      std::abs(Dot(normalized0, normalized2)) > kMaximumBasisDot ||
      std::abs(Dot(normalized1, normalized2)) > kMaximumBasisDot) {
    // Projection, shear and transitional visibility matrices are not rigid
    // object poses. Gram-Schmidt would make them look valid while changing
    // their actual meaning, so leave those nodes at the exact endpoint.
    return false;
  }

  Vec3 row0 = original[0];
  if (!Normalize(&row0)) {
    return false;
  }
  Vec3 row1 = Subtract(original[1], Scale(row0, Dot(original[1], row0)));
  if (!Normalize(&row1)) {
    return false;
  }
  Vec3 row2 = Cross(row0, row1);
  if (!Normalize(&row2) || Dot(row2, original[2]) < 0.0) {
    return false;
  }

  transform->rows = {row0, row1, row2};
  transform->scale = {sx, sy, sz};
  transform->translation = {
      matrix.values[9] / kMatrixFixedScale,
      matrix.values[10] / kMatrixFixedScale,
      matrix.values[11] / kMatrixFixedScale};
  transform->rotation = QuaternionFromRows(transform->rows);
  return std::isfinite(transform->translation.x) &&
         std::isfinite(transform->translation.y) &&
         std::isfinite(transform->translation.z);
}

double QuaternionAngularDistanceDegrees(Quaternion a, Quaternion b) {
  a = NormalizeQuaternion(a);
  b = NormalizeQuaternion(b);
  double dot = std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
  dot = std::clamp(dot, 0.0, 1.0);
  return 2.0 * std::acos(dot) * 180.0 / 3.14159265358979323846;
}

bool HasPlayerContactCorrection(const Matrix3x4& older,
                                const Matrix3x4& previous,
                                const Matrix3x4& current) {
  RigidTransform a;
  RigidTransform b;
  RigidTransform c;
  if (!DecodeRigid(older, &a) || !DecodeRigid(previous, &b) ||
      !DecodeRigid(current, &c)) {
    return false;
  }

  // Collision resolution moves the player root forward and immediately back
  // by only a few Q14 units. Do not expose an extra halfway pose on that
  // reverse leg. The exact native endpoints are left completely untouched.
  const Vec3 incoming = Subtract(b.translation, a.translation);
  const Vec3 outgoing = Subtract(c.translation, b.translation);
  const double incoming_length = Length(incoming);
  const double outgoing_length = Length(outgoing);
  const double maximum_leg = std::max(incoming_length, outgoing_length);
  if (incoming_length >= (1.0 / kMatrixFixedScale) &&
      outgoing_length >= (1.0 / kMatrixFixedScale) &&
      maximum_leg <= 0.25) {
    const double direction = Dot(incoming, outgoing) /
                             (incoming_length * outgoing_length);
    const double round_trip = Length(Subtract(c.translation, a.translation));
    if (std::isfinite(direction) && direction < -0.15 &&
        round_trip < 1.10 * maximum_leg) {
      return true;
    }
  }
  return false;
}

bool HasPlayerRawContactCorrection(
    const std::array<int32_t, 3>& older,
    const std::array<int32_t, 3>& previous,
    const std::array<int32_t, 3>& current) {
  const Vec3 a{static_cast<double>(older[0]),
               static_cast<double>(older[1]),
               static_cast<double>(older[2])};
  const Vec3 b{static_cast<double>(previous[0]),
               static_cast<double>(previous[1]),
               static_cast<double>(previous[2])};
  const Vec3 c{static_cast<double>(current[0]),
               static_cast<double>(current[1]),
               static_cast<double>(current[2])};
  const Vec3 incoming = Subtract(b, a);
  const Vec3 outgoing = Subtract(c, b);
  const double incoming_length = Length(incoming);
  const double outgoing_length = Length(outgoing);
  const double maximum_leg = std::max(incoming_length, outgoing_length);
  if (incoming_length < 1.0 || outgoing_length < 1.0 ||
      !std::isfinite(maximum_leg)) {
    return false;
  }

  // The gameplay coordinate is corrected before the render cache is built.
  // At a blocked wall it therefore forms a very tight A -> B -> A pattern,
  // even when the derived world matrix has already hidden part of the kick.
  // Restrict this to a strong reversal returning close to A so ordinary
  // forward motion and animation changes remain fully interpolated.
  const double direction =
      Dot(incoming, outgoing) / (incoming_length * outgoing_length);
  const double round_trip = Length(Subtract(c, a));
  return std::isfinite(direction) && direction < -0.35 &&
         round_trip < 0.45 * maximum_leg;
}

void RecordAxisReturnBins(const Vec3& older, const Vec3& previous,
                          const Vec3& current, double minimum_leg,
                          double maximum_leg, AxisReturnBins* bins) {
  const std::array<double, 3> a = {older.x, older.y, older.z};
  const std::array<double, 3> b = {previous.x, previous.y, previous.z};
  const std::array<double, 3> c = {current.x, current.y, current.z};
  for (size_t axis = 0; axis < a.size(); ++axis) {
    const double incoming = b[axis] - a[axis];
    const double outgoing = c[axis] - b[axis];
    const double largest = std::max(std::abs(incoming), std::abs(outgoing));
    if (std::abs(incoming) < minimum_leg ||
        std::abs(outgoing) < minimum_leg || largest > maximum_leg ||
        incoming * outgoing >= 0.0) {
      continue;
    }
    const double return_ratio = std::abs(c[axis] - a[axis]) / largest;
    if (!std::isfinite(return_ratio) || return_ratio >= 1.0) {
      continue;
    }
    const size_t bin = return_ratio < 0.25 ? 0u
                       : return_ratio < 0.50 ? 1u
                       : return_ratio < 0.75 ? 2u
                                             : 3u;
    ++(*bins)[axis][bin];
  }
}

void RecordPlayerAxisTelemetry(const SceneSnapshot& older,
                               const SceneSnapshot& previous,
                               const SceneSnapshot& current,
                               uintptr_t player_render_node) {
  const auto older_player = older.nodes.find(player_render_node);
  const auto previous_player = previous.nodes.find(player_render_node);
  const auto current_player = current.nodes.find(player_render_node);
  if (older_player != older.nodes.end() &&
      previous_player != previous.nodes.end() &&
      current_player != current.nodes.end()) {
    RigidTransform a;
    RigidTransform b;
    RigidTransform c;
    if (DecodeRigid(older_player->second.world, &a) &&
        DecodeRigid(previous_player->second.world, &b) &&
        DecodeRigid(current_player->second.world, &c)) {
      RecordAxisReturnBins(a.translation, b.translation, c.translation,
                           1.0 / kMatrixFixedScale, 0.25,
                           &g_matrix_axis_return_bins);
    }
  }
  if (older.player_position_valid && previous.player_position_valid &&
      current.player_position_valid) {
    const Vec3 a{static_cast<double>(older.player_position[0]),
                 static_cast<double>(older.player_position[1]),
                 static_cast<double>(older.player_position[2])};
    const Vec3 b{static_cast<double>(previous.player_position[0]),
                 static_cast<double>(previous.player_position[1]),
                 static_cast<double>(previous.player_position[2])};
    const Vec3 c{static_cast<double>(current.player_position[0]),
                 static_cast<double>(current.player_position[1]),
                 static_cast<double>(current.player_position[2])};
    RecordAxisReturnBins(a, b, c, 1.0,
                         std::numeric_limits<double>::max(),
                         &g_raw_axis_return_bins);
  }
}

uintptr_t ResolvePlayerRenderNode(const SceneSnapshot& scene) {
  uintptr_t player = 0;
  uintptr_t render_link = 0;
  uintptr_t render_node = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player) ||
      !player ||
      !SafeReadValue(reinterpret_cast<const void*>(player + 0x10u),
                     &render_link) ||
      !render_link ||
      !SafeReadValue(reinterpret_cast<const void*>(render_link),
                     &render_node) ||
      !render_node || scene.nodes.find(render_node) == scene.nodes.end()) {
    return 0;
  }
  return render_node;
}

bool ReadPlayerObject(uintptr_t* player) {
  if (!player || !g_dungeon_base) {
    return false;
  }
  uintptr_t value = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &value) || !value) {
    return false;
  }
  *player = value;
  return true;
}

void LogPlayerProbeTriplet(uint64_t source_tick,
                           const SceneSnapshot& older,
                           const SceneSnapshot& previous,
                           const SceneSnapshot& current) {
  if (!g_debug_log || source_tick - g_last_player_probe_log_tick < 6u ||
      !older.player_position_valid || !previous.player_position_valid ||
      !current.player_position_valid) {
    return;
  }
  g_last_player_probe_log_tick = source_tick;
  auto value = [](const std::array<int32_t, 3>& position, size_t axis) {
    return static_cast<long long>(position[axis]);
  };
  AppendNativeLog(
      "player_probe tick=%llu object=%08llX contacts=%u/%u/%u "
      "node_x=%lld/%lld/%lld node_y=%lld/%lld/%lld node_z=%lld/%lld/%lld "
      "cache_x=%lld/%lld/%lld cache_y=%lld/%lld/%lld "
      "cache_z=%lld/%lld/%lld bounds_a_x=%lld/%lld/%lld "
      "bounds_a_y=%lld/%lld/%lld bounds_a_z=%lld/%lld/%lld "
      "bounds_b_x=%lld/%lld/%lld bounds_b_y=%lld/%lld/%lld "
      "bounds_b_z=%lld/%lld/%lld valid_cache=%u/%u/%u "
      "valid_a=%u/%u/%u valid_b=%u/%u/%u",
      static_cast<unsigned long long>(source_tick),
      static_cast<unsigned long long>(current.player_object),
      older.player_contact_count, previous.player_contact_count,
      current.player_contact_count, value(older.player_position, 0),
      value(previous.player_position, 0), value(current.player_position, 0),
      value(older.player_position, 1), value(previous.player_position, 1),
      value(current.player_position, 1), value(older.player_position, 2),
      value(previous.player_position, 2), value(current.player_position, 2),
      value(older.player_cached_position, 0),
      value(previous.player_cached_position, 0),
      value(current.player_cached_position, 0),
      value(older.player_cached_position, 1),
      value(previous.player_cached_position, 1),
      value(current.player_cached_position, 1),
      value(older.player_cached_position, 2),
      value(previous.player_cached_position, 2),
      value(current.player_cached_position, 2), value(older.player_bounds_a, 0),
      value(previous.player_bounds_a, 0), value(current.player_bounds_a, 0),
      value(older.player_bounds_a, 1), value(previous.player_bounds_a, 1),
      value(current.player_bounds_a, 1), value(older.player_bounds_a, 2),
      value(previous.player_bounds_a, 2), value(current.player_bounds_a, 2),
      value(older.player_bounds_b, 0), value(previous.player_bounds_b, 0),
      value(current.player_bounds_b, 0), value(older.player_bounds_b, 1),
      value(previous.player_bounds_b, 1), value(current.player_bounds_b, 1),
      value(older.player_bounds_b, 2), value(previous.player_bounds_b, 2),
      value(current.player_bounds_b, 2),
      older.player_cached_position_valid ? 1u : 0u,
      previous.player_cached_position_valid ? 1u : 0u,
      current.player_cached_position_valid ? 1u : 0u,
      older.player_bounds_a_valid ? 1u : 0u,
      previous.player_bounds_a_valid ? 1u : 0u,
      current.player_bounds_a_valid ? 1u : 0u,
      older.player_bounds_b_valid ? 1u : 0u,
      previous.player_bounds_b_valid ? 1u : 0u,
      current.player_bounds_b_valid ? 1u : 0u);
}

uint32_t PlayerBoundsConsensusAxes(const SceneSnapshot& older,
                                   const SceneSnapshot& previous,
                                   const SceneSnapshot& current,
                                   Vec3* midpoint_offset) {
  if (midpoint_offset) {
    *midpoint_offset = {};
  }
  if (!older.player_position_valid || !previous.player_position_valid ||
      !current.player_position_valid || !older.player_bounds_a_valid ||
      !previous.player_bounds_a_valid || !current.player_bounds_a_valid ||
      !older.player_bounds_b_valid || !previous.player_bounds_b_valid ||
      !current.player_bounds_b_valid || !older.player_object ||
      older.player_object != previous.player_object ||
      older.player_object != current.player_object) {
    return 0u;
  }

  constexpr int64_t kMinimumMotion = 2;
  constexpr int64_t kMaximumMotion = 4096;
  auto agrees = [](int64_t reference, int64_t value) {
    return std::abs(value) >= kMinimumMotion &&
           std::abs(value) <= kMaximumMotion &&
           ((reference > 0 && value > 0) ||
            (reference < 0 && value < 0));
  };

  uint32_t mask = 0u;
  std::array<int64_t, 3> root_deltas{};
  for (size_t axis = 0; axis < 3u; ++axis) {
    const int64_t root_delta =
        static_cast<int64_t>(current.player_position[axis]) -
        static_cast<int64_t>(previous.player_position[axis]);
    root_deltas[axis] = root_delta;
    if (std::abs(root_delta) < kMinimumMotion ||
        std::abs(root_delta) > kMaximumMotion) {
      continue;
    }
    const int64_t previous_a =
        static_cast<int64_t>(previous.player_bounds_a[axis]) -
        static_cast<int64_t>(older.player_bounds_a[axis]);
    const int64_t current_a =
        static_cast<int64_t>(current.player_bounds_a[axis]) -
        static_cast<int64_t>(previous.player_bounds_a[axis]);
    const int64_t previous_b =
        static_cast<int64_t>(previous.player_bounds_b[axis]) -
        static_cast<int64_t>(older.player_bounds_b[axis]);
    const int64_t current_b =
        static_cast<int64_t>(current.player_bounds_b[axis]) -
        static_cast<int64_t>(previous.player_bounds_b[axis]);
    const double current_centroid_delta =
        0.5 * static_cast<double>(current_a + current_b);
    const double previous_centroid_delta =
        0.5 * static_cast<double>(previous_a + previous_b);
    const double magnitude_ratio =
        std::abs(current_centroid_delta) /
        static_cast<double>(std::abs(root_delta));
    const double temporal_ratio =
        std::abs(previous_centroid_delta) /
        std::max(std::abs(current_centroid_delta), 1.0);
    if (agrees(root_delta, previous_a) && agrees(root_delta, current_a) &&
        agrees(root_delta, previous_b) && agrees(root_delta, current_b) &&
        std::isfinite(magnitude_ratio) && magnitude_ratio >= 0.50 &&
        magnitude_ratio <= 1.50 && std::isfinite(temporal_ratio) &&
        temporal_ratio >= 0.50 && temporal_ratio <= 2.00) {
      mask |= 1u << axis;
    }
  }

  // Never create a hybrid diagonal pose where one meaningful horizontal axis
  // is halfway while the other is left at the exact endpoint. This occurred
  // during detach and angled wall motion (for example root X/Z 64/91 with only
  // X accepted) and visibly bent the path for one synthetic frame.
  const double accepted_horizontal = std::max(
      (mask & 0x1u) ? static_cast<double>(std::abs(root_deltas[0])) : 0.0,
      (mask & 0x4u) ? static_cast<double>(std::abs(root_deltas[2])) : 0.0);
  if (accepted_horizontal > 0.0) {
    constexpr double kMaximumRejectedAxisFraction = 0.35;
    if (((mask & 0x1u) == 0u &&
         std::abs(root_deltas[0]) >
             kMaximumRejectedAxisFraction * accepted_horizontal) ||
        ((mask & 0x4u) == 0u &&
         std::abs(root_deltas[2]) >
             kMaximumRejectedAxisFraction * accepted_horizontal)) {
      mask = 0u;
    }
  }

  if (midpoint_offset && mask) {
    midpoint_offset->x = (mask & 0x1u)
                             ? -0.5 * root_deltas[0] / kMatrixFixedScale
                             : 0.0;
    midpoint_offset->y = (mask & 0x2u)
                             ? -0.5 * root_deltas[1] / kMatrixFixedScale
                             : 0.0;
    midpoint_offset->z = (mask & 0x4u)
                             ? -0.5 * root_deltas[2] / kMatrixFixedScale
                             : 0.0;
  }
  return mask;
}

void LogBoundsApplication(uint64_t source_tick,
                          const SceneSnapshot& previous,
                          const SceneSnapshot& current,
                          uint32_t axis_mask) {
  if (!g_debug_log || !axis_mask ||
      source_tick - g_last_bounds_apply_log_tick < 6u) {
    return;
  }
  g_last_bounds_apply_log_tick = source_tick;
  AppendNativeLog(
      "bounds_apply tick=%llu axes=%u root_delta=%lld/%lld/%lld "
      "bounds_a_delta=%lld/%lld/%lld bounds_b_delta=%lld/%lld/%lld "
      "exact_cooldown=%u persistent=%u",
      static_cast<unsigned long long>(source_tick), axis_mask,
      static_cast<long long>(current.player_position[0]) -
          static_cast<long long>(previous.player_position[0]),
      static_cast<long long>(current.player_position[1]) -
          static_cast<long long>(previous.player_position[1]),
      static_cast<long long>(current.player_position[2]) -
          static_cast<long long>(previous.player_position[2]),
      static_cast<long long>(current.player_bounds_a[0]) -
          static_cast<long long>(previous.player_bounds_a[0]),
      static_cast<long long>(current.player_bounds_a[1]) -
          static_cast<long long>(previous.player_bounds_a[1]),
      static_cast<long long>(current.player_bounds_a[2]) -
          static_cast<long long>(previous.player_bounds_a[2]),
      static_cast<long long>(current.player_bounds_b[0]) -
          static_cast<long long>(previous.player_bounds_b[0]),
      static_cast<long long>(current.player_bounds_b[1]) -
          static_cast<long long>(previous.player_bounds_b[1]),
      static_cast<long long>(current.player_bounds_b[2]) -
          static_cast<long long>(previous.player_bounds_b[2]),
      g_player_contact_exact_cooldown,
      g_player_persistent_contact_cooldown);
}

Quaternion Slerp(Quaternion a, Quaternion b, double amount,
                 double* angle_degrees) {
  a = NormalizeQuaternion(a);
  b = NormalizeQuaternion(b);
  double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  if (dot < 0.0) {
    dot = -dot;
    b.w = -b.w;
    b.x = -b.x;
    b.y = -b.y;
    b.z = -b.z;
  }
  dot = std::clamp(dot, -1.0, 1.0);
  if (angle_degrees) {
    *angle_degrees = 2.0 * std::acos(dot) * 180.0 / 3.14159265358979323846;
  }
  if (dot > 0.9995) {
    return NormalizeQuaternion({a.w + amount * (b.w - a.w),
                                a.x + amount * (b.x - a.x),
                                a.y + amount * (b.y - a.y),
                                a.z + amount * (b.z - a.z)});
  }
  const double theta = std::acos(dot);
  const double sin_theta = std::sin(theta);
  const double from_weight = std::sin((1.0 - amount) * theta) / sin_theta;
  const double to_weight = std::sin(amount * theta) / sin_theta;
  return NormalizeQuaternion({a.w * from_weight + b.w * to_weight,
                              a.x * from_weight + b.x * to_weight,
                              a.y * from_weight + b.y * to_weight,
                              a.z * from_weight + b.z * to_weight});
}

int32_t ToFixed(double value) {
  const double fixed = std::round(value * kMatrixFixedScale);
  return static_cast<int32_t>(std::clamp(
      fixed, static_cast<double>(std::numeric_limits<int32_t>::min()),
      static_cast<double>(std::numeric_limits<int32_t>::max())));
}

bool InterpolateRigid(const Matrix3x4& previous, const Matrix3x4& current,
                      double phase, Matrix3x4* interpolated,
                      double* translation_distance,
                      double* rotation_degrees) {
  RigidTransform from;
  RigidTransform to;
  if (!DecodeRigid(previous, &from) || !DecodeRigid(current, &to)) {
    return false;
  }
  const Vec3 translation_delta = Subtract(to.translation, from.translation);
  const double distance = Length(translation_delta);
  if (!std::isfinite(distance) || distance > kMaximumNodeTranslation) {
    return false;
  }
  double angle = 0.0;
  phase = std::clamp(phase, 0.0, 1.0);
  const Quaternion rotation = Slerp(from.rotation, to.rotation, phase, &angle);
  if (!std::isfinite(angle) || angle > kMaximumNodeRotationDegrees) {
    return false;
  }
  const auto scale_ratio = [](double a, double b) {
    const double smaller = std::min(std::abs(a), std::abs(b));
    const double larger = std::max(std::abs(a), std::abs(b));
    return smaller > 1.0e-6 ? larger / smaller
                            : std::numeric_limits<double>::infinity();
  };
  if (scale_ratio(from.scale.x, to.scale.x) > kMaximumScaleRatio ||
      scale_ratio(from.scale.y, to.scale.y) > kMaximumScaleRatio ||
      scale_ratio(from.scale.z, to.scale.z) > kMaximumScaleRatio) {
    // Scale is also used for visibility and abrupt state changes in Asylum.
    // An in-between scale creates black wedges or a partially erased UI
    // object.
    return false;
  }
  const auto rows = RowsFromQuaternion(rotation);
  const Vec3 scale{from.scale.x + (to.scale.x - from.scale.x) * phase,
                   from.scale.y + (to.scale.y - from.scale.y) * phase,
                   from.scale.z + (to.scale.z - from.scale.z) * phase};
  for (size_t row = 0; row < 3; ++row) {
    const double row_scale = row == 0 ? scale.x : (row == 1 ? scale.y : scale.z);
    interpolated->values[row * 3 + 0] = ToFixed(rows[row].x * row_scale);
    interpolated->values[row * 3 + 1] = ToFixed(rows[row].y * row_scale);
    interpolated->values[row * 3 + 2] = ToFixed(rows[row].z * row_scale);
  }
  interpolated->values[9] = ToFixed(
      from.translation.x + (to.translation.x - from.translation.x) * phase);
  interpolated->values[10] = ToFixed(
      from.translation.y + (to.translation.y - from.translation.y) * phase);
  interpolated->values[11] = ToFixed(
      from.translation.z + (to.translation.z - from.translation.z) * phase);
  if (translation_distance) {
    *translation_distance = distance;
  }
  if (rotation_degrees) {
    *rotation_degrees = angle;
  }
  return true;
}

// Camera mode transitions intentionally cover a much larger translation and
// rotation than an ordinary animation frame, so they must not use the scene
// interpolation rejection thresholds above. The same rigid decomposition is
// retained, but the blend is explicitly bounded by the transition timer.
bool BlendCameraRigid(const Matrix3x4& from_matrix,
                      const Matrix3x4& to_matrix, double phase,
                      Matrix3x4* blended) {
  if (!blended) {
    return false;
  }
  RigidTransform from;
  RigidTransform to;
  if (!DecodeRigid(from_matrix, &from) || !DecodeRigid(to_matrix, &to)) {
    return false;
  }
  phase = std::clamp(phase, 0.0, 1.0);
  const Quaternion rotation = Slerp(from.rotation, to.rotation, phase, nullptr);
  const auto rows = RowsFromQuaternion(rotation);
  const Vec3 scale{from.scale.x + (to.scale.x - from.scale.x) * phase,
                   from.scale.y + (to.scale.y - from.scale.y) * phase,
                   from.scale.z + (to.scale.z - from.scale.z) * phase};
  for (size_t row = 0; row < 3; ++row) {
    const double row_scale = row == 0 ? scale.x : (row == 1 ? scale.y : scale.z);
    blended->values[row * 3 + 0] = ToFixed(rows[row].x * row_scale);
    blended->values[row * 3 + 1] = ToFixed(rows[row].y * row_scale);
    blended->values[row * 3 + 2] = ToFixed(rows[row].z * row_scale);
  }
  blended->values[9] = ToFixed(
      from.translation.x + (to.translation.x - from.translation.x) * phase);
  blended->values[10] = ToFixed(
      from.translation.y + (to.translation.y - from.translation.y) * phase);
  blended->values[11] = ToFixed(
      from.translation.z + (to.translation.z - from.translation.z) * phase);
  return true;
}

Matrix3x4 MultiplyAffine(const Matrix3x4& local,
                         const Matrix3x4& parent) {
  Matrix3x4 world{};
  for (size_t row = 0; row < 3; ++row) {
    for (size_t column = 0; column < 3; ++column) {
      int64_t sum = 0;
      for (size_t k = 0; k < 3; ++k) {
        sum += static_cast<int64_t>(local.values[row * 3 + k]) *
               static_cast<int64_t>(parent.values[k * 3 + column]);
      }
      world.values[row * 3 + column] = static_cast<int32_t>(
          sum >= 0 ? (sum + 8192) >> 14 : -(((-sum) + 8192) >> 14));
    }
  }
  for (size_t column = 0; column < 3; ++column) {
    int64_t sum = 0;
    for (size_t k = 0; k < 3; ++k) {
      sum += static_cast<int64_t>(local.values[9 + k]) *
             static_cast<int64_t>(parent.values[k * 3 + column]);
    }
    const int64_t rotated =
        sum >= 0 ? (sum + 8192) >> 14 : -(((-sum) + 8192) >> 14);
    world.values[9 + column] = static_cast<int32_t>(
        std::clamp<int64_t>(rotated + parent.values[9 + column],
                            std::numeric_limits<int32_t>::min(),
                            std::numeric_limits<int32_t>::max()));
  }
  return world;
}

void CaptureNode(uintptr_t node, SceneSnapshot* snapshot,
                 std::unordered_set<uintptr_t>* visited) {
  while (node && snapshot->nodes.size() < kMaximumSceneNodes) {
    if (!visited->insert(node).second) {
      return;
    }

    NodeTransform transform{};
    if (!SafeRead(reinterpret_cast<const void*>(node + kMatrixOffset),
                  &transform.world, sizeof(transform.world)) ||
        !SafeRead(reinterpret_cast<const void*>(node + kLocalMatrixOffset),
                  &transform.local, sizeof(transform.local)) ||
        !SafeReadValue(reinterpret_cast<const void*>(node + kNodeFlagsOffset),
                       &transform.flags) ||
        !SafeReadValue(reinterpret_cast<const void*>(node + kParentOffset),
                       &transform.parent)) {
      return;
    }
    SafeReadValue(
        reinterpret_cast<const void*>(node + kRenderResourceHandleOffset),
        &transform.render_resource_handle);
    std::array<int32_t, 4> bounds{};
    if (SafeRead(reinterpret_cast<const void*>(node + kWorldBoundsOffset),
                 bounds.data(), sizeof(bounds))) {
      transform.bounds_center = {bounds[0], bounds[1], bounds[2]};
      transform.bounds_radius = bounds[3];
      transform.bounds_valid = bounds[3] > 0;
    }
    snapshot->nodes.emplace(node, transform);

    uintptr_t child = 0;
    uintptr_t sibling = 0;
    if (!SafeReadValue(reinterpret_cast<const void*>(node + kChildOffset),
                       &child) ||
        !SafeReadValue(reinterpret_cast<const void*>(node + kSiblingOffset),
                       &sibling)) {
      return;
    }
    if (child) {
      CaptureNode(child, snapshot, visited);
    }
    node = sibling;
  }
}

SceneSnapshot CaptureScene(void* context) {
  SceneSnapshot snapshot;
  snapshot.nodes.reserve(1024);
  std::unordered_set<uintptr_t> visited;
  visited.reserve(1024);

  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  uintptr_t scene_owner = 0;
  uintptr_t root = 0;
  if (SafeReadValue(
          reinterpret_cast<const void*>(context_address +
                                        kContextSceneOwnerOffset),
          &scene_owner) &&
      scene_owner &&
      SafeReadValue(reinterpret_cast<const void*>(scene_owner +
                                                  kSceneRootOffset),
                    &root) &&
      root) {
    snapshot.root = root;
    CaptureNode(root, &snapshot, &visited);
  }

  // The camera transform is supplied separately to the scene traversal in
  // Dungeon.dll!0x10039100, so include it even if it is not in the root tree.
  uintptr_t camera_owner = 0;
  uintptr_t camera_node = 0;
  if (SafeReadValue(
          reinterpret_cast<const void*>(context_address +
                                        kContextCameraOwnerOffset),
          &camera_owner) &&
      camera_owner &&
      SafeReadValue(reinterpret_cast<const void*>(camera_owner +
                                                  kCameraNodeOffset),
                    &camera_node) &&
      camera_node) {
    snapshot.camera = camera_node;
    snapshot.camera_sector_valid = SafeReadValue(
        reinterpret_cast<const void*>(camera_node + kCameraNodeSectorOffset),
        &snapshot.camera_sector) && snapshot.camera_sector;
    auto existing = snapshot.nodes.find(camera_node);
    snapshot.camera_in_scene_tree = existing != snapshot.nodes.end();
    if (existing != snapshot.nodes.end()) {
      existing->second.pose_fields_valid =
          SafeRead(reinterpret_cast<const void*>(camera_node),
                   existing->second.local_transform_source.data(),
                   sizeof(existing->second.local_transform_source));
      if (existing->second.pose_fields_valid) {
        std::memcpy(existing->second.position.data(),
                    existing->second.local_transform_source.data(),
                    sizeof(existing->second.position));
        std::memcpy(
            existing->second.angles.data(),
            reinterpret_cast<const uint8_t*>(
                existing->second.local_transform_source.data()) + 0x18u,
            sizeof(existing->second.angles));
      }
    }
    if (existing == snapshot.nodes.end()) {
      visited.insert(camera_node);
      NodeTransform transform{};
      if (SafeRead(reinterpret_cast<const void*>(camera_node + kMatrixOffset),
                   &transform.world, sizeof(transform.world)) &&
          SafeRead(
              reinterpret_cast<const void*>(camera_node + kLocalMatrixOffset),
              &transform.local, sizeof(transform.local)) &&
          SafeReadValue(
              reinterpret_cast<const void*>(camera_node + kNodeFlagsOffset),
              &transform.flags) &&
           SafeReadValue(
               reinterpret_cast<const void*>(camera_node + kParentOffset),
               &transform.parent)) {
        transform.pose_fields_valid =
            SafeRead(reinterpret_cast<const void*>(camera_node),
                     transform.local_transform_source.data(),
                     sizeof(transform.local_transform_source));
        if (transform.pose_fields_valid) {
          std::memcpy(transform.position.data(),
                      transform.local_transform_source.data(),
                      sizeof(transform.position));
          std::memcpy(
              transform.angles.data(),
              reinterpret_cast<const uint8_t*>(
                  transform.local_transform_source.data()) + 0x18u,
              sizeof(transform.angles));
        }
        SafeReadValue(reinterpret_cast<const void*>(
                          camera_node + kRenderResourceHandleOffset),
                      &transform.render_resource_handle);
        std::array<int32_t, 4> bounds{};
        if (SafeRead(reinterpret_cast<const void*>(
                         camera_node + kWorldBoundsOffset),
                     bounds.data(), sizeof(bounds))) {
          transform.bounds_center = {bounds[0], bounds[1], bounds[2]};
          transform.bounds_radius = bounds[3];
          transform.bounds_valid = bounds[3] > 0;
        }
        snapshot.nodes.emplace(camera_node, transform);
      }
    }
  }

  if (g_third_person_orbit_state.engaged &&
      g_third_person_orbit_state.controller) {
    snapshot.camera_focus_valid = ReadCameraFocusPosition(
        g_third_person_orbit_state.controller, &snapshot.camera_focus);
  }

  snapshot.player = ResolvePlayerRenderNode(snapshot);
  if (snapshot.player) {
    snapshot.player_position_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player),
        snapshot.player_position.data(),
        sizeof(snapshot.player_position));
  }
  if (ReadPlayerObject(&snapshot.player_object)) {
    snapshot.player_cached_position_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player_object + 0x70u),
        snapshot.player_cached_position.data(),
        sizeof(snapshot.player_cached_position));
    snapshot.player_bounds_a_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player_object + 0xA4u),
        snapshot.player_bounds_a.data(), sizeof(snapshot.player_bounds_a));
    snapshot.player_bounds_b_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player_object + 0xC4u),
        snapshot.player_bounds_b.data(), sizeof(snapshot.player_bounds_b));
    snapshot.player_contact_count_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player_object + 0x44u),
        &snapshot.player_contact_count, sizeof(snapshot.player_contact_count));
  }
  return snapshot;
}

bool ApplyCustomCameraTransition(SceneSnapshot* current) {
  if (!current || !current->camera) {
    g_custom_camera_transition.initialized = false;
    return false;
  }
  if (current->nodes.find(current->camera) == current->nodes.end()) {
    g_custom_camera_transition.initialized = false;
    return false;
  }

  const CustomCameraViewMode mode = CurrentCustomCameraViewMode();
  if (!g_custom_camera_transition.initialized) {
    g_custom_camera_transition.initialized = true;
    g_custom_camera_transition.previous_mode = mode;
    return false;
  }
  if (mode != g_custom_camera_transition.previous_mode) {
    const CustomCameraViewMode previous =
        g_custom_camera_transition.previous_mode;
    g_custom_camera_transition.previous_mode = mode;
    // Never linearly blend a third-person origin through the character into
    // the head origin. That path intersects the model and was the direct
    // cause of the intermittent black frames in v0.0.56. Selection changes
    // are atomic; normal 50 Hz phase interpolation resumes from the new safe
    // endpoint immediately afterward.
    AppendNativeLog("camera_view render_transition=%s->%s policy=SAFE_CUT",
                    CustomCameraViewModeName(previous),
                    CustomCameraViewModeName(mode));
    return true;
  }
  return false;
}

bool PublishLiveCameraMatrix(uintptr_t camera_node) {
  if (!g_dungeon_base || !camera_node) {
    return false;
  }
  Matrix3x4 camera{};
  return SafeRead(reinterpret_cast<const void*>(camera_node + kMatrixOffset),
                  &camera, sizeof(camera)) &&
         SafeWrite(g_dungeon_base + kPublishedCameraMatrixRva,
                   &camera, sizeof(camera));
}

bool PublishLiveCameraSector(uintptr_t camera_node, uintptr_t seed_sector,
                             uintptr_t* published_sector) {
  if (published_sector) {
    *published_sector = 0;
  }
  if (!camera_node || !g_resolve_camera_sector) {
    return false;
  }
  Matrix3x4 camera{};
  if (!SafeRead(reinterpret_cast<const void*>(camera_node + kMatrixOffset),
                &camera, sizeof(camera))) {
    return false;
  }
  std::array<int32_t, 3> position = {
      camera.values[9], camera.values[10], camera.values[11]};
  if (!seed_sector && g_third_person_orbit_state.controller) {
    SafeReadValue(reinterpret_cast<const uint8_t*>(
                      g_third_person_orbit_state.controller) +
                      kCameraControllerEndpointSectorOffset,
                  &seed_sector);
  }
  if (!seed_sector) {
    return false;
  }

  uintptr_t sector = 0;
  __try {
    sector = g_resolve_camera_sector(position.data(), seed_sector);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    sector = 0;
  }
  if (!sector ||
      !SafeWrite(reinterpret_cast<void*>(
                     camera_node + kCameraNodeSectorOffset),
                 &sector, sizeof(sector))) {
    return false;
  }
  if (published_sector) {
    *published_sector = sector;
  }
  return true;
}

bool PublishSnapshotCameraMatrix(const SceneSnapshot& scene) {
  if (!g_dungeon_base || !scene.camera) {
    return false;
  }
  const auto camera = scene.nodes.find(scene.camera);
  return camera != scene.nodes.end() &&
         SafeWrite(g_dungeon_base + kPublishedCameraMatrixRva,
                   &camera->second.world, sizeof(camera->second.world));
}

bool MeasureRigidDelta(const Matrix3x4& previous, const Matrix3x4& current,
                       double* translation_distance,
                       double* rotation_degrees) {
  RigidTransform from;
  RigidTransform to;
  if (!DecodeRigid(previous, &from) || !DecodeRigid(current, &to)) {
    return false;
  }
  const double distance = Length(Subtract(to.translation, from.translation));
  Quaternion a = NormalizeQuaternion(from.rotation);
  Quaternion b = NormalizeQuaternion(to.rotation);
  double dot = std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
  dot = std::clamp(dot, 0.0, 1.0);
  const double angle =
      2.0 * std::acos(dot) * 180.0 / 3.14159265358979323846;
  if (!std::isfinite(distance) || !std::isfinite(angle)) {
    return false;
  }
  *translation_distance = distance;
  *rotation_degrees = angle;
  return true;
}

TransitionStats AnalyzeTransition(void* context,
                                  const SceneSnapshot& previous,
                                  const SceneSnapshot& current) {
  TransitionStats stats;
  stats.camera_in_scene_tree = current.camera_in_scene_tree;
  for (const auto& entry : current.nodes) {
    if (previous.nodes.find(entry.first) != previous.nodes.end()) {
      ++stats.matched;
    }
  }
  stats.entered = current.nodes.size() - stats.matched;
  stats.departed = previous.nodes.size() - stats.matched;
  const size_t population = std::max(previous.nodes.size(), current.nodes.size());
  stats.overlap = population
                      ? static_cast<double>(stats.matched) /
                            static_cast<double>(population)
                      : 0.0;

  if (current.camera && current.camera == previous.camera) {
    const auto previous_camera = previous.nodes.find(previous.camera);
    const auto current_camera = current.nodes.find(current.camera);
    if (previous_camera != previous.nodes.end() &&
        current_camera != current.nodes.end()) {
      stats.camera_valid = MeasureRigidDelta(
          previous_camera->second.world, current_camera->second.world,
          &stats.camera_translation, &stats.camera_rotation_degrees);
    }
  }

  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  SafeReadValue(reinterpret_cast<const void*>(
                    context_address + kContextPrimaryCallbackOffset),
                &stats.primary_callback);
  SafeReadValue(reinterpret_cast<const void*>(
                    context_address + kContextSecondaryCallbackOffset),
                &stats.secondary_callback);

  // Suppress only transitions whose scene membership is demonstrably unsafe.
  // Camera rotation by itself is not a cut signal in Deathtrap: the camera
  // quaternion may cross an equivalent representation during an ordinary
  // turn, and small frustum membership changes are normal. Rejecting those
  // valid midpoints caused long runs of held frames and visible tripling.
  // Completed D3D11 midpoints still pass through the independent pixel-level
  // black-region guard, so actual corrupt raster output remains hidden.
  if (stats.overlap < 0.90) {
    stats.suppress_midpoint = true;
    stats.reason = "visibility_cut";
  } else if (stats.overlap < 0.960 &&
             stats.entered + stats.departed >= 24u) {
    // The captured camera node is static in this engine path. Moderate
    // culling-set churn is therefore the more reliable signal for a fast view
    // transition. Normal actor animation changes at most a few nodes.
    stats.suppress_midpoint = true;
    stats.reason = "visibility_churn";
  }
  return stats;
}

uintptr_t DungeonRva(uintptr_t address) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  return address >= base && address < base + kExpectedImageSize
             ? address - base
             : address;
}

template <typename Value, size_t Count>
uint32_t AppendCameraFieldChanges(
    const char* name, const std::array<Value, Count>& previous,
    const std::array<Value, Count>& current, std::string* line) {
  uint32_t changed = 0;
  uint32_t printed = 0;
  char field[96] = {};
  for (size_t index = 0; index < Count; ++index) {
    if (previous[index] == current[index]) {
      continue;
    }
    ++changed;
    if (printed >= 16u) {
      continue;
    }
    const int length = std::snprintf(
        field, sizeof(field), "%s+%02zX:%08X>%08X,", name,
        index * sizeof(Value), static_cast<uint32_t>(previous[index]),
        static_cast<uint32_t>(current[index]));
    if (length > 0) {
      line->append(field, static_cast<size_t>(length));
      ++printed;
    }
  }
  if (changed > printed) {
    const int length = std::snprintf(field, sizeof(field), "%s_more=%u,", name,
                                     changed - printed);
    if (length > 0) {
      line->append(field, static_cast<size_t>(length));
    }
  }
  return changed;
}

void FlushCameraProbeLog() {
  if (g_camera_probe_log_buffer.empty()) {
    return;
  }
  AppendNativeLogBlock(g_camera_probe_log_buffer);
  g_camera_probe_log_buffer.clear();
}

void ProbeCameraState(void* context, const SceneSnapshot& scene,
                      uint64_t source_tick) {
  if (!g_camera_probe_enabled || !g_debug_log || !g_dungeon_base ||
      !context) {
    return;
  }

  CameraProbeSnapshot current;
  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  SafeReadValue(reinterpret_cast<const void*>(
                    context_address + kContextCameraOwnerOffset),
                &current.context_owner);
  SafeReadValue(g_dungeon_base + kRetailCameraManagerPointerRva,
                &current.manager);
  current.controller_valid = SafeRead(
      g_dungeon_base + kCameraControllerRva,
      current.controller_fields.data(), sizeof(current.controller_fields));
  SafeReadValue(g_dungeon_base + kCameraControllerFlagsRva,
                &current.controller_flags);
  SafeReadValue(g_dungeon_base + kCameraControllerCallback0Rva,
                &current.controller_callback0);
  SafeReadValue(g_dungeon_base + kCameraControllerCallback1Rva,
                &current.controller_callback1);
  SafeReadValue(g_dungeon_base + kCameraControllerModeRva,
                &current.controller_mode);
  if (current.context_owner) {
    SafeReadValue(reinterpret_cast<const void*>(
                      current.context_owner + kCameraNodeOffset),
                  &current.node);
    SafeReadValue(reinterpret_cast<const void*>(
                      current.context_owner + kCameraOwnerCallbackOffset),
                  &current.callback);
    current.owner_valid = SafeRead(
        reinterpret_cast<const void*>(current.context_owner),
        current.owner_fields.data(), sizeof(current.owner_fields));
  }
  if (current.manager) {
    SafeReadValue(reinterpret_cast<const void*>(
                      current.manager + kCameraNodeOffset),
                  &current.manager_node);
    current.manager_valid = SafeRead(
        reinterpret_cast<const void*>(current.manager),
        current.manager_fields.data(), sizeof(current.manager_fields));
  }
  if (current.node) {
    current.node_valid = SafeRead(
        reinterpret_cast<const void*>(current.node), current.node_fields.data(),
        sizeof(current.node_fields));
  }
  current.published_valid = SafeRead(g_dungeon_base + kPublishedCameraMatrixRva,
                                     &current.published_matrix,
                                     sizeof(current.published_matrix));

  const bool identity_changed =
      g_camera_probe_previous.initialized &&
      (g_camera_probe_previous.context_owner != current.context_owner ||
       g_camera_probe_previous.manager != current.manager ||
       g_camera_probe_previous.node != current.node ||
       g_camera_probe_previous.callback != current.callback);
  if (!g_camera_probe_previous.initialized || identity_changed) {
    char baseline[768] = {};
    const int length = std::snprintf(
        baseline, sizeof(baseline),
        "camera_probe baseline tick=%llu context=%08llX owner=%08llX "
        "manager=%08llX manager_node=%08llX node=%08llX callback_rva=%08llX "
        "controller=%08llX controller_cb=%08llX/%08llX flags=%08X mode=%02X "
        "owner_valid=%u manager_valid=%u node_valid=%u controller_valid=%u "
        "published_valid=%u\r\n",
        static_cast<unsigned long long>(source_tick),
        static_cast<unsigned long long>(context_address),
        static_cast<unsigned long long>(current.context_owner),
        static_cast<unsigned long long>(current.manager),
        static_cast<unsigned long long>(current.manager_node),
        static_cast<unsigned long long>(current.node),
        static_cast<unsigned long long>(DungeonRva(current.callback)),
        static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(g_dungeon_base) +
            kCameraControllerRva),
        static_cast<unsigned long long>(
            DungeonRva(current.controller_callback0)),
        static_cast<unsigned long long>(
            DungeonRva(current.controller_callback1)),
        current.controller_flags, static_cast<unsigned>(current.controller_mode),
        current.owner_valid ? 1u : 0u, current.manager_valid ? 1u : 0u,
        current.node_valid ? 1u : 0u, current.controller_valid ? 1u : 0u,
        current.published_valid ? 1u : 0u);
    if (length > 0) {
      g_camera_probe_log_buffer.append(baseline,
                                       static_cast<size_t>(length));
    }
    current.initialized = true;
    g_camera_probe_previous = current;
    FlushCameraProbeLog();
    return;
  }

  std::string fields;
  fields.reserve(2048);
  uint32_t owner_changes = 0;
  uint32_t manager_changes = 0;
  uint32_t node_changes = 0;
  uint32_t controller_changes = 0;
  uint32_t published_changes = 0;
  if (current.owner_valid && g_camera_probe_previous.owner_valid) {
    owner_changes = AppendCameraFieldChanges(
        "o", g_camera_probe_previous.owner_fields, current.owner_fields,
        &fields);
  }
  if (current.manager_valid && g_camera_probe_previous.manager_valid) {
    manager_changes = AppendCameraFieldChanges(
        "m", g_camera_probe_previous.manager_fields, current.manager_fields,
        &fields);
  }
  if (current.node_valid && g_camera_probe_previous.node_valid) {
    node_changes = AppendCameraFieldChanges(
        "n", g_camera_probe_previous.node_fields, current.node_fields,
        &fields);
  }
  if (current.controller_valid &&
      g_camera_probe_previous.controller_valid) {
    controller_changes = AppendCameraFieldChanges(
        "c", g_camera_probe_previous.controller_fields,
        current.controller_fields, &fields);
  }
  if (current.published_valid && g_camera_probe_previous.published_valid) {
    published_changes = AppendCameraFieldChanges(
        "p", g_camera_probe_previous.published_matrix.values,
        current.published_matrix.values, &fields);
  }

  SHORT right_x = 0;
  SHORT right_y = 0;
  if (g_xinput_get_state) {
    XINPUT_STATE state = {};
    if (g_xinput_get_state(g_xinput_controller_index, &state) ==
        ERROR_SUCCESS) {
      right_x = state.Gamepad.sThumbRX;
      right_y = state.Gamepad.sThumbRY;
    }
  }

  std::array<int32_t, 3> camera_translation{};
  std::array<int32_t, 3> player_translation{};
  const auto camera = scene.nodes.find(scene.camera);
  if (camera != scene.nodes.end()) {
    std::copy_n(camera->second.world.values.begin() + 9, 3,
                camera_translation.begin());
  }
  const auto player = scene.nodes.find(scene.player);
  if (player != scene.nodes.end()) {
    std::copy_n(player->second.world.values.begin() + 9, 3,
                player_translation.begin());
  }

  const auto controller_field = [&current](size_t offset) -> uint32_t {
    const size_t index = offset / sizeof(uint32_t);
    return index < current.controller_fields.size()
               ? current.controller_fields[index]
               : 0u;
  };

  char header[1536] = {};
  const int length = std::snprintf(
      header, sizeof(header),
      "camera_probe tick=%llu first_person=%u rs=%d/%d "
      "changes=%u/%u/%u/%u/%u ctrl_flags=%08X ctrl_mode=%02X "
      "ctrl_cb=%08llX/%08llX "
      "ctrl_key=%08X/%08X/%08X/%08X/%08X/%08X/%08X/%08X/%08X "
      "camera_t=%d/%d/%d player_t=%d/%d/%d fields=[",
      static_cast<unsigned long long>(source_tick),
      (g_xinput_first_person_toggled.load(std::memory_order_acquire) ||
       RetailFirstPersonActive()) ? 1u : 0u,
      static_cast<int>(right_x),
      static_cast<int>(right_y), owner_changes, manager_changes, node_changes,
      controller_changes, published_changes, current.controller_flags,
      static_cast<unsigned>(current.controller_mode),
      static_cast<unsigned long long>(DungeonRva(current.controller_callback0)),
      static_cast<unsigned long long>(DungeonRva(current.controller_callback1)),
      controller_field(0x19Cu), controller_field(0x1ACu),
      controller_field(0x1B0u), controller_field(0x1DCu),
      controller_field(0x1E0u), controller_field(0x1E4u),
      controller_field(0x1E8u), controller_field(0x1ECu),
      controller_field(0x27Cu), camera_translation[0], camera_translation[1],
      camera_translation[2], player_translation[0], player_translation[1],
      player_translation[2]);
  if (length > 0) {
    g_camera_probe_log_buffer.append(header, static_cast<size_t>(length));
  }
  g_camera_probe_log_buffer.append(fields);
  g_camera_probe_log_buffer.append("]\r\n");
  current.initialized = true;
  g_camera_probe_previous = current;
  if ((source_tick & 15u) == 0u ||
      g_camera_probe_log_buffer.size() >= 24u * 1024u) {
    FlushCameraProbeLog();
  }
}

InterpolationStats ApplyInterpolatedScene(const SceneSnapshot* older,
                                           const SceneSnapshot& previous,
                                           SceneSnapshot& current,
                                           double phase,
                                           bool update_temporal_state,
                                           uint64_t source_tick) {
  struct MidpointNode {
    Matrix3x4 local;
    Matrix3x4 world;
    bool local_interpolated = false;
    bool world_interpolated = false;
    bool ready = false;
  };

  InterpolationStats stats;
  const uintptr_t player_render_node = current.player;
  const bool authoritative_contact_projection =
      current.player_contact_projection_valid && player_render_node != 0;
  if (authoritative_contact_projection) {
    stats.player_contact_projection_events = 1u;
  }
  stats.player_root_found = player_render_node ? 1u : 0u;
  bool player_contact_correction = false;
  bool player_raw_contact_correction = false;
  RigidTransform previous_player_transform;
  RigidTransform current_player_transform;
  RigidTransform older_player_transform;
  bool player_motion_valid = false;
  bool player_contact_bridge_valid = false;
  Vec3 player_contact_bridge_offset{};
  phase = std::clamp(phase, 0.0, 1.0);
  if (older && player_render_node &&
      older->player == player_render_node &&
      previous.player == player_render_node) {
    if (update_temporal_state) {
      RecordPlayerAxisTelemetry(*older, previous, current,
                                player_render_node);
    }
    const auto older_player = older->nodes.find(player_render_node);
    const auto previous_player = previous.nodes.find(player_render_node);
    const auto current_player = current.nodes.find(player_render_node);
    if (older_player != older->nodes.end() &&
        previous_player != previous.nodes.end() &&
        current_player != current.nodes.end() &&
        older_player->second.parent == current_player->second.parent &&
        previous_player->second.parent == current_player->second.parent) {
      player_motion_valid =
          DecodeRigid(older_player->second.world, &older_player_transform) &&
          DecodeRigid(previous_player->second.world,
                      &previous_player_transform) &&
          DecodeRigid(current_player->second.world, &current_player_transform);
      player_contact_correction = HasPlayerContactCorrection(
          older_player->second.world, previous_player->second.world,
          current_player->second.world);
    }
    if (older->player_position_valid && previous.player_position_valid &&
        current.player_position_valid) {
      player_raw_contact_correction = HasPlayerRawContactCorrection(
          older->player_position, previous.player_position,
          current.player_position);
    }
  }
  if (update_temporal_state && player_contact_correction) {
    ++g_player_matrix_contact_candidates;
  }
  if (update_temporal_state && player_raw_contact_correction) {
    ++g_player_raw_contact_candidates;
  }
  // The world-matrix detector deliberately has a wide envelope and catches
  // animation-root sway as well as collision response. V41 proved that using
  // it alone was far too eager (63 visual reconciliations versus 8 tight raw
  // A-B-A returns in the same run). Require both independently captured root
  // representations to agree before changing contact presentation cadence.
  const bool player_confirmed_contact_correction =
      player_contact_correction && player_raw_contact_correction;
  if (player_confirmed_contact_correction) {
    if (player_motion_valid) {
      // A -> B -> C ~= A means B was the rejected collision pose. Preserve
      // B-to-C bone animation, but translate the complete synthetic actor so
      // its root lies between the two accepted endpoints A and C. The
      // correction decreases toward C as the presentation phase advances.
      player_contact_bridge_offset = Scale(
          Subtract(older_player_transform.translation,
                   previous_player_transform.translation),
          1.0 - phase);
      player_contact_bridge_valid = true;
      stats.player_contact_bridge_events = 1u;
      if (update_temporal_state) {
        Vec3 correction = Subtract(current_player_transform.translation,
                                   previous_player_transform.translation);
        const double correction_length = Length(correction);
        if (Normalize(&correction)) {
          // A whole-vector A -> B -> A correction ends with B -> C pointing
          // away from the rejected wall penetration. Keep that direction so
          // a later genuine detach can end the exact-current fallback.
          const double normal_alignment = g_player_contact_normal_valid
              ? Dot(correction, g_player_contact_outward_normal)
              : 1.0;
          const bool isolated_opposite_flip =
              g_player_contact_normal_valid && g_player_contact_hit_window &&
              std::isfinite(normal_alignment) && normal_alignment < -0.25;
          if (isolated_opposite_flip) {
            // The wall trace occasionally contains one reversed A-B-A cycle
            // between dozens of consistently oriented cycles (v46 tick 270,
            // then normal orientation restored at tick 276). Letting that one
            // sample replace the established plane moves the actor to the
            // opposite side for several presentations. Keep the prior
            // measured manifold; a real detach is handled independently by
            // sustained outward motion and a later contact starts from
            // cleared state.
            ++stats.player_contact_normal_flip_rejections;
            ++g_player_contact_normal_flip_rejections;
          } else {
            g_player_contact_outward_normal = correction;
            g_player_contact_anchor = current_player_transform.translation;
            g_player_contact_normal_valid = true;
            g_player_contact_anchor_valid = true;
            g_player_contact_correction_length = correction_length;
          }
        }
      }
    }
    if (update_temporal_state) {
      g_player_contact_release_streak = 0u;
      g_player_contact_quiet_ticks = 0u;
      g_player_contact_release_alignment = 0.0;
      g_player_contact_outward_distance = 0.0;
      if (!g_player_contact_hit_window) {
        g_player_contact_hits_in_window = 0u;
      }
      ++g_player_contact_hits_in_window;
      g_player_contact_hit_window = 12u;
      if (g_player_contact_hits_in_window >= 2u) {
        g_player_persistent_contact_cooldown = 18u;
      }
      // A single confirmed A-B-A event is handled by the accepted-endpoint
      // bridge above. Exact-current cadence is reserved for repeated contact;
      // holding an isolated correction for eight endpoints caused visible
      // sticking and multiple silhouettes at pipes and angled walls.
      g_player_contact_exact_cooldown =
          g_player_contact_hits_in_window >= 2u ? 1u : 0u;
      ++stats.contact_reversal;
      ++stats.temporal_pose_guard;
      ++stats.player_contact_settle;
      ++stats.player_raw_contact;
    }
  } else if (update_temporal_state) {
    bool directional_release = false;
    if (g_player_contact_quiet_ticks <
        std::numeric_limits<uint32_t>::max()) {
      ++g_player_contact_quiet_ticks;
    }
    if (g_player_contact_normal_valid && g_player_contact_anchor_valid &&
        player_motion_valid &&
        g_player_persistent_contact_cooldown) {
      const Vec3 motion = Subtract(current_player_transform.translation,
                                   previous_player_transform.translation);
      const double motion_length = Length(motion);
      const Vec3 from_contact = Subtract(
          current_player_transform.translation, g_player_contact_anchor);
      g_player_contact_outward_distance =
          Dot(from_contact, g_player_contact_outward_normal);
      if (std::isfinite(motion_length) &&
          motion_length >= 2.0 / kMatrixFixedScale) {
        g_player_contact_release_alignment =
            Dot(motion, g_player_contact_outward_normal) / motion_length;
        if (std::isfinite(g_player_contact_release_alignment) &&
            g_player_contact_release_alignment > 0.10) {
          ++g_player_contact_release_streak;
        } else {
          g_player_contact_release_streak = 0u;
        }
      } else {
        g_player_contact_release_alignment = 0.0;
        g_player_contact_release_streak = 0u;
      }
      const double required_outward_distance = std::max(
          64.0 / kMatrixFixedScale,
          4.0 * g_player_contact_correction_length);
      if (g_player_contact_quiet_ticks >= 12u &&
          g_player_contact_release_streak >= 6u &&
          std::isfinite(g_player_contact_outward_distance) &&
          g_player_contact_outward_distance >= required_outward_distance) {
        // Collision response itself also contains one or two outward steps.
        // Only a sustained, spatially meaningful departure beyond the last
        // correction endpoint is a real detach.
        directional_release = true;
        const bool was_persistent =
            g_player_persistent_contact_cooldown != 0u;
        g_player_contact_exact_cooldown = 0u;
        g_player_persistent_contact_cooldown = 0u;
        g_player_contact_hit_window = 0u;
        g_player_contact_hits_in_window = 0u;
        g_player_contact_normal_valid = false;
        g_player_contact_anchor_valid = false;
        ++stats.player_directional_release;
        ++g_player_contact_directional_releases;
        if (g_debug_log) {
          AppendNativeLog(
              "contact_release quiet=%u streak=%u alignment=%.3f "
              "outward=%.6f required=%.6f correction=%.6f persistent=%u",
              g_player_contact_quiet_ticks,
              g_player_contact_release_streak,
              g_player_contact_release_alignment,
              g_player_contact_outward_distance,
              required_outward_distance,
              g_player_contact_correction_length,
              was_persistent ? 1u : 0u);
        }
      }
    }
    if (!directional_release && g_player_contact_exact_cooldown) {
      --g_player_contact_exact_cooldown;
    }
  }
  if (update_temporal_state) {
    if (g_player_contact_hit_window) {
      --g_player_contact_hit_window;
      if (!g_player_contact_hit_window) {
        g_player_contact_hits_in_window = 0u;
      }
    }
    if (g_player_persistent_contact_cooldown &&
        !player_confirmed_contact_correction) {
      --g_player_persistent_contact_cooldown;
    }
    if (!g_player_contact_exact_cooldown &&
        !g_player_persistent_contact_cooldown &&
        !g_player_contact_hit_window) {
      g_player_contact_normal_valid = false;
      g_player_contact_anchor_valid = false;
      g_player_contact_release_streak = 0u;
      g_player_contact_quiet_ticks = 0u;
      g_player_contact_release_alignment = 0.0;
      g_player_contact_outward_distance = 0.0;
      g_player_contact_correction_length = 0.0;
    }
  }
  if (update_temporal_state && authoritative_contact_projection) {
    // The measured collision projection supersedes the old A-B-A fallback
    // for this transition.  Keeping the fallback cooldown would still force
    // the complete actor to exact-current and throw away the smooth tangent
    // component that we can now reconstruct exactly.
    g_player_contact_exact_cooldown = 0u;
    g_player_contact_hit_window = 0u;
    g_player_contact_hits_in_window = 0u;
    g_player_persistent_contact_cooldown = 0u;
    g_player_contact_normal_valid = false;
    g_player_contact_anchor_valid = false;
    g_player_contact_release_streak = 0u;
    g_player_contact_quiet_ticks = 0u;
    g_player_contact_release_alignment = 0.0;
    g_player_contact_outward_distance = 0.0;
    g_player_contact_correction_length = 0.0;
  }

  // Once two independently observed A-B-A collision returns have established
  // a persistent contact, reject only the component of a later render
  // endpoint that crosses the accepted contact plane. The game still receives
  // and resolves its original B endpoint; this correction exists solely for
  // midpoint and exact presentation. Tangential motion and motion away from
  // the surface are intentionally untouched.
  if (update_temporal_state && !authoritative_contact_projection &&
      player_render_node &&
      player_motion_valid && g_player_persistent_contact_cooldown &&
      g_player_contact_normal_valid && g_player_contact_anchor_valid) {
    const Vec3 from_contact = Subtract(
        current_player_transform.translation, g_player_contact_anchor);
    const double signed_distance =
        Dot(from_contact, g_player_contact_outward_normal);
    const double minimum_depth = 0.5 / kMatrixFixedScale;
    const double maximum_depth = 256.0 / kMatrixFixedScale;
    if (std::isfinite(signed_distance) && signed_distance < -minimum_depth &&
        -signed_distance <= maximum_depth) {
      const double depth = -signed_distance;
      const Vec3 projection =
          Scale(g_player_contact_outward_normal, depth);
      current.player_contact_manifold_projection = {
          ToFixed(projection.x), ToFixed(projection.y),
          ToFixed(projection.z)};
      current.player_contact_manifold_projection_valid = true;
      stats.player_contact_manifold_events = 1u;
      stats.player_contact_manifold_depth = depth;
      ++g_player_contact_manifold_events;
      g_player_contact_manifold_max_depth =
          std::max(g_player_contact_manifold_max_depth, depth);
    }
  }

  auto is_player_subtree_node = [&](uintptr_t address) {
    for (size_t depth = 0; address && depth < 128u; ++depth) {
      if (address == player_render_node) {
        return true;
      }
      const auto node = current.nodes.find(address);
      if (node == current.nodes.end()) {
        break;
      }
      address = node->second.parent;
    }
    return false;
  };

  std::unordered_map<uintptr_t, MidpointNode> midpoint_nodes;
  midpoint_nodes.reserve(current.nodes.size());
  for (const auto& entry : current.nodes) {
    MidpointNode midpoint;
    midpoint.local = entry.second.local;
    midpoint.world = entry.second.world;
    const auto previous_it = previous.nodes.find(entry.first);
    if (previous_it == previous.nodes.end()) {
      ++stats.exact;
    } else if (previous_it->second.parent != entry.second.parent) {
      ++stats.parent_mismatch;
      ++stats.exact;
    } else {
      double translation = 0.0;
      double rotation = 0.0;
      if (InterpolateRigid(previous_it->second.local, entry.second.local,
                           phase, &midpoint.local, &translation, &rotation)) {
        midpoint.local_interpolated = true;
        stats.max_translation = std::max(stats.max_translation, translation);
        stats.max_rotation_degrees =
            std::max(stats.max_rotation_degrees, rotation);
      } else {
        ++stats.invalid;
        ++stats.exact;
      }
    }
    midpoint_nodes.emplace(entry.first, midpoint);
  }

  std::unordered_set<uintptr_t> visiting;
  visiting.reserve(current.nodes.size());
  std::function<bool(uintptr_t)> build_world = [&](uintptr_t address) {
    auto midpoint_it = midpoint_nodes.find(address);
    const auto current_it = current.nodes.find(address);
    if (midpoint_it == midpoint_nodes.end() || current_it == current.nodes.end()) {
      return false;
    }
    MidpointNode& midpoint = midpoint_it->second;
    if (midpoint.ready) {
      return midpoint.world_interpolated;
    }
    if (!visiting.insert(address).second) {
      midpoint.world = current_it->second.world;
      midpoint.ready = true;
      midpoint.world_interpolated = false;
      ++stats.exact;
      return false;
    }

    const uintptr_t parent = current_it->second.parent;
    if (!midpoint.local_interpolated) {
      midpoint.world = current_it->second.world;
    } else if (!parent) {
      midpoint.world = midpoint.local;
      midpoint.world_interpolated = true;
      ++stats.hierarchical;
    } else {
      auto parent_midpoint = midpoint_nodes.find(parent);
      if (parent_midpoint != midpoint_nodes.end() && build_world(parent)) {
        midpoint.world = MultiplyAffine(midpoint.local,
                                        parent_midpoint->second.world);
        midpoint.world_interpolated = true;
        ++stats.hierarchical;
      } else if (parent_midpoint == midpoint_nodes.end()) {
        const auto previous_it = previous.nodes.find(address);
        double translation = 0.0;
        double rotation = 0.0;
        if (previous_it != previous.nodes.end() &&
            InterpolateRigid(previous_it->second.world,
                             current_it->second.world, phase,
                             &midpoint.world, &translation, &rotation)) {
          midpoint.world_interpolated = true;
          ++stats.world_fallback;
          stats.max_translation =
              std::max(stats.max_translation, translation);
          stats.max_rotation_degrees =
              std::max(stats.max_rotation_degrees, rotation);
        } else {
          midpoint.world = current_it->second.world;
        }
      } else {
        midpoint.world = current_it->second.world;
      }
    }
    visiting.erase(address);
    midpoint.ready = true;
    return midpoint.world_interpolated;
  };

  for (const auto& entry : current.nodes) {
    build_world(entry.first);
  }

  bool pivot_relative_camera_handled = false;
  if (current.camera && previous.camera == current.camera &&
      current.camera_focus_valid && previous.camera_focus_valid &&
      CurrentCustomCameraViewMode() ==
          CustomCameraViewMode::kModernThirdPerson &&
      !RetailFirstPersonActive() &&
      g_third_person_orbit_state.engaged &&
      g_third_person_orbit_state.controller &&
      !g_scripted_camera_override_active.load(std::memory_order_acquire)) {
    const auto previous_camera = previous.nodes.find(current.camera);
    const auto current_camera = current.nodes.find(current.camera);
    auto midpoint_camera = midpoint_nodes.find(current.camera);
    if (previous_camera != previous.nodes.end() &&
        current_camera != current.nodes.end() &&
        midpoint_camera != midpoint_nodes.end() &&
        midpoint_camera->second.world_interpolated) {
      const CameraPivotRelativeInterpolation pivot =
          InterpolateCameraPivotRelative(
              {static_cast<double>(previous.camera_focus[0]),
               static_cast<double>(previous.camera_focus[1]),
               static_cast<double>(previous.camera_focus[2])},
              {static_cast<double>(previous_camera->second.world.values[9]),
               static_cast<double>(previous_camera->second.world.values[10]),
               static_cast<double>(previous_camera->second.world.values[11])},
              {static_cast<double>(current.camera_focus[0]),
               static_cast<double>(current.camera_focus[1]),
               static_cast<double>(current.camera_focus[2])},
              {static_cast<double>(current_camera->second.world.values[9]),
               static_cast<double>(current_camera->second.world.values[10]),
               static_cast<double>(current_camera->second.world.values[11])},
              phase);
      if (pivot.valid) {
        const std::array<int32_t, 3> phase_focus = {
            static_cast<int32_t>(std::lround(pivot.focus[0])),
            static_cast<int32_t>(std::lround(pivot.focus[1])),
            static_cast<int32_t>(std::lround(pivot.focus[2]))};
        const std::array<int32_t, 3> phase_position = {
            static_cast<int32_t>(std::lround(pivot.position[0])),
            static_cast<int32_t>(std::lround(pivot.position[1])),
            static_cast<int32_t>(std::lround(pivot.position[2]))};
        bool native_blocked = true;
        const bool native_valid = ModernCameraFootprintBlocked(
            g_third_person_orbit_state.controller, phase_focus,
            phase_position, &native_blocked);
        std::array<int32_t, 3> resolved_position = phase_position;
        bool native_clipped = false;
        if (native_valid && native_blocked) {
          native_clipped = ClipThirdPersonOrbitAgainstNativeWorld(
              g_third_person_orbit_state.controller, phase_focus,
              phase_position, &resolved_position);
        }
        const bool resolved_clear = native_valid &&
            (!native_blocked || native_clipped) &&
            CameraTargetMeetsMinimumDistance(
                phase_focus, resolved_position,
                kThirdPersonMinimumCameraDistance);

        if (resolved_clear) {
          for (size_t axis = 0; axis < 3u; ++axis) {
            const int64_t delta =
                static_cast<int64_t>(resolved_position[axis]) -
                midpoint_camera->second.world.values[9u + axis];
            midpoint_camera->second.world.values[9u + axis] =
                resolved_position[axis];
            midpoint_camera->second.local.values[9u + axis] =
                static_cast<int32_t>(std::clamp<int64_t>(
                    static_cast<int64_t>(
                        midpoint_camera->second.local.values[9u + axis]) +
                        delta,
                    std::numeric_limits<int32_t>::min(),
                    std::numeric_limits<int32_t>::max()));
          }
        } else {
          midpoint_camera->second.world = previous_camera->second.world;
          midpoint_camera->second.local = previous_camera->second.local;
        }
        pivot_relative_camera_handled = true;
        if (!resolved_clear) {
          stats.camera_temporal_chord_guard = 1u;
          ++g_camera_temporal_chord_guards;
          if (g_debug_log) {
            AppendNativeLog(
                "camera_pivot_native_guard tick=%llu phase=%.3f "
                "native=%u/%u clipped=%u selected=previous_exact_hold "
                "total=%llu",
                static_cast<unsigned long long>(source_tick), phase,
                native_valid ? 1u : 0u, native_blocked ? 1u : 0u,
                native_clipped ? 1u : 0u,
                static_cast<unsigned long long>(
                    g_camera_temporal_chord_guards));
          }
        }
      }
    }
  }

  // The exact camera positions at the two source ticks were each accepted by
  // the spring-arm collision resolver. They are not sufficient proof that the
  // straight interpolation chord between them is safe: while orbiting a prop
  // corner that chord may cross the prop even though both radial pivot rays are
  // clear. Validate the complete temporal camera segment against stable render
  // geometry. On a blocked chord keep the first synthetic sample at the
  // previous safe endpoint and move the second to the current safe endpoint;
  // rotation remains smoothly interpolated. This sacrifices translation
  // smoothing only for the two unsafe samples instead of rendering the camera
  // behind a one-sided mesh.
  if (!pivot_relative_camera_handled && current.camera &&
      previous.camera == current.camera) {
    const auto previous_camera = previous.nodes.find(current.camera);
    const auto current_camera = current.nodes.find(current.camera);
    auto midpoint_camera = midpoint_nodes.find(current.camera);
    if (previous_camera != previous.nodes.end() &&
        current_camera != current.nodes.end() &&
        midpoint_camera != midpoint_nodes.end() &&
        midpoint_camera->second.world_interpolated) {
      const Vec3 previous_position{
          static_cast<double>(previous_camera->second.world.values[9]),
          static_cast<double>(previous_camera->second.world.values[10]),
          static_cast<double>(previous_camera->second.world.values[11])};
      const Vec3 current_position{
          static_cast<double>(current_camera->second.world.values[9]),
          static_cast<double>(current_camera->second.world.values[10]),
          static_cast<double>(current_camera->second.world.values[11])};
      CameraMeshHitDiagnostic diagnostic;
      double hit_distance = 0.0;
      if (CameraTemporalChordIntersectsSceneObjects(
              current, previous, previous_position, current_position,
              &diagnostic, &hit_distance)) {
        const Matrix3x4& safe_endpoint =
            phase < 0.5 ? previous_camera->second.world
                        : current_camera->second.world;
        midpoint_camera->second.world.values[9] = safe_endpoint.values[9];
        midpoint_camera->second.world.values[10] = safe_endpoint.values[10];
        midpoint_camera->second.world.values[11] = safe_endpoint.values[11];
        stats.camera_temporal_chord_guard = 1u;
        ++g_camera_temporal_chord_guards;
        if (g_debug_log) {
          AppendNativeLog(
              "camera_temporal_chord_guard tick=%llu phase=%.3f "
              "node=%08llX resource=%llu tri=%llu hit=%.1f "
              "motion=%.1f "
              "from=%.0f/%.0f/%.0f to=%.0f/%.0f/%.0f "
              "selected=%s total=%llu",
              static_cast<unsigned long long>(source_tick), phase,
              static_cast<unsigned long long>(diagnostic.node),
              static_cast<unsigned long long>(diagnostic.resource),
              static_cast<unsigned long long>(diagnostic.triangle_index),
              hit_distance, diagnostic.bounds_motion, previous_position.x,
              previous_position.y,
              previous_position.z, current_position.x, current_position.y,
              current_position.z, phase < 0.5 ? "previous" : "current",
              static_cast<unsigned long long>(
                  g_camera_temporal_chord_guards));
        }
      }
    }
  }

  // A player's animated parent chain can contain a model-space pivot that is
  // refreshed on a different cadence from the scene-node world matrix. When
  // the hierarchy is rebuilt at an intermediate phase, that stale pivot can
  // move the complete actor far away from the direct interpolation of its two
  // real render roots. V48's
  // raster-entry trace exposed errors over 100 fixed-point units while the
  // adjacent exact roots differed by fewer than 15. Keep bone rotations and
  // relative animation hierarchical, but make the complete actor's root
  // translation agree with a direct rigid world-space interpolation.
  if (player_render_node) {
    const auto previous_player = previous.nodes.find(player_render_node);
    const auto current_player = current.nodes.find(player_render_node);
    auto midpoint_player = midpoint_nodes.find(player_render_node);
    if (previous_player != previous.nodes.end() &&
        current_player != current.nodes.end() &&
        midpoint_player != midpoint_nodes.end() &&
        midpoint_player->second.world_interpolated &&
        previous_player->second.parent == current_player->second.parent) {
      Matrix3x4 desired_world{};
      double ignored_translation = 0.0;
      double ignored_rotation = 0.0;
      if (InterpolateRigid(previous_player->second.world,
                           current_player->second.world, phase,
                           &desired_world, &ignored_translation,
                           &ignored_rotation)) {
        std::array<int32_t, 3> coherence{};
        for (size_t axis = 0; axis < coherence.size(); ++axis) {
          const int64_t delta =
              static_cast<int64_t>(desired_world.values[9u + axis]) -
              midpoint_player->second.world.values[9u + axis];
          coherence[axis] = static_cast<int32_t>(std::clamp<int64_t>(
              delta, std::numeric_limits<int32_t>::min(),
              std::numeric_limits<int32_t>::max()));
        }
        stats.player_root_coherence_offset = coherence;
        if (coherence != std::array<int32_t, 3>{}) {
          for (auto& entry : midpoint_nodes) {
            MidpointNode& midpoint = entry.second;
            if (!midpoint.world_interpolated ||
                !is_player_subtree_node(entry.first)) {
              continue;
            }
            for (size_t axis = 0; axis < coherence.size(); ++axis) {
              const int64_t shifted =
                  static_cast<int64_t>(midpoint.world.values[9u + axis]) +
                  coherence[axis];
              midpoint.world.values[9u + axis] = static_cast<int32_t>(
                  std::clamp<int64_t>(
                      shifted, std::numeric_limits<int32_t>::min(),
                      std::numeric_limits<int32_t>::max()));
            }
            ++stats.player_root_coherence_nodes;
          }
        }
      }
    }
  }
  if (player_contact_bridge_valid && !authoritative_contact_projection) {
    const std::array<double, 3> offset = {
        player_contact_bridge_offset.x,
        player_contact_bridge_offset.y,
        player_contact_bridge_offset.z};
    for (auto& entry : midpoint_nodes) {
      MidpointNode& midpoint = entry.second;
      if (!midpoint.world_interpolated ||
          !is_player_subtree_node(entry.first)) {
        continue;
      }
      for (size_t axis = 0; axis < offset.size(); ++axis) {
        const int64_t shifted =
            static_cast<int64_t>(midpoint.world.values[9u + axis]) +
            static_cast<int64_t>(ToFixed(offset[axis]));
        midpoint.world.values[9u + axis] = static_cast<int32_t>(
            std::clamp<int64_t>(shifted,
                                std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::max()));
      }
      ++stats.player_contact_bridge_nodes;
    }
  }
  if (current.player_contact_manifold_projection_valid) {
    // The uncorrected phase contains phase*C of the rejected inward endpoint,
    // so applying phase times the full endpoint projection keeps every
    // synthetic sample on the same contact plane without changing tangent
    // motion.
    std::array<int32_t, 3> phase_projection{};
    for (size_t axis = 0; axis < phase_projection.size(); ++axis) {
      phase_projection[axis] = static_cast<int32_t>(std::llround(
          phase * static_cast<double>(
                      current.player_contact_manifold_projection[axis])));
    }
    for (auto& entry : midpoint_nodes) {
      MidpointNode& midpoint = entry.second;
      if (!midpoint.world_interpolated ||
          !is_player_subtree_node(entry.first)) {
        continue;
      }
      for (size_t axis = 0; axis < phase_projection.size(); ++axis) {
        const int64_t shifted =
            static_cast<int64_t>(midpoint.world.values[9u + axis]) +
            phase_projection[axis];
        midpoint.world.values[9u + axis] = static_cast<int32_t>(
            std::clamp<int64_t>(shifted,
                                std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::max()));
      }
      ++stats.player_contact_manifold_midpoint_nodes;
    }
    g_player_contact_manifold_midpoint_nodes +=
        stats.player_contact_manifold_midpoint_nodes;
  }
  if (authoritative_contact_projection) {
    // The exact endpoint already contains the full resolver correction C.
    // A naive phase contains only phase*C, so add the missing (1-phase)*C to
    // the complete actor subtree. Never carry C into later exact endpoints:
    // doing that preserved the rejected pose and produced multi-frame ghosts.
    std::array<int32_t, 3> missing_projection{};
    for (size_t axis = 0; axis < missing_projection.size(); ++axis) {
      missing_projection[axis] = static_cast<int32_t>(std::llround(
          (1.0 - phase) * static_cast<double>(
                              current.player_contact_projection[axis])));
    }
    for (auto& entry : midpoint_nodes) {
      MidpointNode& midpoint = entry.second;
      if (!midpoint.world_interpolated ||
          !is_player_subtree_node(entry.first)) {
        continue;
      }
      for (size_t axis = 0; axis < missing_projection.size(); ++axis) {
        const int64_t shifted =
            static_cast<int64_t>(midpoint.world.values[9u + axis]) +
            missing_projection[axis];
        midpoint.world.values[9u + axis] = static_cast<int32_t>(
            std::clamp<int64_t>(shifted,
                                std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::max()));
      }
      ++stats.player_contact_projection_nodes;
    }
  }
  for (const auto& entry : midpoint_nodes) {
    if (!entry.second.world_interpolated) {
      continue;
    }
    if (SafeWrite(reinterpret_cast<void*>(entry.first + kMatrixOffset),
                  &entry.second.world, sizeof(entry.second.world))) {
      ++stats.applied;
    }
  }
  return stats;
}

void RestoreScene(const SceneSnapshot& current) {
  for (const auto& entry : current.nodes) {
    SafeWrite(reinterpret_cast<void*>(entry.first + kMatrixOffset),
              &entry.second.world, sizeof(entry.second.world));
  }
}

uint64_t ApplyPlayerEndpointOffset(
    const SceneSnapshot& scene, const std::array<int32_t, 3>& offset) {
  if (!scene.player) {
    return 0;
  }
  uint64_t applied = 0;
  for (const auto& entry : scene.nodes) {
    uintptr_t address = entry.first;
    bool player_subtree = false;
    for (size_t depth = 0; address && depth < 128u; ++depth) {
      if (address == scene.player) {
        player_subtree = true;
        break;
      }
      const auto node = scene.nodes.find(address);
      if (node == scene.nodes.end()) {
        break;
      }
      address = node->second.parent;
    }
    if (!player_subtree) {
      continue;
    }
    Matrix3x4 projected = entry.second.world;
    for (size_t axis = 0; axis < offset.size(); ++axis) {
      const int64_t shifted =
          static_cast<int64_t>(projected.values[9u + axis]) + offset[axis];
      projected.values[9u + axis] = static_cast<int32_t>(
          std::clamp<int64_t>(shifted,
                              std::numeric_limits<int32_t>::min(),
                              std::numeric_limits<int32_t>::max()));
    }
    if (SafeWrite(reinterpret_cast<void*>(entry.first + kMatrixOffset),
                  &projected, sizeof(projected))) {
      ++applied;
    }
  }
  return applied;
}

void RestorePlayerEndpoint(const SceneSnapshot& scene) {
  if (!scene.player) {
    return;
  }
  for (const auto& entry : scene.nodes) {
    uintptr_t address = entry.first;
    bool player_subtree = false;
    for (size_t depth = 0; address && depth < 128u; ++depth) {
      if (address == scene.player) {
        player_subtree = true;
        break;
      }
      const auto node = scene.nodes.find(address);
      if (node == scene.nodes.end()) {
        break;
      }
      address = node->second.parent;
    }
    if (player_subtree) {
      SafeWrite(reinterpret_cast<void*>(entry.first + kMatrixOffset),
                &entry.second.world, sizeof(entry.second.world));
    }
  }
}

bool ReadLiveNodeTranslation(uintptr_t node,
                             std::array<int32_t, 3>* translation) {
  if (!node || !translation) {
    return false;
  }
  Matrix3x4 world{};
  if (!SafeRead(reinterpret_cast<const void*>(node + kMatrixOffset),
                &world, sizeof(world))) {
    return false;
  }
  *translation = {world.values[9], world.values[10], world.values[11]};
  return true;
}

bool SnapshotPlayerTranslation(const SceneSnapshot& scene,
                               std::array<int32_t, 3>* translation) {
  if (!scene.player || !translation) {
    return false;
  }
  const auto player = scene.nodes.find(scene.player);
  if (player == scene.nodes.end()) {
    return false;
  }
  *translation = {player->second.world.values[9],
                  player->second.world.values[10],
                  player->second.world.values[11]};
  return true;
}

void FlushPresentationTraceBuffer() {
  if (!g_debug_log || g_presentation_trace_buffer.empty()) {
    g_presentation_trace_buffer.clear();
    return;
  }
  std::string block;
  block.reserve(g_presentation_trace_buffer.size() * 240u);
  for (const PresentationTraceSample& sample :
       g_presentation_trace_buffer) {
    char line[512] = {};
    const int length = std::snprintf(
        line, sizeof(line),
        "presentation_pair tick=%llu calls=%u/%u flags=%02X "
        "prev=%d/%d/%d current=%d/%d/%d midpoint_seen=%d/%d/%d "
        "exact_pre=%d/%d/%d exact_seen=%d/%d/%d projection=%d/%d/%d "
        "endpoint_nodes=%llu root_coherence=%d/%d/%d "
        "root_nodes=%llu\r\n",
        static_cast<unsigned long long>(sample.tick), sample.midpoint_calls,
        sample.exact_calls, sample.flags, sample.previous[0],
        sample.previous[1], sample.previous[2], sample.current[0],
        sample.current[1], sample.current[2], sample.midpoint_seen[0],
        sample.midpoint_seen[1], sample.midpoint_seen[2],
        sample.exact_before_projection[0],
        sample.exact_before_projection[1],
        sample.exact_before_projection[2], sample.exact_seen[0],
        sample.exact_seen[1], sample.exact_seen[2], sample.projection[0],
        sample.projection[1], sample.projection[2],
        static_cast<unsigned long long>(sample.exact_projection_nodes),
        sample.root_coherence[0], sample.root_coherence[1],
        sample.root_coherence[2],
        static_cast<unsigned long long>(sample.root_coherence_nodes));
    if (length > 0) {
      block.append(line, std::min<size_t>(static_cast<size_t>(length),
                                          sizeof(line) - 1u));
    }
  }
  AppendNativeLogBlock(block);
  g_presentation_trace_buffer.clear();
}

void QueuePresentationTrace(uint64_t source_tick,
                            const SceneSnapshot& previous,
                            const SceneSnapshot& current,
                            const InterpolationStats& interpolation) {
  const bool contact_active =
      interpolation.contact_reversal ||
      current.player_contact_manifold_projection_valid ||
      current.player_contact_projection_valid ||
      g_player_contact_hit_window || g_player_persistent_contact_cooldown;
  if (!g_debug_log || !contact_active || !current.player) {
    if (g_debug_log && source_tick % 120u == 0u) {
      FlushPresentationTraceBuffer();
    }
    return;
  }

  PresentationTraceSample sample;
  sample.tick = source_tick;
  SnapshotPlayerTranslation(previous, &sample.previous);
  SnapshotPlayerTranslation(current, &sample.current);
  sample.midpoint_seen = g_active_presentation_trace.midpoint_seen;
  sample.exact_before_projection =
      g_active_presentation_trace.exact_before_projection;
  sample.exact_seen = g_active_presentation_trace.exact_seen;
  sample.projection =
      current.player_contact_manifold_projection_valid
          ? current.player_contact_manifold_projection
          : std::array<int32_t, 3>{};
  sample.exact_projection_nodes =
      g_active_presentation_trace.exact_projection_nodes;
  sample.root_coherence = interpolation.player_root_coherence_offset;
  sample.root_coherence_nodes = interpolation.player_root_coherence_nodes;
  sample.midpoint_calls = g_active_presentation_trace.midpoint_calls;
  sample.exact_calls = g_active_presentation_trace.exact_calls;
  sample.flags =
      (g_active_presentation_trace.midpoint_seen_valid ? 1u : 0u) |
      (g_active_presentation_trace.exact_before_projection_valid ? 2u : 0u) |
      (g_active_presentation_trace.exact_seen_valid ? 4u : 0u) |
      (current.player_contact_manifold_projection_valid ? 8u : 0u) |
      (interpolation.contact_reversal ? 16u : 0u);
  g_presentation_trace_buffer.push_back(sample);
  if (g_presentation_trace_buffer.size() >= 16u ||
      source_tick % 120u == 0u) {
    FlushPresentationTraceBuffer();
  }
}

void __cdecl HookRenderer(void* context) {
  ActivePresentationTrace& trace = g_active_presentation_trace;
  if (!g_original_renderer) {
    return;
  }

  if (trace.stage == DeathtrapNativePresentationStage::kMidpoint) {
    ++trace.midpoint_calls;
    trace.midpoint_seen_valid =
        ReadLiveNodeTranslation(trace.player, &trace.midpoint_seen);
    g_original_renderer(context);
    return;
  }

  if (trace.stage == DeathtrapNativePresentationStage::kExact) {
    ++trace.exact_calls;
    trace.exact_before_projection_valid = ReadLiveNodeTranslation(
        trace.player, &trace.exact_before_projection);
    uint64_t projected_nodes = 0;
    if (trace.exact_projection_valid && trace.exact_scene) {
      // Apply the one-sided contact projection at the last possible point,
      // after the original render/present wrapper has refreshed every scene
      // cache. Earlier versions wrote the same matrices before that wrapper;
      // its cache update could silently replace them before rasterization.
      projected_nodes = ApplyPlayerEndpointOffset(
          *trace.exact_scene, trace.exact_projection);
      trace.exact_projection_nodes += projected_nodes;
    }
    trace.exact_seen_valid =
        ReadLiveNodeTranslation(trace.player, &trace.exact_seen);
    g_original_renderer(context);
    if (projected_nodes && trace.exact_scene) {
      RestorePlayerEndpoint(*trace.exact_scene);
    }
    return;
  }

  g_original_renderer(context);
}

PlayerMutableStateRollbackStats RestorePlayerMutableState(
    const SceneSnapshot& exact) {
  PlayerMutableStateRollbackStats stats;

  const auto restore_vector = [&stats](uintptr_t address,
                                       const std::array<int32_t, 3>& expected,
                                       bool valid, uint32_t bit) {
    if (!address || !valid) {
      return;
    }
    std::array<int32_t, 3> live{};
    if (!SafeRead(reinterpret_cast<const void*>(address), live.data(),
                  sizeof(live)) ||
        live == expected) {
      return;
    }
    stats.changed_mask |= bit;
    ++stats.changed_fields;
    for (size_t axis = 0; axis < live.size(); ++axis) {
      const int64_t delta =
          static_cast<int64_t>(live[axis]) - expected[axis];
      stats.maximum_delta =
          std::max<int64_t>(stats.maximum_delta, std::llabs(delta));
    }
    if (SafeWrite(reinterpret_cast<void*>(address), expected.data(),
                  sizeof(expected))) {
      stats.restored_mask |= bit;
    }
  };

  // The synthetic renderer must be a transaction.  Exact gameplay owns all
  // of these fields; the midpoint is allowed to consume them, never to leave
  // mutations behind for the next collision or animation update.
  restore_vector(exact.player, exact.player_position,
                 exact.player_position_valid, 1u << 0u);
  restore_vector(exact.player_object + 0x70u,
                 exact.player_cached_position,
                 exact.player_cached_position_valid, 1u << 1u);
  restore_vector(exact.player_object + 0xA4u, exact.player_bounds_a,
                 exact.player_bounds_a_valid, 1u << 2u);
  restore_vector(exact.player_object + 0xC4u, exact.player_bounds_b,
                 exact.player_bounds_b_valid, 1u << 3u);

  if (exact.player_object && exact.player_contact_count_valid) {
    uint8_t live_contact_count = 0;
    const uintptr_t address = exact.player_object + 0x44u;
    if (SafeRead(reinterpret_cast<const void*>(address), &live_contact_count,
                 sizeof(live_contact_count)) &&
        live_contact_count != exact.player_contact_count) {
      stats.changed_mask |= 1u << 4u;
      ++stats.changed_fields;
      stats.maximum_delta = std::max<int64_t>(
          stats.maximum_delta,
          std::llabs(static_cast<int64_t>(live_contact_count) -
                     exact.player_contact_count));
      if (SafeWrite(reinterpret_cast<void*>(address),
                    &exact.player_contact_count,
                    sizeof(exact.player_contact_count))) {
        stats.restored_mask |= 1u << 4u;
      }
    }
  }
  return stats;
}

void AdvanceSceneHistory(SceneSnapshot&& current) {
  g_older_snapshot = std::move(g_previous_snapshot);
  g_previous_snapshot = std::move(current);
}

void ObservePlayerLanding(const SceneSnapshot& current) {
  const bool position_valid = current.player_cached_position_valid ||
                              current.player_position_valid;
  if (!position_valid || !current.player_contact_count_valid) {
    g_landing_observer_valid = false;
    g_landing_observer_airborne = false;
    g_landing_observer_airborne_ticks = 0;
    g_landing_observer_peak_vertical_delta = 0;
    return;
  }
  const int32_t current_y = current.player_cached_position_valid
                                ? current.player_cached_position[1]
                                : current.player_position[1];
  if (!g_landing_observer_valid) {
    g_landing_observer_valid = true;
    g_landing_observer_previous_y = current_y;
    g_landing_observer_airborne = current.player_contact_count == 0;
    g_landing_observer_airborne_ticks =
        g_landing_observer_airborne ? 1u : 0u;
    return;
  }

  const int32_t vertical_delta = static_cast<int32_t>(std::min<int64_t>(
      std::llabs(static_cast<int64_t>(current_y) -
                 g_landing_observer_previous_y),
      std::numeric_limits<int32_t>::max()));
  const bool has_contact = current.player_contact_count != 0;
  if (!has_contact) {
    g_landing_observer_airborne = true;
    ++g_landing_observer_airborne_ticks;
    g_landing_observer_peak_vertical_delta = std::max(
        g_landing_observer_peak_vertical_delta, vertical_delta);
  } else if (g_landing_observer_airborne) {
    // Require both a real airborne endpoint and meaningful vertical motion.
    // This rejects ordinary wall/pipe contacts, which can also increment the
    // engine's contact counter while the player stays on the floor.
    constexpr int32_t kMinimumLandingVerticalDelta = 24;
    const int32_t peak = std::max(g_landing_observer_peak_vertical_delta,
                                  vertical_delta);
    if (g_landing_observer_airborne_ticks >= 1u &&
        peak >= kMinimumLandingVerticalDelta) {
      const uint32_t raw_percent = static_cast<uint32_t>(std::clamp(
          42 + peak / 8, 42, 90));
      const uint32_t scaled_percent = static_cast<uint32_t>(
          (static_cast<uint64_t>(raw_percent) *
               g_xinput_vibration_strength_percent +
           50u) /
          100u);
      g_landing_vibration_percent.store(scaled_percent,
                                         std::memory_order_release);
      g_landing_vibration_pending.store(true, std::memory_order_release);
      AppendNativeLog(
          "game_event landing queued airborne_ticks=%u peak_vertical=%d "
          "contacts=%u strength=%u duration_ms=%u",
          g_landing_observer_airborne_ticks, peak,
          current.player_contact_count, scaled_percent,
          g_xinput_landing_vibration_ms);
    }
    g_landing_observer_airborne = false;
    g_landing_observer_airborne_ticks = 0;
    g_landing_observer_peak_vertical_delta = 0;
  }
  g_landing_observer_previous_y = current_y;
}

void ResetSceneHistory() {
  FlushPresentationTraceBuffer();
  g_active_presentation_trace = {};
  g_older_snapshot = {};
  g_previous_snapshot = {};
  g_landing_observer_valid = false;
  g_landing_observer_airborne = false;
  g_landing_observer_airborne_ticks = 0;
  g_landing_observer_peak_vertical_delta = 0;
  g_player_contact_exact_cooldown = 0u;
  g_player_contact_hit_window = 0u;
  g_player_contact_hits_in_window = 0u;
  g_player_persistent_contact_cooldown = 0u;
  g_player_contact_outward_normal = {};
  g_player_contact_anchor = {};
  g_player_contact_normal_valid = false;
  g_player_contact_anchor_valid = false;
  g_player_contact_release_streak = 0u;
  g_player_contact_quiet_ticks = 0u;
  g_player_contact_release_alignment = 0.0;
  g_player_contact_outward_distance = 0.0;
  g_player_contact_correction_length = 0.0;
  g_matrix_axis_return_bins = {};
  g_raw_axis_return_bins = {};
  {
    std::lock_guard<std::mutex> lock(g_contact_projection_mutex);
    g_pending_contact_projection = {};
  }
}

void WaitUntil(const LARGE_INTEGER& target) {
  if (!g_qpc_frequency.QuadPart) {
    return;
  }
  for (;;) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const int64_t remaining = target.QuadPart - now.QuadPart;
    if (remaining <= 0) {
      return;
    }
    const double remaining_ms =
        static_cast<double>(remaining) * 1000.0 /
        static_cast<double>(g_qpc_frequency.QuadPart);
    if (remaining_ms > 2.0) {
      Sleep(1);
    } else {
      SwitchToThread();
    }
  }
}

bool RefreshCurrentRenderCaches(void* context) {
  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  uintptr_t scene_owner = 0;
  uintptr_t camera_owner = 0;
  if (!g_scene_cache_update || !g_camera_cache_update ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         context_address + kContextSceneOwnerOffset),
                     &scene_owner) ||
      !scene_owner ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         context_address + kContextCameraOwnerOffset),
                     &camera_owner) ||
      !camera_owner) {
    return false;
  }

  // Refresh both exact-current caches once before capture. Their owner stamps
  // make the synthetic and exact renderer calls skip duplicate updates for the
  // unchanged engine frame. The scene refresh also publishes the large native
  // animation-root contribution. At x2/x3 this explicit early call replaces
  // the later retail refresh, so it must share the same complete immersive
  // movement transaction as +0x810A0 rather than reverting to body-forward.
  const ImmersiveLocomotionTransaction movement_transaction =
      BeginImmersiveLocomotionTransaction();
  bool scene_refreshed = false;
  __try {
    g_scene_cache_update(reinterpret_cast<void*>(scene_owner));
    scene_refreshed = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    scene_refreshed = false;
  }
  EndImmersiveLocomotionTransaction(movement_transaction);
  if (!scene_refreshed) {
    return false;
  }
  __try {
    g_camera_cache_update(reinterpret_cast<void*>(camera_owner));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  if (movement_transaction.active && g_debug_log &&
      (g_source_ticks.load(std::memory_order_relaxed) % 60u) == 1u) {
    AppendNativeLog(
        "immersive scene_cache transaction motion_heading=%d "
        "collision_heading=%d restored_heading=%d controller=%p",
        movement_transaction.plan.motion_heading,
        movement_transaction.plan.transaction_heading,
        movement_transaction.preserved_body_heading,
        reinterpret_cast<void*>(movement_transaction.controller));
  }
  return true;
}

bool RenderMidpointWithoutAdvancingEngineClock(void* context) {
  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  uintptr_t backend_flip_address = 0;
  uint8_t context_flags = 0;
  if (!SafeReadValue(g_dungeon_base + kBackendFlipPointerRva,
                     &backend_flip_address) ||
      !backend_flip_address ||
      !SafeReadValue(reinterpret_cast<const void*>(context_address),
                     &context_flags)) {
    return false;
  }

  // Bit 2 permits an internal present from the renderer. The normal gameplay
  // context does not need it, but mask it defensively so the only midpoint
  // presentation is the direct backend Flip below.
  const uint8_t midpoint_flags = context_flags & ~uint8_t{0x04u};
  const bool prepared = SafeWrite(reinterpret_cast<void*>(context_address),
                                  &midpoint_flags, sizeof(midpoint_flags));

  bool rendered = false;
  __try {
    if (prepared) {
      g_renderer(context);
      reinterpret_cast<BackendFlipFn>(backend_flip_address)();

      // A legacy DirectDraw Flip rotates the game's front/back surface chain.
      // Leaving the midpoint page at the front makes the exact renderer use a
      // different historical page than the original engine expects. Deathtrap
      // keeps transient UI and partially redrawn sprite regions on those
      // pages, which manifests as disappearing selectors, actor tripling and
      // black tiles. Rotate the DirectDraw chain back immediately, while the
      // DXGI hook suppresses only this restore presentation. The display keeps
      // showing the midpoint until the exact endpoint is due, but the game
      // renders that endpoint into the same page it would have used natively.
      SetDeathtrapNativePageRestorePresentSuppressed(true);
      __try {
        reinterpret_cast<BackendFlipFn>(backend_flip_address)();
        ++g_page_restore_flips;
      } __finally {
        SetDeathtrapNativePageRestorePresentSuppressed(false);
      }
      rendered = true;
    }
  } __finally {
    SafeWrite(reinterpret_cast<void*>(context_address),
              &context_flags, sizeof(context_flags));
  }
  return rendered;
}

struct InterpolatedPassResult {
  InterpolationStats interpolation;
  PlayerMutableStateRollbackStats player_rollback;
  bool rendered = false;
};

InterpolatedPassResult RenderInterpolatedPass(
    void* context, const SceneSnapshot* older,
    const SceneSnapshot& previous, SceneSnapshot& current,
    double phase, bool update_temporal_state, uint64_t source_tick) {
  InterpolatedPassResult result;
  const UiRenderStateSnapshot ui_before = CaptureUiRenderState();
  result.interpolation = ApplyInterpolatedScene(
      older, previous, current, phase, update_temporal_state, source_tick);
  // The renderer also consumes Dungeon.dll's separately published camera
  // transform. Keeping only the scene node at the synthetic phase left the
  // view itself at the preceding 16.7 Hz endpoint and made orbit movement
  // visibly step even while actors were interpolated at 50 Hz.
  PublishLiveCameraMatrix(current.camera);

  // A synthetic matrix may cross a portal even though the adjacent exact
  // endpoints are both valid. Retail resolves camera_node+0x100 from the
  // final translation before rendering; keep the midpoint transaction under
  // the same contract instead of rendering it with the exact endpoint's room
  // sector. The snapshot value is restored below with every other synthetic
  // mutation.
  uintptr_t midpoint_camera_sector = 0;
  const bool midpoint_camera_sector_published = PublishLiveCameraSector(
      current.camera,
      current.camera_sector_valid ? current.camera_sector : 0,
      &midpoint_camera_sector);
  static uint64_t camera_sector_publish_failures = 0;
  static uint64_t camera_sector_changes = 0;
  if (!midpoint_camera_sector_published) {
    ++camera_sector_publish_failures;
    if (g_debug_log &&
        (camera_sector_publish_failures == 1u ||
         (camera_sector_publish_failures % 120u) == 0u)) {
      AppendNativeLog(
          "camera_midpoint_sector tick=%llu phase=%.3f result=FAILED "
          "failures=%llu",
          static_cast<unsigned long long>(source_tick), phase,
          static_cast<unsigned long long>(camera_sector_publish_failures));
    }
  } else if (current.camera_sector_valid &&
             midpoint_camera_sector != current.camera_sector) {
    ++camera_sector_changes;
    if (g_debug_log &&
        (camera_sector_changes == 1u ||
         (camera_sector_changes % 30u) == 0u)) {
      AppendNativeLog(
          "camera_midpoint_sector tick=%llu phase=%.3f result=CHANGED "
          "exact=%08llX midpoint=%08llX changes=%llu",
          static_cast<unsigned long long>(source_tick), phase,
          static_cast<unsigned long long>(current.camera_sector),
          static_cast<unsigned long long>(midpoint_camera_sector),
          static_cast<unsigned long long>(camera_sector_changes));
    }
  }

  g_active_presentation_trace.stage =
      DeathtrapNativePresentationStage::kMidpoint;
  result.rendered = RenderMidpointWithoutAdvancingEngineClock(context);
  g_active_presentation_trace.stage = DeathtrapNativePresentationStage::kNone;

  const UiRenderStateSnapshot ui_after = CaptureUiRenderState();
  if (UiRenderStateChanged(ui_before, ui_after)) {
    ++g_ui_state_changes;
  }
  if (UiTransientLogicChanged(ui_before, ui_after)) {
    ++g_ui_logic_rollbacks;
  }
  if (RestoreUiRenderState(ui_before)) {
    ++g_ui_state_restores;
  }

  result.player_rollback = RestorePlayerMutableState(current);
  if (result.player_rollback.changed_mask) {
    ++g_player_state_rollback_events;
    g_player_state_rollback_fields += result.player_rollback.changed_fields;
    g_player_state_rollback_maximum_delta = std::max(
        g_player_state_rollback_maximum_delta,
        result.player_rollback.maximum_delta);
    if (result.player_rollback.restored_mask !=
        result.player_rollback.changed_mask) {
      ++g_player_state_restore_failures;
    }
    if (g_debug_log &&
        source_tick - g_last_player_state_rollback_log_tick >= 30u) {
      AppendNativeLog(
          "player_state_rollback tick=%llu phase=%.3f changed_mask=%02X "
          "restored_mask=%02X fields=%llu max_delta=%lld events_total=%llu "
          "restore_failures=%llu",
          static_cast<unsigned long long>(source_tick), phase,
          result.player_rollback.changed_mask,
          result.player_rollback.restored_mask,
          static_cast<unsigned long long>(
              result.player_rollback.changed_fields),
          static_cast<long long>(result.player_rollback.maximum_delta),
          static_cast<unsigned long long>(g_player_state_rollback_events),
          static_cast<unsigned long long>(g_player_state_restore_failures));
      g_last_player_state_rollback_log_tick = source_tick;
    }
  }

  // Every synthetic pass is a transaction. The next phase starts from the
  // same exact-current matrices and mutable state as the first one.
  RestoreScene(current);
  if (current.camera && current.camera_sector_valid) {
    SafeWrite(reinterpret_cast<void*>(
                  current.camera + kCameraNodeSectorOffset),
              &current.camera_sector, sizeof(current.camera_sector));
  }
  PublishSnapshotCameraMatrix(current);
  if (result.rendered) {
    g_interpolated_frames.fetch_add(1, std::memory_order_relaxed);
    g_interpolated_nodes.fetch_add(result.interpolation.applied,
                                   std::memory_order_relaxed);
  }
  return result;
}

void CallOriginalRenderPresentWait(void* context, int wait) {
  g_original_render_present_wait(context, wait);
}

void __cdecl HookRenderPresentWait(void* context, int wait) {
  if ((GetAsyncKeyState(VK_F10) & 1) != 0 && IsGameForeground() &&
      DeathtrapGameplayReady(false)) {
    ToggleCustomHeadView("F10");
  }
  if ((GetAsyncKeyState(VK_F11) & 1) != 0) {
    const uint32_t previous =
        g_subframes.load(std::memory_order_relaxed);
    const uint32_t toggled = previous ? 0u : ConfiguredSubframes();
    g_subframes.store(toggled, std::memory_order_relaxed);
    ResetSceneHistory();
    AppendNativeLog("runtime native interpolation=%s passes=%u via F11",
                    toggled ? "ENABLED" : "DISABLED",
                    toggled ? toggled - 1u : 0u);
  }
  // Consume input only once at the real scheduler boundary. Synthetic render
  // phases never poll DirectInput and never feed another weapon action back
  // into the simulation.
  UpdateDeathtrapXInput();
  ConsumePendingWeaponWheel();
  const uint32_t subframes = g_subframes.load(std::memory_order_relaxed);
  if ((subframes != 2u && subframes != 3u) || wait <= 0 || !g_renderer) {
    CallOriginalRenderPresentWait(context, wait);
    // Gameplay camera collision still needs two exact scene snapshots when
    // presentation interpolation is disabled. Capture only after the retail
    // renderer has completed its normal scene/camera cache update; no extra
    // render or gameplay callback is introduced at x1.
    if (context && wait > 0) {
      SceneSnapshot exact = CaptureScene(context);
      // No midpoint exists at x1, but consume the source-transition marker so
      // enabling interpolation later cannot replay a stale cut.
      g_owned_camera_presentation_cut_pending.exchange(
          false, std::memory_order_acq_rel);
      const uint64_t source_tick =
          g_source_ticks.fetch_add(1, std::memory_order_relaxed) + 1;
      ProbeCameraState(context, exact, source_tick);
      ProbePlayerHeadJoints(exact, source_tick);
      if (exact.nodes.empty() || !exact.root) {
        ResetSceneHistory();
      } else if (!g_previous_snapshot.nodes.empty() &&
                 g_previous_snapshot.root != exact.root) {
        g_older_snapshot = {};
        g_previous_snapshot = std::move(exact);
      } else {
        AdvanceSceneHistory(std::move(exact));
      }
    }
    return;
  }

  if (!RefreshCurrentRenderCaches(context)) {
    CallOriginalRenderPresentWait(context, wait);
    ResetSceneHistory();
    return;
  }

  SceneSnapshot current = CaptureScene(context);
  const bool owned_camera_transition_cut =
      g_owned_camera_presentation_cut_pending.exchange(
          false, std::memory_order_acq_rel);
  const bool scene_history_boundary =
      current.nodes.empty() || g_previous_snapshot.nodes.empty() ||
      current.root == 0 || current.root != g_previous_snapshot.root;
  // The source-tick camera is the only gameplay presentation owner. Applying
  // a second render-only translation here would retain a different rotation,
  // recreating the invisible-centre/stuck-camera failure.
  const bool modern_follow_target = false;
  const bool custom_camera_transition =
      !scene_history_boundary && ApplyCustomCameraTransition(&current);
  if (modern_follow_target || custom_camera_transition) {
    // The transition is render-only. Native controller state remains mode 3;
    // only the captured camera transform and its published mirror are moved.
    RestoreScene(current);
    PublishSnapshotCameraMatrix(current);
  }
  ConsumePendingContactProjection(&current);
  ObservePlayerLanding(current);
  const uint64_t source_tick =
      g_source_ticks.fetch_add(1, std::memory_order_relaxed) + 1;
  ProbeCameraState(context, current, source_tick);
  ProbePlayerHeadJoints(current, source_tick);
  SampleUiEligibility();
  if (scene_history_boundary) {
    CallOriginalRenderPresentWait(context, wait);
    g_older_snapshot = {};
    g_previous_snapshot = std::move(current);
    return;
  }

  TransitionStats transition =
      AnalyzeTransition(context, g_previous_snapshot, current);
  if (custom_camera_transition) {
    transition.suppress_midpoint = true;
    transition.reason = "camera_mode_safe_cut";
  }
  if (owned_camera_transition_cut) {
    transition.suppress_midpoint = true;
    transition.reason = "owned_camera_safe_cut";
  }
  if (transition.suppress_midpoint) {
    g_transition_guard_cooldown = 1u;
  } else if (g_transition_guard_cooldown) {
    transition.suppress_midpoint = true;
    transition.reason = "post_transition";
    --g_transition_guard_cooldown;
  }
  if (transition.suppress_midpoint) {
    ++g_suppressed_midpoints;
    if (g_debug_log &&
        source_tick - g_last_transition_log_tick >= 30u) {
      AppendNativeLog(
          "transition_guard tick=%llu reason=%s matched=%zu entered=%zu "
          "departed=%zu overlap=%.4f camera_move=%.3f camera_rotation_deg=%.2f "
          "camera_tree=%u "
          "callback50_rva=%08llX callback54_rva=%08llX suppressed_total=%llu",
          static_cast<unsigned long long>(source_tick), transition.reason,
          transition.matched, transition.entered, transition.departed,
          transition.overlap, transition.camera_translation,
          transition.camera_rotation_degrees,
          transition.camera_in_scene_tree ? 1u : 0u,
          static_cast<unsigned long long>(
              DungeonRva(transition.primary_callback)),
          static_cast<unsigned long long>(
              DungeonRva(transition.secondary_callback)),
          static_cast<unsigned long long>(g_suppressed_midpoints));
      g_last_transition_log_tick = source_tick;
    }
    CallOriginalRenderPresentWait(context, wait);
    AdvanceSceneHistory(std::move(current));
    return;
  }

  LARGE_INTEGER start{};
  QueryPerformanceCounter(&start);
  const int64_t phase_ticks =
      (g_qpc_frequency.QuadPart * kOriginalPeriodMilliseconds) /
      (1000ll * static_cast<int64_t>(subframes));
  const double first_phase = 1.0 / static_cast<double>(subframes);

  const SceneSnapshot* older =
      g_older_snapshot.root == current.root && !g_older_snapshot.nodes.empty()
          ? &g_older_snapshot
          : nullptr;
  g_active_presentation_trace = {};
  g_active_presentation_trace.tick = source_tick;
  g_active_presentation_trace.player = current.player;
  const InterpolatedPassResult first_pass = RenderInterpolatedPass(
      context, older, g_previous_snapshot, current, first_phase,
      true, source_tick);
  const InterpolationStats& interpolation = first_pass.interpolation;
  const PlayerMutableStateRollbackStats& player_rollback =
      first_pass.player_rollback;
  if (older && interpolation.contact_reversal) {
    LogPlayerProbeTriplet(source_tick, *older, g_previous_snapshot, current);
  }
  if (interpolation.player_bounds_axis_mask) {
    LogBoundsApplication(source_tick, g_previous_snapshot, current,
                         interpolation.player_bounds_axis_mask);
  }
  if (!first_pass.rendered) {
    CallOriginalRenderPresentWait(context, wait);
    AdvanceSceneHistory(std::move(current));
    return;
  }
  g_player_contact_settles += interpolation.player_contact_settle;
  g_player_raw_contact_detections += interpolation.player_raw_contact;
  if (g_debug_log && source_tick % 120u == 0u) {
    AppendNativeLog(
        "tick=%llu captured=%zu applied=%llu hierarchy=%llu world_fallback=%llu "
        "exact=%llu invalid=%llu parent_mismatch=%llu contact_reversal=%llu "
        "temporal_pose_guard=%llu player_root_found=%llu "
        "player_contact_settle=%llu player_raw_contact=%llu "
        "player_exact_subtree_nodes=%llu contact_settle_total=%llu "
        "raw_contact_total=%llu matrix_candidates=%llu raw_candidates=%llu "
        "contact_cooldown=%u contact_hits=%u "
        "contact_hit_window=%u persistent_contact=%u "
        "directional_release=%llu directional_release_total=%llu "
        "release_streak=%u release_quiet=%u release_alignment=%.3f "
        "release_outward=%.6f correction_length=%.6f normal_valid=%u "
        "bounds_nodes=%llu bounds_axes=%u projection=%d/%d/%d "
        "projection_seq=%llu projection_events=%llu projection_nodes=%llu "
        "projection_captures=%llu projection_consumes=%llu "
        "projection_54d00=%llu projection_68390=%llu "
        "projection_68390_small_rejected=%llu bridge_events=%llu "
        "bridge_nodes=%llu manifold=%d/%d/%d manifold_event=%llu "
        "manifold_events_total=%llu manifold_midpoint_nodes=%llu "
        "manifold_endpoint_nodes=%llu manifold_depth=%.6f "
        "manifold_max_depth=%.6f normal_flip_rejected=%llu "
        "normal_flip_rejected_total=%llu "
        "max_move=%.3f "
        "max_rotation_deg=%.2f overlap=%.4f entered=%zu departed=%zu "
        "camera_move=%.3f camera_rotation_deg=%.2f camera_tree=%u "
        "callback50_rva=%08llX "
        "callback54_rva=%08llX suppressed_total=%llu ui_changed=%llu "
        "ui_restored=%llu ui_logic_rollbacks=%llu page_restores=%llu "
        "player_rollback_mask=%02X player_restore_mask=%02X "
        "player_rollback_events=%llu player_rollback_fields=%llu "
        "player_restore_failures=%llu player_rollback_max_delta=%lld "
        "suppressed_dxgi=%llu "
        "ui_owner_ticks=%llu ui_object_ticks=%llu ui_gate_open=%llu "
        "ui_gate_closed=%llu ui_delta=%lld",
        static_cast<unsigned long long>(source_tick), current.nodes.size(),
        static_cast<unsigned long long>(interpolation.applied),
        static_cast<unsigned long long>(interpolation.hierarchical),
        static_cast<unsigned long long>(interpolation.world_fallback),
        static_cast<unsigned long long>(interpolation.exact),
        static_cast<unsigned long long>(interpolation.invalid),
        static_cast<unsigned long long>(interpolation.parent_mismatch),
        static_cast<unsigned long long>(interpolation.contact_reversal),
        static_cast<unsigned long long>(interpolation.temporal_pose_guard),
        static_cast<unsigned long long>(interpolation.player_root_found),
        static_cast<unsigned long long>(interpolation.player_contact_settle),
        static_cast<unsigned long long>(interpolation.player_raw_contact),
        static_cast<unsigned long long>(
            interpolation.player_exact_subtree_nodes),
        static_cast<unsigned long long>(g_player_contact_settles),
        static_cast<unsigned long long>(g_player_raw_contact_detections),
        static_cast<unsigned long long>(g_player_matrix_contact_candidates),
        static_cast<unsigned long long>(g_player_raw_contact_candidates),
        g_player_contact_exact_cooldown,
        g_player_contact_hits_in_window,
        g_player_contact_hit_window,
        g_player_persistent_contact_cooldown,
        static_cast<unsigned long long>(
            interpolation.player_directional_release),
        static_cast<unsigned long long>(g_player_contact_directional_releases),
        g_player_contact_release_streak,
        g_player_contact_quiet_ticks,
        g_player_contact_release_alignment,
        g_player_contact_outward_distance,
        g_player_contact_correction_length,
        g_player_contact_normal_valid ? 1u : 0u,
        static_cast<unsigned long long>(
            interpolation.player_bounds_guided_nodes),
        interpolation.player_bounds_axis_mask,
        current.player_contact_projection[0],
        current.player_contact_projection[1],
        current.player_contact_projection[2],
        static_cast<unsigned long long>(
            current.player_contact_projection_sequence),
        static_cast<unsigned long long>(
            interpolation.player_contact_projection_events),
        static_cast<unsigned long long>(
            interpolation.player_contact_projection_nodes),
        static_cast<unsigned long long>(g_contact_projection_captures),
        static_cast<unsigned long long>(g_contact_projection_consumes),
        static_cast<unsigned long long>(g_contact_projection_54d00_captures),
        static_cast<unsigned long long>(g_contact_projection_68390_captures),
        static_cast<unsigned long long>(
            g_contact_projection_68390_small_rejections),
        static_cast<unsigned long long>(
            interpolation.player_contact_bridge_events),
        static_cast<unsigned long long>(
            interpolation.player_contact_bridge_nodes),
        current.player_contact_manifold_projection[0],
        current.player_contact_manifold_projection[1],
        current.player_contact_manifold_projection[2],
        static_cast<unsigned long long>(
            interpolation.player_contact_manifold_events),
        static_cast<unsigned long long>(g_player_contact_manifold_events),
        static_cast<unsigned long long>(
            g_player_contact_manifold_midpoint_nodes),
        static_cast<unsigned long long>(
            g_player_contact_manifold_endpoint_nodes),
        interpolation.player_contact_manifold_depth,
        g_player_contact_manifold_max_depth,
        static_cast<unsigned long long>(
            interpolation.player_contact_normal_flip_rejections),
        static_cast<unsigned long long>(
            g_player_contact_normal_flip_rejections),
        interpolation.max_translation, interpolation.max_rotation_degrees,
        transition.overlap, transition.entered, transition.departed,
        transition.camera_translation, transition.camera_rotation_degrees,
        transition.camera_in_scene_tree ? 1u : 0u,
        static_cast<unsigned long long>(
            DungeonRva(transition.primary_callback)),
        static_cast<unsigned long long>(
            DungeonRva(transition.secondary_callback)),
        static_cast<unsigned long long>(g_suppressed_midpoints),
        static_cast<unsigned long long>(g_ui_state_changes),
        static_cast<unsigned long long>(g_ui_state_restores),
        static_cast<unsigned long long>(g_ui_logic_rollbacks),
        static_cast<unsigned long long>(g_page_restore_flips),
        player_rollback.changed_mask, player_rollback.restored_mask,
        static_cast<unsigned long long>(g_player_state_rollback_events),
        static_cast<unsigned long long>(g_player_state_rollback_fields),
        static_cast<unsigned long long>(g_player_state_restore_failures),
        static_cast<long long>(g_player_state_rollback_maximum_delta),
        static_cast<unsigned long long>(
            GetDeathtrapNativeSuppressedPresentCount()),
        static_cast<unsigned long long>(g_ui_owner_ticks),
        static_cast<unsigned long long>(g_ui_object_ticks),
        static_cast<unsigned long long>(g_ui_gate_open_ticks),
        static_cast<unsigned long long>(g_ui_gate_closed_ticks),
        static_cast<long long>(g_last_ui_frame_delta));
    AppendNativeLog(
        "axis_return_bins tick=%llu bins=<.25/.50/.75/1.0 "
        "matrix_x=%llu/%llu/%llu/%llu matrix_y=%llu/%llu/%llu/%llu "
        "matrix_z=%llu/%llu/%llu/%llu raw_x=%llu/%llu/%llu/%llu "
        "raw_y=%llu/%llu/%llu/%llu raw_z=%llu/%llu/%llu/%llu",
        static_cast<unsigned long long>(source_tick),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[0][0]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[0][1]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[0][2]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[0][3]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[1][0]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[1][1]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[1][2]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[1][3]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[2][0]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[2][1]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[2][2]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[2][3]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[0][0]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[0][1]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[0][2]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[0][3]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[1][0]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[1][1]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[1][2]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[1][3]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[2][0]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[2][1]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[2][2]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[2][3]));
    g_matrix_axis_return_bins = {};
    g_raw_axis_return_bins = {};
  }

  LARGE_INTEGER target{};
  target.QuadPart = start.QuadPart + phase_ticks;
  WaitUntil(target);

  if (subframes == 3u) {
    // Temporal contact state was advanced by the first pass. The second pass
    // reuses its resolved projection and performs only another render
    // transaction at 2/3, so collision cooldowns still advance exactly once
    // per real source tick.
    const InterpolatedPassResult second_pass = RenderInterpolatedPass(
        context, older, g_previous_snapshot, current, 2.0 / 3.0,
        false, source_tick);
    if (!second_pass.rendered && g_debug_log) {
      AppendNativeLog("interpolated phase failed tick=%llu phase=0.667",
                      static_cast<unsigned long long>(source_tick));
    }
    target.QuadPart = start.QuadPart + phase_ticks * 2ll;
    WaitUntil(target);
  }

  // The original routine performs the exact current-state render, advances
  // the engine frame counter once, runs its normal housekeeping, and waits
  // the remaining final fraction of the native six-count scheduler period.
  RestoreScene(current);
  PublishSnapshotCameraMatrix(current);
  g_active_presentation_trace.stage = DeathtrapNativePresentationStage::kExact;
  g_active_presentation_trace.exact_scene = &current;
  if (current.player_contact_manifold_projection_valid) {
    g_active_presentation_trace.exact_projection =
        current.player_contact_manifold_projection;
    g_active_presentation_trace.exact_projection_valid = true;
  }
  CallOriginalRenderPresentWait(context, wait);
  g_active_presentation_trace.stage = DeathtrapNativePresentationStage::kNone;
  g_active_presentation_trace.exact_scene = nullptr;
  g_player_contact_manifold_endpoint_nodes +=
      g_active_presentation_trace.exact_projection_nodes;
  QueuePresentationTrace(source_tick, g_previous_snapshot, current,
                         interpolation);
  AdvanceSceneHistory(std::move(current));
}

void InitializePatchState() {
  g_debug_log = ConfiguredDebugLog();
  g_music_track_fix_enabled =
      ConfiguredInteger(L"Audio", L"FixMusicTracks", 1) != 0;
  g_camera_probe_enabled =
      ConfiguredInteger(L"Diagnostics", L"CameraProbe", 0) != 0;
  g_head_joint_probe_enabled =
      ConfiguredInteger(L"Diagnostics", L"HeadJointProbe", 0) != 0;
  g_weapon_wheel_enabled = ConfiguredWeaponWheelEnabled();
  g_weapon_wheel_invert = ConfiguredWeaponWheelInvert();
  g_xinput_enabled =
      ConfiguredInteger(L"XInput", L"Enabled", 1) != 0;
  g_xinput_base_bindings =
      ConfiguredInteger(L"XInput", L"BaseBindings", 1) != 0;
  g_xinput_controller_index = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"Controller", 0), 0, 3));
  g_xinput_selector_hold_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"SelectorHoldMs", 225), 150, 600));
  g_xinput_left_deadzone = std::clamp(
      ConfiguredInteger(L"XInput", L"LeftStickDeadzone",
                        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
      0, 30000);
  g_xinput_right_deadzone = std::clamp(
      ConfiguredInteger(L"XInput", L"RightStickDeadzone",
                        XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
      0, 30000);
  g_xinput_trigger_threshold = std::clamp(
      ConfiguredInteger(L"XInput", L"TriggerThreshold",
                        XINPUT_GAMEPAD_TRIGGER_THRESHOLD),
      0, 255);
  g_xinput_first_person_pixels = std::clamp(
      ConfiguredInteger(L"XInput", L"RightStickPixelsPerTick", 12), 1, 80);
  g_xinput_menu_mouse_pixels = std::clamp(
      ConfiguredInteger(L"XInput", L"MenuRightStickPixelsPerTick", 6), 1, 40);
  g_xinput_right_stick_curve = static_cast<double>(std::clamp(
      ConfiguredInteger(L"XInput", L"RightStickResponseCurvePercent", 135),
      100, 250)) / 100.0;
  g_xinput_right_stick_axis_lock_ratio =
      static_cast<double>(std::clamp(
          ConfiguredInteger(L"XInput", L"RightStickAxisLockPercent", 0),
          0, 50)) / 100.0;
  g_third_person_stick_speed_scale =
      static_cast<double>(std::clamp(
          ConfiguredInteger(L"XInput", L"ThirdPersonOrbitSpeedPercent", 140),
          50, 300)) / 100.0;
  g_xinput_invert_right_y =
      ConfiguredInteger(L"XInput", L"InvertRightY", 0) != 0;
  g_third_person_orbit_enabled =
      ConfiguredInteger(L"Camera", L"ThirdPersonOrbit", 1) != 0;
  g_immersive_first_person_enabled =
      ConfiguredInteger(L"Camera", L"ImmersiveFirstPerson", 1) != 0;
  g_third_person_orbit_invert_x =
      ConfiguredInteger(L"Camera", L"InvertX", 0) != 0;
  g_third_person_orbit_invert_y =
      ConfiguredInteger(L"Camera", L"InvertY", 0) != 0;
  g_third_person_orbit_horizontal_radians =
      static_cast<double>(std::clamp(
          ConfiguredInteger(L"Camera", L"HorizontalDegreesPerTick", 5),
          1, 30)) * kOrbitPi / 180.0;
  g_third_person_orbit_vertical_radians =
      static_cast<double>(std::clamp(
          ConfiguredInteger(L"Camera", L"VerticalDegreesPerTick", 3),
          1, 20)) * kOrbitPi / 180.0;
  g_third_person_mouse_horizontal_radians =
      static_cast<double>(std::clamp(
          ConfiguredInteger(L"Camera", L"MouseHorizontalMilliDegreesPerPixel",
                            180),
          20, 1000)) * kOrbitPi / 180000.0;
  g_third_person_mouse_vertical_radians =
      static_cast<double>(std::clamp(
          ConfiguredInteger(L"Camera", L"MouseVerticalMilliDegreesPerPixel",
                            150),
          20, 1000)) * kOrbitPi / 180000.0;
  g_third_person_orbit_response_seconds =
      static_cast<double>(std::clamp(
          ConfiguredInteger(L"Camera", L"ResponseTimeMs", 50), 20, 300)) /
      1000.0;
  int32_t minimum_pitch_degrees = std::clamp(
      ConfiguredInteger(L"Camera", L"MinimumPitchDegrees", -35), -50, 70);
  int32_t maximum_pitch_degrees = std::clamp(
      ConfiguredInteger(L"Camera", L"MaximumPitchDegrees", 55), -20, 80);
  if (maximum_pitch_degrees <= minimum_pitch_degrees) {
    maximum_pitch_degrees = std::min(80, minimum_pitch_degrees + 20);
  }
  g_third_person_orbit_min_pitch_radians =
      static_cast<double>(minimum_pitch_degrees) * kOrbitPi / 180.0;
  g_third_person_orbit_max_pitch_radians =
      static_cast<double>(maximum_pitch_degrees) * kOrbitPi / 180.0;
  int32_t head_minimum_pitch_degrees = std::clamp(
      ConfiguredInteger(L"Camera", L"HeadMinimumPitchDegrees", -75),
      -85, 60);
  int32_t head_maximum_pitch_degrees = std::clamp(
      ConfiguredInteger(L"Camera", L"HeadMaximumPitchDegrees", 75),
      -40, 85);
  if (head_maximum_pitch_degrees <= head_minimum_pitch_degrees) {
    head_maximum_pitch_degrees =
        std::min(85, head_minimum_pitch_degrees + 40);
  }
  g_custom_head_min_pitch_radians =
      static_cast<double>(head_minimum_pitch_degrees) * kOrbitPi / 180.0;
  g_custom_head_max_pitch_radians =
      static_cast<double>(head_maximum_pitch_degrees) * kOrbitPi / 180.0;
  g_custom_head_height = std::clamp(
      ConfiguredInteger(L"Camera", L"HeadHeight", 10), 0, 300);
  g_custom_head_forward_offset = std::clamp(
      ConfiguredInteger(L"Camera", L"HeadForwardOffset", 55), 0, 220);
  g_third_person_orbit_min_radius = static_cast<double>(std::clamp(
      ConfiguredInteger(L"Camera", L"MinimumRadius", 650), 200, 3000));
  g_third_person_orbit_max_radius = static_cast<double>(std::clamp(
      ConfiguredInteger(L"Camera", L"MaximumRadius", 1800), 400, 5000));
  if (g_third_person_orbit_max_radius <=
      g_third_person_orbit_min_radius) {
    g_third_person_orbit_max_radius =
        g_third_person_orbit_min_radius + 500.0;
  }
  g_third_person_orbit_preferred_radius = static_cast<double>(std::clamp(
      ConfiguredInteger(L"Camera", L"PreferredRadius", 1400),
      static_cast<int32_t>(g_third_person_orbit_min_radius),
      static_cast<int32_t>(g_third_person_orbit_max_radius)));
  g_xinput_vibration_enabled =
      ConfiguredInteger(L"XInput", L"VibrationEnabled", 1) != 0;
  g_xinput_vibration_strength_percent = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"VibrationStrengthPercent", 100),
      0, 100));
  g_xinput_melee_swing_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"MeleeSwingVibrationMs", 170), 40, 400));
  g_xinput_block_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"BlockVibrationMs", 60), 20, 250));
  g_xinput_successful_block_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"SuccessfulBlockVibrationMs", 210),
      60, 500));
  g_xinput_spell_cast_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"SpellCastVibrationMs", 260),
      80, 600));
  g_xinput_ranged_shot_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"RangedShotVibrationMs", 115),
      40, 350));
  g_xinput_healing_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"HealingVibrationMs", 320),
      100, 800));
  g_xinput_selector_tick_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"SelectorTickVibrationMs", 38),
      15, 120));
  g_xinput_landing_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"LandingVibrationMs", 145),
      50, 450));
  g_xinput_heavy_damage_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"HeavyDamageVibrationMs", 380),
      120, 900));
  g_xinput_heavy_damage_threshold_hp = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"HeavyDamageThresholdHp", 12),
      4, 100));
  g_xinput_hit_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"HitVibrationMs", 150), 60, 400));
  g_xinput_damage_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"DamageVibrationMs", 240), 80, 600));
  g_xinput_death_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"DeathVibrationMs", 700), 200, 1500));
  g_ui_message_lifetime_percent = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"Text", L"MessageLifetimePercent", 300),
      100, 1000));
  g_ui_message_lifetime_ticks = static_cast<uint32_t>(
      (kOriginalUiMessageLifetimeTicks * g_ui_message_lifetime_percent + 50u) /
      100u);
  g_pst_message_lifetime_ticks = static_cast<uint32_t>(
      (kOriginalPstMessageLifetimeTicks * g_ui_message_lifetime_percent +
       50u) /
      100u);
  g_xinput_selector_radius = std::clamp(
      ConfiguredInteger(L"XInput", L"SelectorRadius", 104), 64, 160);
  g_xinput_selector_center_y = std::clamp(
      ConfiguredInteger(L"XInput", L"SelectorCenterY", 316), 192, 400);
  g_xinput_movement_threshold = static_cast<double>(std::clamp(
      ConfiguredInteger(L"XInput", L"MovementThresholdPercent", 14),
      10, 60)) / 100.0;
  g_xinput_run_threshold = static_cast<double>(std::clamp(
      ConfiguredInteger(L"XInput", L"RunThresholdPercent", 50),
      40, 95)) / 100.0;
  g_xinput_run_release_threshold = static_cast<double>(std::clamp(
      ConfiguredInteger(L"XInput", L"RunReleaseThresholdPercent", 30),
      20, 80)) / 100.0;
  if (g_xinput_run_release_threshold >= g_xinput_run_threshold) {
    g_xinput_run_release_threshold =
        std::max(0.20, g_xinput_run_threshold - 0.12);
  }
  g_xinput_camera_relative_movement =
      ConfiguredInteger(L"XInput", L"CameraRelativeMovement", 1) != 0;
  g_xinput_camera_relative_invert_y =
      ConfiguredInteger(L"XInput", L"CameraRelativeInvertY", 1) != 0;
  g_xinput_movement_turn_degrees_per_tick =
      static_cast<double>(std::clamp(
          ConfiguredInteger(L"XInput", L"MovementTurnDegreesPerTick", 12),
          4, 30));
  if (g_xinput_enabled) {
    LoadXInputRuntime();
  }
  const uint32_t subframes = ConfiguredSubframes();
  g_subframes.store(subframes, std::memory_order_relaxed);
  LogCompactSupportEnvironment(subframes);

  HMODULE dungeon = GetModuleHandleW(L"Dungeon.dll");
  if (!dungeon) {
    AppendDeathtrapSupportLog("support_patch state=dungeon_module_missing");
    g_state.store(DeathtrapNativeRenderPatchState::kDungeonModuleMissing,
                  std::memory_order_release);
    return;
  }
  g_dungeon_base = reinterpret_cast<uint8_t*>(dungeon);
  if (!IsExpectedDungeonImage(g_dungeon_base)) {
    AppendDeathtrapSupportLog("support_patch state=unsupported_dungeon_dll");
    g_state.store(DeathtrapNativeRenderPatchState::kUnsupportedDungeonDll,
                  std::memory_order_release);
    return;
  }

  g_ui_message_lifetime_patched =
      PatchUiMessageLifetime(g_ui_message_lifetime_ticks);
  g_pst_message_lifetime_patched =
      PatchPstMessageLifetime(g_pst_message_lifetime_ticks);

  QueryPerformanceFrequency(&g_qpc_frequency);
  g_renderer = reinterpret_cast<RendererFn>(g_dungeon_base + kRendererRva);
  g_scene_cache_update = reinterpret_cast<RenderCacheUpdateFn>(
      g_dungeon_base + kSceneCacheUpdateRva);
  g_camera_cache_update = reinterpret_cast<RenderCacheUpdateFn>(
      g_dungeon_base + kCameraCacheUpdateRva);
  g_camera_node_local_update = reinterpret_cast<RenderCacheUpdateFn>(
      g_dungeon_base + kCameraNodeLocalUpdateRva);
  g_camera_node_world_update = reinterpret_cast<RenderCacheUpdateFn>(
      g_dungeon_base + kCameraNodeWorldUpdateRva);
  AppendNativeLog(
      "Deathtrap native render overlay " DEATHTRAP_NATIVE_VERSION
      " uses compact per-launch "
      "support logging and preserves the accepted v0.0.214 immersive camera "
      "and camera-relative locomotion; "
      "0.0.197 keeps the animated head mount "
      "through the complete configured pitch range; "
      "0.0.196 keeps the animated head mount "
      "through neck translation and aligns its right-stick vertical axis; "
      "0.0.195 mounts the immersive eye on "
      "the structurally resolved animated head joint and advances it in "
      "front of the face while preserving user-owned rotation; "
      "0.0.193 bound the immersive eye to "
      "the stable player focus, corrects its look-at orientation and keeps "
      "the body-visible placement; "
      "it keeps the Steam "
      "Redbook-to-MP3 routing fix; it publishes one fully-owned "
      "collision-safe room+scene gameplay-camera pose per source tick, with "
      "detached matrix construction, atomic verified live publication, "
      "initial-overlap ray exit, radial near-pivot collapse, bounded scripted "
      "owner-convergence takeover with completed-owner replay suppression, "
      "smooth retail first-person presentation, no automatic gameplay yaw, "
      "immediate contraction, sustained-margin "
      "radial release and presentation cuts "
      "across disconnected safe shots; scripted reveals remain native and a "
      "transaction failure explicitly falls back to the 0.0.172 hybrid; "
      "it uses one pre-history "
      "scene-mesh candidate owner before the retail position-ring average; "
      "accepted boundaries are validated against real render triangles, not "
      "conservative empty OBB space; post-native exact correction remains a "
      "stateless fail-closed safety net and delayed retail publication is never "
      "fed back as collision evidence; legacy presentation latch/follow, "
      "post-history repair and post-native radius-gate layers are removed; "
      "dense diagnostics reuse one synchronized session-log handle; "
      "x1/x2/x3 share the same gameplay "
      "camera and x2/x3 interpolate finalized exact endpoints; DirectInput "
      "wheel events "
      "are observation-only and commit through Dungeon.dll+0x90610 once per "
      "real gameplay tick (enabled=%u invert=%u); XInput controller=%u "
      "base_bindings=%u hold_ms=%u deadzones=%d/%d axis_lock=%u%% "
      "third_person_stick_speed=%u%% "
      "radial=%d center_y=%d "
      "camera_relative_movement=%u invert_y=%u turn=%.0fdeg "
      "vibration=%u/%u%% action=%u/%u/%u/%ums event=%u/%u/%u/%u/%u/"
      "%u/%ums heavy=%uhp/%ums "
      "available=%u camera_probe=%u orbit=%u immersive_first_person=%u "
      "sensitivity=%d/%ddeg "
      "pitch=%d..%d head_pitch=%d..%d head=%d/%d radius=%d..%d invert=%u/%u "
      "message_lifetime=%u%% ui=%u_ticks/%u pst=%u_ticks/%u",
      g_weapon_wheel_enabled ? 1u : 0u,
      g_weapon_wheel_invert ? 1u : 0u,
      g_xinput_controller_index,
      g_xinput_base_bindings ? 1u : 0u,
      g_xinput_selector_hold_ms,
      g_xinput_left_deadzone,
      g_xinput_right_deadzone,
      static_cast<unsigned>(std::lround(
          g_xinput_right_stick_axis_lock_ratio * 100.0)),
      static_cast<unsigned>(std::lround(
          g_third_person_stick_speed_scale * 100.0)),
      g_xinput_selector_radius,
      g_xinput_selector_center_y,
      g_xinput_camera_relative_movement ? 1u : 0u,
      g_xinput_camera_relative_invert_y ? 1u : 0u,
      g_xinput_movement_turn_degrees_per_tick,
      g_xinput_vibration_enabled ? 1u : 0u,
      g_xinput_vibration_strength_percent,
      g_xinput_melee_swing_vibration_ms,
      g_xinput_block_vibration_ms,
      g_xinput_successful_block_vibration_ms,
      g_xinput_spell_cast_vibration_ms,
      g_xinput_ranged_shot_vibration_ms,
      g_xinput_healing_vibration_ms,
      g_xinput_selector_tick_vibration_ms,
      g_xinput_landing_vibration_ms,
      g_xinput_hit_vibration_ms,
      g_xinput_damage_vibration_ms,
      g_xinput_death_vibration_ms,
      g_xinput_heavy_damage_threshold_hp,
      g_xinput_heavy_damage_vibration_ms,
      g_xinput_set_state ? 1u : 0u,
      g_camera_probe_enabled ? 1u : 0u,
      g_third_person_orbit_enabled ? 1u : 0u,
      g_immersive_first_person_enabled ? 1u : 0u,
      static_cast<int>(std::lround(
          g_third_person_orbit_horizontal_radians * 180.0 / kOrbitPi)),
      static_cast<int>(std::lround(
          g_third_person_orbit_vertical_radians * 180.0 / kOrbitPi)),
      minimum_pitch_degrees,
      maximum_pitch_degrees,
      head_minimum_pitch_degrees,
      head_maximum_pitch_degrees,
      g_custom_head_height,
      g_custom_head_forward_offset,
      static_cast<int>(std::lround(g_third_person_orbit_min_radius)),
      static_cast<int>(std::lround(g_third_person_orbit_max_radius)),
      g_third_person_orbit_invert_x ? 1u : 0u,
      g_third_person_orbit_invert_y ? 1u : 0u,
      g_ui_message_lifetime_percent,
      g_ui_message_lifetime_ticks,
      g_ui_message_lifetime_patched ? 1u : 0u,
      g_pst_message_lifetime_ticks,
      g_pst_message_lifetime_patched ? 1u : 0u);
  AppendNativeLog("diagnostics head_joint_probe=%u interval=10 max_candidates=24",
                  g_head_joint_probe_enabled ? 1u : 0u);
  g_state.store(DeathtrapNativeRenderPatchState::kActive,
                std::memory_order_release);
  AppendDeathtrapSupportLog(
      "support_patch state=active orbit=%u immersive=%u xinput=%u "
      "xinput_runtime=%u music_fix=%u",
      g_third_person_orbit_enabled ? 1u : 0u,
      g_immersive_first_person_enabled ? 1u : 0u,
      g_xinput_enabled ? 1u : 0u, g_xinput_get_state ? 1u : 0u,
      g_music_track_fix_enabled ? 1u : 0u);
}

}  // namespace

void AppendDeathtrapSupportLog(const char* format, ...) {
  va_list args;
  va_start(args, format);
  AppendSupportLogV(format, args);
  va_end(args);
}

const wchar_t* GetDeathtrapSessionLogPath() {
  static std::once_flag once;
  static std::wstring path;
  std::call_once(once, [] {
    const std::wstring directory = ModuleDirectory() + L"\\logs";
    if (!CreateDirectoryW(directory.c_str(), nullptr)) {
      const DWORD error = GetLastError();
      if (error != ERROR_ALREADY_EXISTS) {
        path = ModuleDirectory() + L"\\deathtrap-native-fallback.log";
        return;
      }
    }
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t name[128] = {};
    swprintf_s(name, L"deathtrap-native-%04u%02u%02u-%02u%02u%02u-%03u-pid%lu.log",
               static_cast<unsigned>(time.wYear),
               static_cast<unsigned>(time.wMonth),
               static_cast<unsigned>(time.wDay),
               static_cast<unsigned>(time.wHour),
               static_cast<unsigned>(time.wMinute),
               static_cast<unsigned>(time.wSecond),
               static_cast<unsigned>(time.wMilliseconds),
               static_cast<unsigned long>(GetCurrentProcessId()));
    path = directory + L"\\" + name;
  });
  return path.c_str();
}

bool DeathtrapImmersiveFirstPersonActive() {
  return CustomHeadViewSelected() && DeathtrapGameplayReady(false) &&
      !RetailFirstPersonActive();
}

bool DeathtrapImmersiveVectorLocomotionActive() {
  return g_immersive_root_motion_hooks_installed.load(
      std::memory_order_acquire);
}

void SubmitDeathtrapImmersiveKeyboardMovement(int32_t lateral,
                                              int32_t longitudinal) {
  const bool active = DeathtrapImmersiveFirstPersonActive();
  g_immersive_keyboard_movement_x.store(
      active ? std::clamp(lateral, -1, 1) : 0,
      std::memory_order_release);
  g_immersive_keyboard_movement_y.store(
      active ? std::clamp(longitudinal, -1, 1) : 0,
      std::memory_order_release);
}

void QueueDeathtrapWeaponWheelDelta(int32_t delta) {
  if (!g_weapon_wheel_enabled || delta == 0) {
    return;
  }
  // DirectInput reports relative wheel motion in WHEEL_DELTA units. Preserve
  // the sign and a small bounded number of detents without ever delaying the
  // input callback or touching engine state from the input thread.
  int32_t detents = delta / WHEEL_DELTA;
  if (detents == 0) {
    detents = delta > 0 ? 1 : -1;
  }
  int32_t observed =
      g_pending_weapon_wheel_detents.load(std::memory_order_relaxed);
  for (;;) {
    const int32_t desired = std::clamp(observed + detents, -8, 8);
    if (g_pending_weapon_wheel_detents.compare_exchange_weak(
            observed, desired, std::memory_order_release,
            std::memory_order_relaxed)) {
      break;
    }
  }
  g_last_weapon_wheel_event_ms.store(GetTickCount64(),
                                     std::memory_order_relaxed);
}

void PollDeathtrapFrontendXInput() {
  PollFrontendXInputInternal();
}

void SubmitDeathtrapPhysicalMouseDelta(int32_t delta_x, int32_t delta_y) {
  auto accumulate = [](std::atomic<int32_t>* target, int32_t delta) {
    int32_t observed = target->load(std::memory_order_relaxed);
    for (;;) {
      const int32_t desired = std::clamp(observed + delta, -8192, 8192);
      if (target->compare_exchange_weak(observed, desired,
                                        std::memory_order_release,
                                        std::memory_order_relaxed)) {
        return;
      }
    }
  };
  accumulate(&g_third_person_mouse_delta_x, delta_x);
  accumulate(&g_third_person_mouse_delta_y, delta_y);
}

void NotifyDeathtrapOperateInput() {
  const uint64_t now_ms = GetTickCount64();
  g_last_controller_interaction_ms.store(now_ms, std::memory_order_release);
  const uint64_t sequence =
      g_controller_interaction_sequence.fetch_add(
          1u, std::memory_order_acq_rel) +
      1u;
  if (g_debug_log) {
    AppendNativeLog("camera_script interaction=operate time=%llu sequence=%llu",
                    static_cast<unsigned long long>(now_ms),
                    static_cast<unsigned long long>(sequence));
  }
}

bool DeathtrapModernCameraConsumesMouse() {
  if (!g_third_person_orbit_enabled ||
      (g_controller_selector_overlay.load(std::memory_order_acquire) & 1u) !=
          0u) {
    return false;
  }
  // Tab and R3 both enter the retail mode-4 camera. Hand physical mouse axes
  // back immediately; the mode-3 watchdog alone would delay native look and
  // cannot identify an R3 request before the controller mode flips.
  if (g_xinput_first_person_toggled.load(std::memory_order_acquire) ||
      RetailFirstPersonActive()) {
    return false;
  }
  if (!DeathtrapGameplayReady(false)) {
    return false;
  }
  const uint64_t last_mode3_ms =
      g_last_mode3_source_tick_ms.load(std::memory_order_acquire);
  const uint64_t now_ms = GetTickCount64();
  if (!last_mode3_ms || now_ms - last_mode3_ms > 200u) {
    // Pause and frontend screens may keep the player/controller pointers
    // alive, but they stop the gameplay mode-3 camera callback. This watchdog
    // is therefore the authoritative mouse hand-off for physical-mouse runs
    // where no XInput Start transition exists.
    return false;
  }
  CURSORINFO cursor = {};
  cursor.cbSize = sizeof(cursor);
  if (GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) != 0u) {
    // Pause/options screens may retain every gameplay pointer used by
    // DeathtrapGameplayReady.  Their visible native cursor is the reliable
    // frontend ownership signal, including mouse-only runs where no XInput
    // Start transition exists.
    return false;
  }
  // menu_mode is a pause/frontend override maintained by the XInput bridge.
  // Without a connected controller it used to remain true forever, so a real
  // mouse controlled Lara until plugging a pad happened to clear the flag.
  // Ignore that controller-owned override when no controller is present; the
  // native gameplay test still releases the mouse in movies and main menus.
  return !g_xinput_controller_present.load(std::memory_order_acquire) ||
         !g_xinput_menu_mode.load(std::memory_order_acquire);
}

bool DeathtrapGameplayAcceptsMouseCombat() {
  if (!IsGameForeground() ||
      (g_controller_selector_overlay.load(std::memory_order_acquire) & 1u) !=
          0u ||
      !DeathtrapGameplayReady(true)) {
    return false;
  }
  CURSORINFO cursor = {};
  cursor.cbSize = sizeof(cursor);
  if (GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) != 0u) {
    return false;
  }
  return !g_xinput_controller_present.load(std::memory_order_acquire) ||
         !g_xinput_menu_mode.load(std::memory_order_acquire);
}

DeathtrapNativePresentationStage GetDeathtrapNativePresentationStage() {
  return g_active_presentation_trace.stage;
}

uint64_t GetDeathtrapNativePresentationTick() {
  return g_active_presentation_trace.tick;
}

DeathtrapControllerSelectorStatus GetDeathtrapControllerSelectorStatus() {
  const uint32_t packed =
      g_controller_selector_overlay.load(std::memory_order_acquire);
  DeathtrapControllerSelectorStatus status;
  status.visible = (packed & 1u) != 0;
  status.slot_available = (packed & 2u) != 0;
  status.confirmation_required = (packed & 4u) != 0;
  status.category = (packed >> 8u) & 0xFu;
  status.slot = (packed >> 16u) & 0xFu;
  return status;
}

void InitializeDeathtrapNativeRenderPatch() {
  std::call_once(g_patch_once, &InitializePatchState);
}

bool InstallDeathtrapNativeRenderHooks() {
  if (g_render_hook_installed.load(std::memory_order_acquire)) {
    return true;
  }
  if (g_state.load(std::memory_order_acquire) !=
      DeathtrapNativeRenderPatchState::kActive ||
      !g_dungeon_base || !IsExpectedDungeonImage(g_dungeon_base)) {
    return false;
  }

  // Optional and fail-closed: only the exact 25 KiB Steam MP3 wrapper is
  // accepted. Music failure must never disable rendering, input or camera.
  InstallDeathtrapMusicTrackFix();

  // Optional modern-camera layer. It supplies an orbit candidate at the
  // mode-3 dispatcher, clips the exact spring-arm ray with the native camera
  // volume query, then submits the clear endpoint through the common retail
  // configure/history/publication path.
  if (g_third_person_orbit_enabled) {
    g_configure_camera = reinterpret_cast<ConfigureCameraFn>(
        g_dungeon_base + kConfigureCameraRva);
    g_resolve_camera_sector = reinterpret_cast<ResolveCameraSectorFn>(
        g_dungeon_base + kResolveCameraSectorRva);
    g_camera_volume_visible = reinterpret_cast<CameraVolumeVisibleFn>(
        g_dungeon_base + kCameraVolumeVisibleRva);
    g_camera_room_trace_visible = reinterpret_cast<CameraRoomTraceVisibleFn>(
        g_dungeon_base + kCameraRoomTraceVisibleRva);
    void* const camera_history_add_target =
        g_dungeon_base + kCameraHistoryAddRva;
    const MH_STATUS create_history_add = MH_CreateHook(
        camera_history_add_target,
        reinterpret_cast<void*>(&HookCameraHistoryAdd),
        reinterpret_cast<void**>(&g_original_camera_history_add));
    const bool history_add_created =
        create_history_add == MH_OK ||
        create_history_add == MH_ERROR_ALREADY_CREATED;
    const MH_STATUS enable_history_add =
        history_add_created ? MH_EnableHook(camera_history_add_target)
                            : create_history_add;
    const bool history_add_enabled =
        enable_history_add == MH_OK ||
        enable_history_add == MH_ERROR_ENABLED;
    if (!history_add_created || !history_add_enabled) {
      AppendNativeLog(
          "camera_pre_history hook=failed create=%d enable=%d "
          "fallback=post_native",
          static_cast<int>(create_history_add),
          static_cast<int>(enable_history_add));
    }
    void* const camera_look_at_target =
        g_dungeon_base + kCameraLookAtRva;
    const MH_STATUS create_look_at = MH_CreateHook(
        camera_look_at_target,
        reinterpret_cast<void*>(&HookCameraLookAt),
        reinterpret_cast<void**>(&g_original_camera_look_at));
    const bool look_at_created =
        create_look_at == MH_OK ||
        create_look_at == MH_ERROR_ALREADY_CREATED;
    const MH_STATUS enable_look_at =
        look_at_created ? MH_EnableHook(camera_look_at_target)
                        : create_look_at;
    const bool look_at_enabled =
        enable_look_at == MH_OK || enable_look_at == MH_ERROR_ENABLED;
    if (!look_at_created || !look_at_enabled) {
      AppendNativeLog(
          "camera_pose_capture hook=failed create=%d enable=%d "
          "fallback=translation_only",
          static_cast<int>(create_look_at),
          static_cast<int>(enable_look_at));
    }
    void* const mode3_camera_target = g_dungeon_base + kMode3CameraRva;
    const MH_STATUS create_camera = MH_CreateHook(
        mode3_camera_target,
        reinterpret_cast<void*>(&HookMode3Camera),
        reinterpret_cast<void**>(&g_original_mode3_camera));
    if (create_camera == MH_OK ||
        create_camera == MH_ERROR_ALREADY_CREATED) {
      const MH_STATUS enable_camera = MH_EnableHook(mode3_camera_target);
      if (enable_camera == MH_OK || enable_camera == MH_ERROR_ENABLED) {
        g_camera_orbit_hook_installed.store(true,
                                             std::memory_order_release);
        AppendNativeLog(
            "camera_orbit hook=active rva=%08llX configure=%08llX "
            "mode3_free_path_override=1 cinematic_arbitration=1 "
            "native_radial_spring=1 native_volume_query=%08llX "
            "focus_offset=%03llX "
            "mesh_props=pre_history_veto+post_native_fallback "
            "forced_commit=0 "
            "pre_history=%u/%08llX pose_capture=%u/%08llX "
            "head_pose=render_only body_safe=1 "
            "transition=SAFE_CUT",
            static_cast<unsigned long long>(kMode3CameraRva),
            static_cast<unsigned long long>(kConfigureCameraRva),
            static_cast<unsigned long long>(kCameraVolumeVisibleRva),
            static_cast<unsigned long long>(kCameraControllerFocusOffset),
            history_add_created && history_add_enabled ? 1u : 0u,
            static_cast<unsigned long long>(kCameraHistoryAddRva),
            look_at_created && look_at_enabled ? 1u : 0u,
            static_cast<unsigned long long>(kCameraLookAtRva));
      } else {
        AppendNativeLog("camera_orbit hook=enable_failed status=%d",
                        static_cast<int>(enable_camera));
      }
    } else {
      AppendNativeLog("camera_orbit hook=create_failed status=%d",
                      static_cast<int>(create_camera));
    }
  }

  // XInput movement enters through the retail DirectInput joystick boundary,
  // so keys.cfg JOY_* bindings and the game's action resolver remain the only
  // movement-input authority. Both hooks are required before W/A/S/D can be
  // released; otherwise UpdateControllerBaseBindings keeps its stable
  // keyboard-backed fallback.
  if (g_xinput_enabled && g_xinput_base_bindings) {
    void* const joystick_poll_target =
        g_dungeon_base + kNativeJoystickPollRva;
    void* const joystick_enabled_target =
        g_dungeon_base + kNativeJoystickEnabledRva;
    const MH_STATUS create_poll = MH_CreateHook(
        joystick_poll_target,
        reinterpret_cast<void*>(&HookNativeJoystickPoll),
        reinterpret_cast<void**>(&g_original_native_joystick_poll));
    const MH_STATUS create_enabled = MH_CreateHook(
        joystick_enabled_target,
        reinterpret_cast<void*>(&HookNativeJoystickEnabled),
        reinterpret_cast<void**>(&g_original_native_joystick_enabled));
    const bool poll_created =
        create_poll == MH_OK || create_poll == MH_ERROR_ALREADY_CREATED;
    const bool enabled_created =
        create_enabled == MH_OK ||
        create_enabled == MH_ERROR_ALREADY_CREATED;
    const MH_STATUS enable_poll = poll_created
                                      ? MH_EnableHook(joystick_poll_target)
                                      : create_poll;
    const MH_STATUS enable_enabled =
        enabled_created ? MH_EnableHook(joystick_enabled_target)
                        : create_enabled;
    const bool poll_enabled =
        enable_poll == MH_OK || enable_poll == MH_ERROR_ENABLED;
    const bool enabled_enabled =
        enable_enabled == MH_OK || enable_enabled == MH_ERROR_ENABLED;
    if (poll_created && enabled_created && poll_enabled && enabled_enabled) {
      g_native_joystick_hooks_installed.store(true,
                                               std::memory_order_release);
      AppendNativeLog(
          "xinput movement native_joystick=active poll_rva=%08llX "
          "enabled_rva=%08llX range=0..16384 keyboard_wasd=off",
          static_cast<unsigned long long>(kNativeJoystickPollRva),
          static_cast<unsigned long long>(kNativeJoystickEnabledRva));
    } else {
      AppendNativeLog(
          "xinput movement native_joystick=failed create=%d/%d enable=%d/%d "
          "fallback=keyboard_tank",
          static_cast<int>(create_poll), static_cast<int>(create_enabled),
          static_cast<int>(enable_poll), static_cast<int>(enable_enabled));
    }
  }

  // 0x82750 is the common pre-state boundary installed by ordinary ground
  // movement, forward locomotion and both direct turn states. It is also the
  // immersive keyboard vector boundary, so installation must not depend on a
  // connected or enabled XInput device. The canonical 0x44DD0 writer keeps
  // render and collision headings in one transaction.
  if (g_third_person_orbit_enabled &&
      (g_xinput_camera_relative_movement ||
       g_immersive_first_person_enabled)) {
    if (g_immersive_first_person_enabled) {
      InstallImmersiveRootMotionHooks();
    }
    void* const dispatcher_target =
        g_dungeon_base + kPlayerStateDispatcherRva;
    constexpr std::array<uint8_t, 12> kExpectedDispatcherPrologue = {
        0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C,
        0x8B, 0xB7, 0x14, 0x01, 0x00, 0x00};
    std::array<uint8_t, kExpectedDispatcherPrologue.size()>
        dispatcher_prologue{};
    const bool signature_matches =
        SafeRead(dispatcher_target, dispatcher_prologue.data(),
                 dispatcher_prologue.size()) &&
        dispatcher_prologue == kExpectedDispatcherPrologue;
    if (signature_matches) {
      g_player_turn_writer = reinterpret_cast<PlayerTurnFn>(
          g_dungeon_base + kPlayerTurnRva);
      const MH_STATUS create_dispatcher = MH_CreateHook(
          dispatcher_target,
          reinterpret_cast<void*>(&HookPlayerStateDispatcher),
          reinterpret_cast<void**>(&g_original_player_state_dispatcher));
      const bool dispatcher_created =
          create_dispatcher == MH_OK ||
          create_dispatcher == MH_ERROR_ALREADY_CREATED;
      const MH_STATUS enable_dispatcher =
          dispatcher_created ? MH_EnableHook(dispatcher_target)
                             : create_dispatcher;
      const bool dispatcher_enabled =
          enable_dispatcher == MH_OK ||
          enable_dispatcher == MH_ERROR_ENABLED;
      if (dispatcher_created && dispatcher_enabled) {
        g_player_state_dispatcher_hook_installed.store(
            true, std::memory_order_release);
        AppendNativeLog(
            "xinput movement dispatcher_heading=active "
            "dispatcher_rva=%08llX writer_rva=%08llX units=1024 "
            "native_horizontal=neutral",
            static_cast<unsigned long long>(kPlayerStateDispatcherRva),
            static_cast<unsigned long long>(kPlayerTurnRva));
      } else {
        AppendNativeLog(
            "xinput movement dispatcher_heading=failed create=%d enable=%d "
            "fallback=native_joystick_tank",
            static_cast<int>(create_dispatcher),
            static_cast<int>(enable_dispatcher));
      }
    } else {
      AppendNativeLog(
          "xinput movement dispatcher_heading=signature_mismatch "
          "rva=%08llX fallback=native_joystick_tank",
          static_cast<unsigned long long>(kPlayerStateDispatcherRva));
    }
  }

  void* const inventory_slot_target =
      g_dungeon_base + kInventorySlotDrawRva;
  const MH_STATUS create_inventory_slot = MH_CreateHook(
      inventory_slot_target, reinterpret_cast<void*>(&HookInventorySlotDraw),
      reinterpret_cast<void**>(&g_original_inventory_slot_draw));
  if (create_inventory_slot != MH_OK &&
      create_inventory_slot != MH_ERROR_ALREADY_CREATED) {
    return false;
  }
  const MH_STATUS enable_inventory_slot = MH_EnableHook(inventory_slot_target);
  if (enable_inventory_slot != MH_OK &&
      enable_inventory_slot != MH_ERROR_ENABLED) {
    return false;
  }

  void* const renderer_target = g_dungeon_base + kRendererRva;
  const MH_STATUS create_renderer = MH_CreateHook(
      renderer_target, reinterpret_cast<void*>(&HookRenderer),
      reinterpret_cast<void**>(&g_original_renderer));
  if (create_renderer != MH_OK &&
      create_renderer != MH_ERROR_ALREADY_CREATED) {
    MH_DisableHook(inventory_slot_target);
    return false;
  }
  const MH_STATUS enable_renderer = MH_EnableHook(renderer_target);
  if (enable_renderer != MH_OK && enable_renderer != MH_ERROR_ENABLED) {
    MH_DisableHook(inventory_slot_target);
    return false;
  }

  void* const scheduler_target = g_dungeon_base + kRenderPresentWaitRva;
  const MH_STATUS create = MH_CreateHook(
      scheduler_target, reinterpret_cast<void*>(&HookRenderPresentWait),
      reinterpret_cast<void**>(&g_original_render_present_wait));
  if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
    MH_DisableHook(renderer_target);
    MH_DisableHook(inventory_slot_target);
    return false;
  }
  const MH_STATUS enable = MH_EnableHook(scheduler_target);
  if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
    MH_DisableHook(renderer_target);
    MH_DisableHook(inventory_slot_target);
    return false;
  }

  // Optional event feedback. Failure must never disable the stable native-50
  // renderer or controller input layer.
  void* const damage_target = g_dungeon_base + kDamageHandlerRva;
  const MH_STATUS create_damage = MH_CreateHook(
      damage_target, reinterpret_cast<void*>(&HookDamageHandler),
      reinterpret_cast<void**>(&g_original_damage_handler));
  if (create_damage == MH_OK || create_damage == MH_ERROR_ALREADY_CREATED) {
    const MH_STATUS enable_damage = MH_EnableHook(damage_target);
    if (enable_damage == MH_OK || enable_damage == MH_ERROR_ENABLED) {
      g_damage_hook_installed.store(true, std::memory_order_release);
      AppendNativeLog("game_event damage_hook=active rva=%08llX",
                      static_cast<unsigned long long>(kDamageHandlerRva));
    } else {
      AppendNativeLog("game_event damage_hook=enable_failed status=%d",
                      static_cast<int>(enable_damage));
    }
  } else {
    AppendNativeLog("game_event damage_hook=create_failed status=%d",
                    static_cast<int>(create_damage));
  }

  // The retail melee window evaluator compares the current animation frame
  // with the attack descriptor's start/end markers. Its rising edge is the
  // closest engine-owned marker for the weapon downstroke and exists even
  // when no target is hit. Like damage feedback, failure is non-fatal.
  void* const melee_window_target =
      g_dungeon_base + kMeleeAttackWindowRva;
  const MH_STATUS create_melee_window = MH_CreateHook(
      melee_window_target, reinterpret_cast<void*>(&HookMeleeAttackWindow),
      reinterpret_cast<void**>(&g_original_melee_attack_window));
  if (create_melee_window == MH_OK ||
      create_melee_window == MH_ERROR_ALREADY_CREATED) {
    const MH_STATUS enable_melee_window = MH_EnableHook(melee_window_target);
    if (enable_melee_window == MH_OK ||
        enable_melee_window == MH_ERROR_ENABLED) {
      g_melee_attack_window_hook_installed.store(true,
                                                  std::memory_order_release);
      AppendNativeLog("game_event melee_window_hook=active rva=%08llX",
                      static_cast<unsigned long long>(
                          kMeleeAttackWindowRva));
    } else {
      AppendNativeLog("game_event melee_window_hook=enable_failed status=%d",
                      static_cast<int>(enable_melee_window));
    }
  } else {
    AppendNativeLog("game_event melee_window_hook=create_failed status=%d",
                    static_cast<int>(create_melee_window));
  }

  // The retail collision code reaches 0x834F0 only after confirming that the
  // struck actor is already in a block state. The function switches that
  // actor to block-impact animation 0x61, making it an exact successful-block
  // event rather than an input or generic combat-sound approximation.
  void* const combat_impact_target =
      g_dungeon_base + kSuccessfulBlockImpactRva;
  const MH_STATUS create_combat_impact = MH_CreateHook(
      combat_impact_target,
      reinterpret_cast<void*>(&HookSuccessfulBlockImpact),
      reinterpret_cast<void**>(&g_original_successful_block_impact));
  if (create_combat_impact == MH_OK ||
      create_combat_impact == MH_ERROR_ALREADY_CREATED) {
    const MH_STATUS enable_combat_impact =
        MH_EnableHook(combat_impact_target);
    if (enable_combat_impact == MH_OK ||
        enable_combat_impact == MH_ERROR_ENABLED) {
      g_combat_impact_hook_installed.store(true,
                                            std::memory_order_release);
      AppendNativeLog(
          "game_event successful_block_hook=active rva=%08llX "
          "impact_animation=97",
          static_cast<unsigned long long>(kSuccessfulBlockImpactRva));
    } else {
      AppendNativeLog(
          "game_event successful_block_hook=enable_failed status=%d",
          static_cast<int>(enable_combat_impact));
    }
  } else {
    AppendNativeLog(
        "game_event successful_block_hook=create_failed status=%d",
        static_cast<int>(create_combat_impact));
  }

  // 0x1D210 is the retail offensive-spell projectile factory. A non-null
  // return means the launch preconditions passed and a projectile was really
  // created. Healing/utility actions do not use the 15..21 projectile range.
  void* const spell_cast_target =
      g_dungeon_base + kOffensiveSpellLaunchRva;
  const MH_STATUS create_spell_cast = MH_CreateHook(
      spell_cast_target,
      reinterpret_cast<void*>(&HookOffensiveSpellLaunch),
      reinterpret_cast<void**>(&g_original_offensive_spell_launch));
  if (create_spell_cast == MH_OK ||
      create_spell_cast == MH_ERROR_ALREADY_CREATED) {
    const MH_STATUS enable_spell_cast = MH_EnableHook(spell_cast_target);
    if (enable_spell_cast == MH_OK ||
        enable_spell_cast == MH_ERROR_ENABLED) {
      g_spell_cast_hook_installed.store(true, std::memory_order_release);
      AppendNativeLog(
          "game_event offensive_spell_hook=active rva=%08llX ids=%d..%d",
          static_cast<unsigned long long>(kOffensiveSpellLaunchRva),
          kFirstOffensiveSpellId, kLastOffensiveSpellId);
    } else {
      AppendNativeLog("game_event spell_cast_hook=enable_failed status=%d",
                      static_cast<int>(enable_spell_cast));
    }
  } else {
    AppendNativeLog("game_event spell_cast_hook=create_failed status=%d",
                    static_cast<int>(create_spell_cast));
  }

  // 0x1CEC0 is the retail ranged-projectile factory. It returns null when
  // the actor cannot fire (including missing ammunition), so this hook never
  // vibrates merely because RT was pressed.
  void* const ranged_shot_target =
      g_dungeon_base + kRangedWeaponLaunchRva;
  const MH_STATUS create_ranged_shot = MH_CreateHook(
      ranged_shot_target,
      reinterpret_cast<void*>(&HookRangedWeaponLaunch),
      reinterpret_cast<void**>(&g_original_ranged_weapon_launch));
  if (create_ranged_shot == MH_OK ||
      create_ranged_shot == MH_ERROR_ALREADY_CREATED) {
    const MH_STATUS enable_ranged_shot = MH_EnableHook(ranged_shot_target);
    if (enable_ranged_shot == MH_OK ||
        enable_ranged_shot == MH_ERROR_ENABLED) {
      g_ranged_weapon_hook_installed.store(true, std::memory_order_release);
      AppendNativeLog("game_event ranged_projectile_hook=active rva=%08llX",
                      static_cast<unsigned long long>(
                          kRangedWeaponLaunchRva));
    } else {
      AppendNativeLog(
          "game_event ranged_projectile_hook=enable_failed status=%d",
          static_cast<int>(enable_ranged_shot));
    }
  } else {
    AppendNativeLog(
        "game_event ranged_projectile_hook=create_failed status=%d",
        static_cast<int>(create_ranged_shot));
  }

  // The common consumable dispatcher is shared by all eight F4 slots. Read
  // the engine-owned player health on both sides and signal only a real
  // increase, which excludes utility items and failed/full-health attempts.
  void* const consumable_target = g_dungeon_base + kUseConsumableRva;
  const MH_STATUS create_consumable = MH_CreateHook(
      consumable_target, reinterpret_cast<void*>(&HookUseConsumable),
      reinterpret_cast<void**>(&g_original_use_consumable));
  if (create_consumable == MH_OK ||
      create_consumable == MH_ERROR_ALREADY_CREATED) {
    const MH_STATUS enable_consumable = MH_EnableHook(consumable_target);
    if (enable_consumable == MH_OK ||
        enable_consumable == MH_ERROR_ENABLED) {
      g_consumable_hook_installed.store(true, std::memory_order_release);
      AppendNativeLog("game_event consumable_hook=active rva=%08llX "
                      "qualification=health_increase",
                      static_cast<unsigned long long>(kUseConsumableRva));
    } else {
      AppendNativeLog("game_event consumable_hook=enable_failed status=%d",
                      static_cast<int>(enable_consumable));
    }
  } else {
    AppendNativeLog("game_event consumable_hook=create_failed status=%d",
                    static_cast<int>(create_consumable));
  }

  // Preserve only the measured collision-projection callbacks required by
  // exact player contact handling. The completed v0.0.207-v0.0.214 movement
  // investigation no longer patches the 95 broad gameplay-loop callsites.
  InstallMovementDispatcherCallbackProbe();
  g_render_hook_installed.store(true, std::memory_order_release);
  return true;
}

DeathtrapNativeRenderPatchStatus GetDeathtrapNativeRenderPatchStatus() {
  DeathtrapNativeRenderPatchStatus status;
  status.state = g_state.load(std::memory_order_acquire);
  status.subframes = g_subframes.load(std::memory_order_relaxed);
  status.original_rate = kOriginalGameplayRate;
  status.requested_rate = kOriginalGameplayRate * status.subframes;
  status.effective_rate =
      status.subframes >= 3u ? 50u
                             : (status.subframes == 2u
                                    ? 33u
                                    : kOriginalGameplayRate);
  status.last_scheduler_request = kOriginalGameplayRate;
  status.forced_scheduler_requests = 0;
  status.scheduler_hook_installed =
      g_render_hook_installed.load(std::memory_order_relaxed);
  status.source_ticks = g_source_ticks.load(std::memory_order_relaxed);
  status.interpolated_frames =
      g_interpolated_frames.load(std::memory_order_relaxed);
  status.interpolated_nodes =
      g_interpolated_nodes.load(std::memory_order_relaxed);
  return status;
}

const char* DeathtrapNativeRenderPatchStateName(
    DeathtrapNativeRenderPatchState state) {
  switch (state) {
    case DeathtrapNativeRenderPatchState::kDisabled:
      return "OFF";
    case DeathtrapNativeRenderPatchState::kActive:
      return "ACTIVE";
    case DeathtrapNativeRenderPatchState::kDungeonModuleMissing:
      return "DUNGEON.DLL NOT FOUND";
    case DeathtrapNativeRenderPatchState::kUnsupportedDungeonDll:
      return "UNSUPPORTED DUNGEON.DLL";
    case DeathtrapNativeRenderPatchState::kUnexpectedOriginalValue:
      return "ORIGINAL RATE ALREADY MODIFIED";
    case DeathtrapNativeRenderPatchState::kWriteFailed:
      return "MEMORY PATCH FAILED";
    default:
      return "NOT ATTEMPTED";
  }
}
