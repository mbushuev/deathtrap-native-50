// Exercise the actual proxy callbacks with a fake DirectInput driver. Only
// gameplay readiness, frontend polling and the camera sink are replaced.
#define DeathtrapModernCameraConsumesMouse TestCameraConsumesMouse
#define PollDeathtrapFrontendXInput TestPollFrontend
#define SubmitDeathtrapPhysicalMouseDelta TestSubmitMouse
#include "../src/dinput_native_proxy.cc"
#include <iostream>

static int32_t received_x = 0, received_y = 0;
static bool owns_mouse = true;
bool TestCameraConsumesMouse() { return owns_mouse; }
void TestPollFrontend() {}
void TestSubmitMouse(int32_t x, int32_t y) {
  received_x += x; received_y += y;
}
static DIDEVICEOBJECTDATA queued{};
static LONG next_x = 0, next_y = 0;
HRESULT STDMETHODCALLTYPE FakeState(IDirectInputDeviceA*, DWORD size, void* p) {
  std::memset(p, 0, size);
  auto* state = static_cast<DIMOUSESTATE*>(p);
  state->lX = next_x; state->lY = next_y;
  next_x = next_y = 0;
  return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeData(IDirectInputDeviceA*, DWORD size,
    LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD) {
  if (*count) { std::memcpy(data, &queued, size); *count = 1; }
  return DI_OK;
}
int main() {
  auto* mouse = reinterpret_cast<IDirectInputDeviceA*>(0x1000);
  auto* keyboard = reinterpret_cast<IDirectInputDeviceA*>(0x2000);
  g_support_mouse_device.store(mouse);
  g_support_mouse_data_size.store(sizeof(DIMOUSESTATE));
  g_direct_input_device_get_state = FakeState;
  g_direct_input_device_get_data = FakeData;
  DIDEVICEOBJECTDATA out{};
  DWORD count = 1;
  queued.dwOfs = DIK_3; queued.dwData = 0x80;
  HookDirectInputDeviceGetData(keyboard, sizeof(out), &out, &count, 0);
  if (count != 1 || out.dwOfs != DIK_3 || out.dwData != 0x80 || received_y)
    return 1;
  // A relative movement read at presentation rate remains in the independent
  // event queue. Reading that queue at the source tick must not replay it.
  next_x = 12; next_y = -5;
  int32_t x = 0, y = 0;
  if (!PollDeathtrapPresentationMouseDeltaInternal(&x, &y) || x != 12 || y != -5)
    return 2;
  TestSubmitMouse(x, y);
  queued.dwOfs = DIMOFS_X; queued.dwData = 12;
  count = 1;
  HookDirectInputDeviceGetData(mouse, sizeof(out), &out, &count, 0);
  if (count || received_x != 12 || received_y != -5) return 3;
  // Peek also hides camera axes without consuming or duplicating motion.
  count = 1;
  HookDirectInputDeviceGetData(mouse, sizeof(out), &out, &count, DIGDD_PEEK);
  if (count || received_x != 12) return 4;
  queued.dwOfs = DIMOFS_BUTTON1; queued.dwData = 0x80;
  count = 1;
  HookDirectInputDeviceGetData(mouse, sizeof(out), &out, &count, 0);
  if (count != 1 || out.dwOfs != DIMOFS_BUTTON1 || out.dwData != 0x80) return 5;
  // Frontend mouse axes must remain available to the native menu.
  owns_mouse = false;
  queued.dwOfs = DIMOFS_X; queued.dwData = 7;
  count = 1;
  HookDirectInputDeviceGetData(mouse, sizeof(out), &out, &count, 0);
  if (count != 1 || out.dwData != 7 || received_x != 12) return 6;
  std::cout << "PASS actual callbacks: keyboard 3 preserved; motion counted once; "
               "peek, buttons and menu axes preserved\n";
}
