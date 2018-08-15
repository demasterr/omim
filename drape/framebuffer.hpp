#pragma once

#include "drape/pointers.hpp"
#include "drape/texture.hpp"

#include <cstdint>
#include <functional>

namespace dp
{
class FramebufferTexture: public Texture
{
public:
  ref_ptr<ResourceInfo> FindResource(Key const & key, bool & newResource) override { return nullptr; }
};

using FramebufferFallback = std::function<bool()>;

class Framebuffer
{
public:
  class DepthStencil
  {
  public:
    DepthStencil(bool depthEnabled, bool stencilEnabled);
    ~DepthStencil();
    void SetSize(uint32_t width, uint32_t height);
    void Destroy();
    uint32_t GetDepthAttachmentId() const;
    uint32_t GetStencilAttachmentId() const;
  private:
    bool const m_depthEnabled = false;
    bool const m_stencilEnabled = false;
    uint32_t m_layout = 0;
    uint32_t m_pixelType = 0;
    drape_ptr<FramebufferTexture> m_texture;
  };

  Framebuffer();
  explicit Framebuffer(TextureFormat colorFormat);
  Framebuffer(TextureFormat colorFormat, bool depthEnabled, bool stencilEnabled);
  ~Framebuffer();

  void SetFramebufferFallback(FramebufferFallback && fallback);
  void SetSize(uint32_t width, uint32_t height);
  void SetDepthStencilRef(ref_ptr<DepthStencil> depthStencilRef);
  void ApplyOwnDepthStencil();

  void Enable();
  void Disable();

  ref_ptr<Texture> GetTexture() const;
  ref_ptr<DepthStencil> GetDepthStencilRef() const;

  bool IsSupported() const { return m_isSupported; }
private:
  void Destroy();

  drape_ptr<DepthStencil> m_depthStencil;
  ref_ptr<DepthStencil> m_depthStencilRef;
  drape_ptr<FramebufferTexture> m_colorTexture;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  uint32_t m_framebufferId = 0;
  TextureFormat m_colorFormat;
  FramebufferFallback m_framebufferFallback;
  bool m_isSupported = true;
};
}  // namespace dp
