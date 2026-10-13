#include <switch.h>
#include "draw_recorder.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace fnv::deko;
namespace {
FILE* log_file;
PadState pad;
bool console_active;
void Log(const char* text) { if (log_file) { std::fprintf(log_file, "%s\n", text); std::fflush(log_file); } }
[[noreturn]] void Fail(const char* text) {
  Log(text);
  if (console_active) { std::printf("ERROR: %s\n+ para salir\n", text); while (appletMainLoop()) {
    padUpdate(&pad); if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break; consoleUpdate(nullptr);
  } consoleExit(nullptr); }
  std::exit(1);
}
void Debug(void*, const char* context, DkResult result, const char* message) {
  if (log_file) { std::fprintf(log_file, "deko [%s] %d: %s\n", context ? context : "", result,
                               message ? message : ""); std::fflush(log_file); }
}
DkMemBlock Allocate(DkDevice device, uint32_t bytes, uint32_t flags) {
  DkMemBlockMaker maker; dkMemBlockMakerDefaults(&maker, device,
      (bytes + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1)); maker.flags = flags;
  auto block = dkMemBlockCreate(&maker); if (!block) Fail("GPU allocation failed"); return block;
}
bool Wait(DkQueue queue) {
  DkFence fence; dkQueueSignalFence(queue, &fence, true); dkQueueFlush(queue);
  for (unsigned i = 0; i < 50; ++i) {
    auto result = dkFenceWait(&fence, 100000000);
    if (result == DkResult_Success) return true;
    if (result != DkResult_Timeout) Fail("GPU fence failed");
    padUpdate(&pad); if (padGetButtonsDown(&pad) & HidNpadButton_Plus) return false;
  }
  Fail("GPU fence timeout");
}
struct Mesh {
  ShaderModule vertex;
  DkMemBlock memory{};
  std::array<uint32_t, 20> header{};
  DkViewport viewport{};
  DkScissor scissor{};
  std::array<BufferBinding, 4> uniforms{};
  BufferBinding storage{};
  IndexDraw indices{};
};
void Load(DkDevice device, Mesh& mesh, unsigned id) {
  char path[160]; std::snprintf(path, sizeof(path), "sdmc:/switch/FNV-nx/deko-replay/%02u/geometry.bin", id);
  FILE* file = std::fopen(path, "rb"); if (!file) Fail("Missing geometry.bin");
  if (std::fseek(file, 0, SEEK_END)) Fail("Cannot seek geometry");
  long bytes = std::ftell(file); std::rewind(file);
  if (bytes < 128 || bytes > 17 * 1024 * 1024) Fail("Invalid geometry length");
  std::vector<uint8_t> data(bytes);
  if (std::fread(data.data(), 1, data.size(), file) != data.size()) Fail("Truncated geometry");
  std::fclose(file); std::memcpy(mesh.header.data(), data.data(), 80);
  const auto& h = mesh.header;
  if (h[0] != 0x50524E46 || h[1] != 1 || h[18] != data.size() || !h[3] || h[5] ||
      (h[2] != DkPrimitive_Triangles && h[2] != DkPrimitive_TriangleStrip) ||
      (h[4] != DkIdxFormat_Uint16 && h[4] != DkIdxFormat_Uint32)) Fail("Unsupported mesh header");
  auto range = [&](uint32_t offset, uint32_t size) {
    if (offset < 128 || offset % 256 || !size || uint64_t(offset) + size > data.size()) Fail("Invalid mesh buffer range");
  };
  range(h[19], h[7]); range(h[8], h[9]);
  if (uint64_t(h[3]) * (h[4] == DkIdxFormat_Uint16 ? 2 : 4) != h[9]) Fail("Invalid index extent");
  for (unsigned i = 10; i < 18; i += 2) range(h[i], h[i + 1]);
  std::memcpy(&mesh.viewport, data.data() + 80, 24);
  std::memcpy(&mesh.scissor, data.data() + 104, 16);
  mesh.memory = Allocate(device, data.size(), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
  std::memcpy(dkMemBlockGetCpuAddr(mesh.memory), data.data(), data.size());
  auto address = dkMemBlockGetGpuAddr(mesh.memory);
  constexpr unsigned bindings[] = {0, 1, 3, 4};
  for (unsigned i = 0; i < 4; ++i) mesh.uniforms[i] = {bindings[i], {address + h[10 + i * 2], h[11 + i * 2]}};
  mesh.storage = {0, {address + h[19], h[7]}};
  mesh.indices.buffer = {address + h[8], h[9]}; mesh.indices.format = DkIdxFormat(h[4]);
  std::snprintf(path, sizeof(path), "sdmc:/switch/FNV-nx/deko-replay/%02u/vertex.dksh", id);
  if (!mesh.vertex.Load(device, path)) Fail("Cannot load captured vertex shader");
  if (log_file) { std::fprintf(log_file, "Loaded mesh %02u: %u indices, %zu bytes; scissor %u x %u\n",
      id, h[3], data.size(), mesh.scissor.width, mesh.scissor.height); std::fflush(log_file); }
}
}

int main() {
  padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
  log_file = std::fopen("sdmc:/switch/FNV-nx/logs/fnv-deko-replay.log", "w");
  Log("Native deko3d replay: captured geometry/vertex shaders; flat diagnostic fragment stage");
  consoleInit(nullptr); console_active = true;
  std::printf("FNV: geometria real con deko3d\n\nA: iniciar\nIzquierda/derecha: 8 dibujos capturados\nY: recorte original/completo\nX: invertir eje vertical\n+: salir\n\nColor plano, sin texturas ni gameplay.\n");
  bool selected = false;
  while (appletMainLoop()) { padUpdate(&pad); auto buttons = padGetButtonsDown(&pad);
    if (buttons & HidNpadButton_Plus) break;
    if (buttons & HidNpadButton_A) { selected = true; break; } consoleUpdate(nullptr);
  }
  consoleExit(nullptr); console_active = false;
  if (!selected) { if (log_file) std::fclose(log_file); return 0; }
  DkDeviceMaker device_maker; dkDeviceMakerDefaults(&device_maker); device_maker.cbDebug = Debug;
  DkDevice device = dkDeviceCreate(&device_maker); if (!device) Fail("Cannot create device");
  std::array<Mesh, 8> meshes;
  for (unsigned i = 0; i < meshes.size(); ++i) Load(device, meshes[i], i);
  ShaderModule fragment;
  if (!fragment.Load(device, "sdmc:/switch/FNV-nx/deko-replay/flat.dksh")) Fail("Cannot load flat fragment shader");
  DkImageLayoutMaker layout_maker; dkImageLayoutMakerDefaults(&layout_maker, device);
  layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent;
  layout_maker.format = DkImageFormat_RGBA8_Unorm; layout_maker.dimensions[0] = 1280; layout_maker.dimensions[1] = 720;
  DkImageLayout layout; dkImageLayoutInitialize(&layout, &layout_maker);
  uint32_t alignment = dkImageLayoutGetAlignment(&layout);
  uint32_t stride = (dkImageLayoutGetSize(&layout) + alignment - 1) & ~(alignment - 1);
  auto targets_memory = Allocate(device, stride * 2, DkMemBlockFlags_Image | DkMemBlockFlags_GpuCached);
  DkImage images[2]; const DkImage* image_ptrs[2]; DkImageView views[2];
  for (unsigned i = 0; i < 2; ++i) { dkImageInitialize(&images[i], &layout, targets_memory, stride * i);
    image_ptrs[i] = &images[i]; dkImageViewDefaults(&views[i], &images[i]); }
  DkSwapchainMaker swap_maker; dkSwapchainMakerDefaults(&swap_maker, device, nwindowGetDefault(), image_ptrs, 2);
  auto swapchain = dkSwapchainCreate(&swap_maker); if (!swapchain) Fail("Cannot create swapchain");
  DkQueueMaker queue_maker; dkQueueMakerDefaults(&queue_maker, device); queue_maker.flags = DkQueueFlags_Graphics;
  auto queue = dkQueueCreate(&queue_maker); if (!queue) Fail("Cannot create queue");
  auto command_memory = Allocate(device, 65536, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
  DkCmdBufMaker command_maker; dkCmdBufMakerDefaults(&command_maker, device);
  auto commands = dkCmdBufCreate(&command_maker); if (!commands) Fail("Cannot create command buffer");
  DkRasterizerState raster; dkRasterizerStateDefaults(&raster); raster.cullMode = DkFace_None;
  DkDepthStencilState depth; dkDepthStencilStateDefaults(&depth); depth.depthTestEnable = false; depth.depthWriteEnable = false;
  DkColorState color; dkColorStateDefaults(&color);
  DkColorWriteState write; dkColorWriteStateDefaults(&write);
  DkBlendState blend; dkBlendStateDefaults(&blend);
  const DkScissor full_extent{0, 0, 1280, 720};
  unsigned current = 4, frames = 0; bool full_scissor = false, flip_y = false;
  Log("Starting mesh 04; left/right select; Y scissor; + exit");
  while (appletMainLoop()) {
    padUpdate(&pad); auto buttons = padGetButtonsDown(&pad);
    if (buttons & HidNpadButton_Plus) break;
    if (buttons & HidNpadButton_Left) current = (current + 7) % 8;
    if (buttons & HidNpadButton_Right) current = (current + 1) % 8;
    if (buttons & HidNpadButton_Y) full_scissor = !full_scissor;
    if (buttons & HidNpadButton_X) flip_y = !flip_y;
    if (buttons & (HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_Y | HidNpadButton_X)) {
      if (log_file) { std::fprintf(log_file, "Select mesh %02u full_scissor=%u flip_y=%u\n", current, full_scissor, flip_y); std::fflush(log_file); }
    }
    dkCmdBufClear(commands); dkCmdBufAddMemory(commands, command_memory, 0, 65536);
    int slot = dkQueueAcquireImage(queue, swapchain); if (slot < 0 || slot > 1) Fail("Cannot acquire image");
    const DkImageView* target = &views[slot];
    dkCmdBufBindRenderTargets(commands, &target, 1, nullptr);
    dkCmdBufSetScissors(commands, 0, &full_extent, 1);
    dkCmdBufClearColorFloat(commands, 0, DkColorMask_RGBA, 0.03f, 0.06f, 0.12f, 1);
    dkCmdBufBindColorWriteState(commands, &write);
    dkCmdBufBarrier(commands, DkBarrier_None, DkInvalidateFlags_Shader);
    const DkViewportSwizzle swizzle{DkSwizzle_PositiveX, flip_y ? DkSwizzle_NegativeY : DkSwizzle_PositiveY,
                                    DkSwizzle_PositiveZ, DkSwizzle_PositiveW};
    dkCmdBufSetViewportSwizzles(commands, 0, &swizzle, 1);
    auto& mesh = meshes[current];
    NativeDraw draw{}; draw.vertex = &mesh.vertex; draw.fragment = &fragment;
    draw.color_targets = {&target, 1}; draw.rasterizer = &raster; draw.depth_stencil = &depth;
    draw.color = &color; draw.blend = {&blend, 1}; draw.viewport = mesh.viewport;
    draw.scissor = full_scissor ? DkScissor{0, 0, 1280, 720} : mesh.scissor;
    draw.vertex_resources.uniforms = mesh.uniforms;
    draw.vertex_resources.storage = {&mesh.storage, 1}; draw.primitive = DkPrimitive(mesh.header[2]);
    draw.vertices = mesh.header[3]; draw.instances = 1; draw.indices = &mesh.indices;
    if (!RecordNativeDraw(commands, draw)) Fail("Draw validation failed");
    dkQueueSubmitCommands(queue, dkCmdBufFinishList(commands)); dkQueuePresentImage(queue, swapchain, slot);
    if (!Wait(queue)) std::exit(0); // Do not destroy resources while completion is unconfirmed.
    if (dkQueueIsInErrorState(queue)) Fail("GPU queue error");
    if (++frames == 1) Log("First native frame completed; visible output needs user confirmation");
  }
  if (!Wait(queue)) std::exit(0);
  if (log_file) { std::fprintf(log_file, "Normal exit; %u frames completed\n", frames); std::fflush(log_file); }
  dkQueueDestroy(queue); dkCmdBufDestroy(commands); dkSwapchainDestroy(swapchain);
  fragment.Reset(); for (auto& mesh : meshes) { mesh.vertex.Reset(); dkMemBlockDestroy(mesh.memory); }
  dkMemBlockDestroy(command_memory); dkMemBlockDestroy(targets_memory); dkDeviceDestroy(device);
  if (log_file) std::fclose(log_file); return 0;
}
