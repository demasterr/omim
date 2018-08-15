#pragma once

#include "drape/debug_renderer.hpp"
#include "drape/gpu_program.hpp"
#include "drape/mesh_object.hpp"
#include "drape/render_state.hpp"

#include "shaders/program_params.hpp"

#include "geometry/rect2d.hpp"
#include "geometry/screenbase.hpp"

#ifdef BUILD_DESIGNER
#define RENDER_DEBUG_RECTS
#endif // BUILD_DESIGNER

namespace df
{
class DebugRectRenderer: public dp::MeshObject, public dp::IDebugRenderer
{
  using TBase = dp::MeshObject;
public:
  static ref_ptr<DebugRectRenderer> Instance();

  void Init(ref_ptr<dp::GpuProgram> program, ref_ptr<gpu::ProgramParamsSetter> paramsSetter);
  void Destroy();
  void SetEnabled(bool enabled);

  bool IsEnabled() const override;
  void DrawRect(ref_ptr<dp::GraphicsContext> context, ScreenBase const & screen,
                m2::RectF const & rect, dp::Color const & color) override;
  void DrawArrow(ref_ptr<dp::GraphicsContext> context, ScreenBase const & screen,
                 dp::OverlayTree::DisplacementData const & data) override;

private:
  DebugRectRenderer();
  ~DebugRectRenderer() override;

  void SetArrow(m2::PointF const & arrowStart, m2::PointF const & arrowEnd, ScreenBase const & screen);
  void SetRect(m2::RectF const & rect, ScreenBase const & screen);

  ref_ptr<dp::GpuProgram> m_program;
  ref_ptr<gpu::ProgramParamsSetter> m_paramsSetter;
  dp::RenderState m_state;

  bool m_isEnabled = false;
};
}  // namespace df

