// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_renderer.h"

#include "src/ui/lib/escher/debug/debug_rects.h"
#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/scene/viewing_volume.h"
#include "src/ui/lib/escher/test/fixtures/readback_test.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/types/color_histogram.h"
#include "src/ui/lib/escher/util/image_utils.h"

#include <vulkan/vulkan.hpp>

namespace {
using namespace escher;

// Extends ReadbackTest by providing a ready-to-use DebugFont instance.
class PaperRendererTest : public ReadbackTest {
 protected:
  // |ReadbackTest|
  void SetUp() override {
    ReadbackTest::SetUp();

    escher()->shader_program_factory()->filesystem()->InitializeWithRealFiles(
        {"shaders/model_renderer/main.frag", "shaders/model_renderer/main.vert",
         "shaders/model_renderer/default_position.vert",
         "shaders/model_renderer/shadow_map_generation.frag",
         "shaders/model_renderer/shadow_map_lighting.frag",
         "shaders/model_renderer/wobble_position.vert", "shaders/paper/common/use.glsl",
         "shaders/paper/frag/main_ambient_light.frag", "shaders/paper/frag/main_point_light.frag",
         "shaders/paper/vert/compute_model_space_position.vert",
         "shaders/paper/vert/compute_world_space_position.vert",
         "shaders/paper/vert/main_shadow_volume_extrude.vert",
         "shaders/paper/vert/vertex_attributes.vert"});
    ren = PaperRenderer::New(escher());
  }

  // |ReadbackTest|
  void TearDown() override {
    ren.reset();
    ReadbackTest::TearDown();
  }

  escher::PaperRenderer* renderer() const { return ren.get(); }

 public:
  escher::PaperRendererPtr ren;
};

VK_TEST_F(PaperRendererTest, Text) {
  constexpr uint32_t kNumPixelsPerGlyph = 7 * 7;

  constexpr ColorBgra kWhite(255, 255, 255, 255);
  constexpr ColorBgra kBlack(0, 0, 0, 255);
  constexpr ColorBgra kTransparentBlack(0, 0, 0, 0);
  // Expects PaperRenderer's background color to be transparent.

  for (int32_t scale = 1; scale <= 4; ++scale) {
    const int32_t scale_squared = scale * scale;

    ReadbackTest::FrameData fd = NewFrame(vk::ImageLayout::eColorAttachmentOptimal);
    auto& frame = fd.frame;

    escher::PaperScenePtr scene = fxl::MakeRefCounted<PaperScene>();
    scene->point_lights.resize(1);
    scene->bounding_box = BoundingBox(vec3(0), vec3(kFramebufferHeight));

    const escher::ViewingVolume& volume = ViewingVolume(scene->bounding_box);
    escher::Camera cam = escher::Camera::NewOrtho(volume);
    std::vector<Camera> cameras = {cam};

    // |expected_black| is the total number of black pixels *within* the glyphs
    // *before* scaling.  In other words, black background pixels outside of the
    // glyph bounds are not counted.  Also, consider the glyph "!" which has 4
    // black pixels all in one vertical column (3 black, 1 white, 1 black)...
    // if the scale is 2 then both the width and height are doubled so the
    // number of black pixels in the glyph after scaling is 16.
    std::function<void(std::string, size_t)> draw_and_check_histogram = [&](std::string glyphs,
                                                                            size_t expected_black) {
      ren->BeginFrame(frame, scene, cameras, fd.color_attachment);
      ren->DrawDebugText(glyphs, {0, 10 * scale}, scale);
      ren->EndFrame();

      size_t expected_white =
          (glyphs.length() * kNumPixelsPerGlyph - expected_black) * scale_squared;

      auto bytes = ReadbackFromColorAttachment(frame, fd.color_attachment->swapchain_layout(),
                                               vk::ImageLayout::eColorAttachmentOptimal);

      const ColorHistogram<ColorBgra> histogram(bytes.data(),
                                                kFramebufferWidth * kFramebufferHeight);

      EXPECT_EQ(3U, histogram.size());
      EXPECT_EQ(histogram[kWhite], expected_white)
          << "FAILED WHILE DRAWING \"" << glyphs << "\" AT SCALE: " << scale;
      EXPECT_EQ(histogram[kBlack], expected_black * scale_squared)
          << "FAILED WHILE DRAWING \"" << glyphs << "\" AT SCALE: " << scale;

      EXPECT_EQ(histogram[kTransparentBlack],
                kNumFramebufferPixels - (scale_squared * kNumPixelsPerGlyph * glyphs.length()));
    };

    // Each time, we draw on top of the previous glyph.
    draw_and_check_histogram("1", 5);
    draw_and_check_histogram("A", 12);
    draw_and_check_histogram("!", 4);

    // Draw a glyph that has not been defined, it should draw a black square.
    draw_and_check_histogram("Z", 25);

    // Draw several glyphs next to each other.
    draw_and_check_histogram(" 1A!", 0 + 5 + 12 + 4);

    frame->EndFrame(SemaphorePtr(), []() {});
  }
  escher()->vk_device().waitIdle();
  ASSERT_TRUE(escher()->Cleanup());
}

// Tests that vertical and horizontal lines of a specific color are drawn correctly when called.
// It does this by checking the number of pixels drawn of the specified color against what is
// expected. Colors are created in debug_rects.h and take the format |kColor|.
VK_TEST_F(PaperRendererTest, Lines) {
  for (int32_t thickness = 1; thickness <= 4; ++thickness) {
    ReadbackTest::FrameData fd = NewFrame(vk::ImageLayout::eColorAttachmentOptimal);
    auto& frame = fd.frame;

    escher::PaperScenePtr scene = fxl::MakeRefCounted<PaperScene>();
    scene->point_lights.resize(1);
    scene->bounding_box = BoundingBox(vec3(0), vec3(kFramebufferHeight));

    const escher::ViewingVolume& volume = ViewingVolume(scene->bounding_box);
    escher::Camera cam = escher::Camera::NewOrtho(volume);
    std::vector<Camera> cameras = {cam};

    std::function<int32_t(FramePtr, DebugRects::Color)> get_data = [&](FramePtr frame,
                                                                       DebugRects::Color kColor) {
      auto bytes = ReadbackFromColorAttachment(frame, vk::ImageLayout::eColorAttachmentOptimal,
                                               vk::ImageLayout::eColorAttachmentOptimal);
      const ColorHistogram<ColorBgra> histogram(bytes.data(),
                                                kFramebufferWidth * kFramebufferHeight);
      EXPECT_EQ(2U, histogram.size());

      ColorRgba c = DebugRects::colorData[kColor];
      ColorBgra b = ColorBgra(c.r, c.g, c.b, c.a);

      return histogram[b];
    };

    // Draws verticle and horizontal lines of |KColor| starting at (0, 0) and going to |endCoord|.
    std::function<void(DebugRects::Color, uint8_t)> draw_and_check_histogram =
        [&](DebugRects::Color kColor, uint8_t endCoord) {
          auto expected_colored = endCoord * thickness;

          ren->BeginFrame(frame, scene, cameras, fd.color_attachment);
          ren->DrawVLine(kColor, 0, 0, endCoord, thickness);
          ren->EndFrame();

          auto returned_colored = get_data(frame, kColor);
          EXPECT_EQ(expected_colored, returned_colored)
              << "FAILED WHILE DRAWING VERTICAL LINE OF COLOR \"" << kColor
              << "\" AT THICKNESS: " << thickness;

          ren->BeginFrame(frame, scene, cameras, fd.color_attachment);
          ren->DrawHLine(kColor, 0, 0, endCoord, thickness);
          ren->EndFrame();

          returned_colored = get_data(frame, kColor);
          EXPECT_EQ(returned_colored, expected_colored)
              << "FAILED WHILE DRAWING HORIZONTAL LINE OF COLOR \"" << kColor
              << "\" AT THICKNESS: " << thickness;
        };

    draw_and_check_histogram(escher::DebugRects::kPurple, (uint8_t)500);
    draw_and_check_histogram(escher::DebugRects::kRed, (uint8_t)800);
    draw_and_check_histogram(escher::DebugRects::kYellow, (uint8_t)200);

    frame->EndFrame(SemaphorePtr(), []() {});
  }
  escher()->vk_device().waitIdle();
  ASSERT_TRUE(escher()->Cleanup());
}

}  // namespace
