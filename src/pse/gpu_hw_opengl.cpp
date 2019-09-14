#include "gpu_hw_opengl.h"
#include "YBaseLib/Assert.h"
#include "YBaseLib/Log.h"
#include "host_interface.h"
#include "system.h"
Log_SetChannel(GPU_HW_OpenGL);

GPU_HW_OpenGL::GPU_HW_OpenGL() : GPU_HW() {}

GPU_HW_OpenGL::~GPU_HW_OpenGL()
{
  DestroyFramebuffer();
}

bool GPU_HW_OpenGL::Initialize(System* system, Bus* bus, DMA* dma)
{
  if (!GPU_HW::Initialize(system, bus, dma))
    return false;

  CreateFramebuffer();
  CreateVertexBuffer();
  if (!CompilePrograms())
    return false;

  return true;
}

void GPU_HW_OpenGL::Reset()
{
  GPU_HW::Reset();

  ClearFramebuffer();
}

std::tuple<s32, s32> GPU_HW_OpenGL::ConvertToFramebufferCoordinates(s32 x, s32 y)
{
  return std::make_tuple(x, static_cast<s32>(static_cast<s32>(VRAM_HEIGHT) - y));
}

void GPU_HW_OpenGL::CreateFramebuffer()
{
  m_framebuffer_texture =
    std::make_unique<GL::Texture>(VRAM_WIDTH, VRAM_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, false);

  glGenFramebuffers(1, &m_framebuffer_fbo_id);
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_fbo_id);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_framebuffer_texture->GetGLId(), 0);
  Assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

  m_texture_page_texture =
    std::make_unique<GL::Texture>(TEXTURE_PAGE_WIDTH, TEXTURE_PAGE_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, false);
  glGenFramebuffers(1, &m_texture_page_fbo_id);
  glBindFramebuffer(GL_FRAMEBUFFER, m_texture_page_fbo_id);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture_page_texture->GetGLId(), 0);
  Assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
}

void GPU_HW_OpenGL::ClearFramebuffer()
{
  // TODO: get rid of the FBO switches
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_fbo_id);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  m_system->GetHostInterface()->SetDisplayTexture(m_framebuffer_texture.get(), 0, 0, VRAM_WIDTH, VRAM_HEIGHT);
}

void GPU_HW_OpenGL::DestroyFramebuffer()
{
  glDeleteFramebuffers(1, &m_texture_page_fbo_id);
  m_texture_page_fbo_id = 0;
  m_texture_page_texture.reset();

  glDeleteFramebuffers(1, &m_framebuffer_fbo_id);
  m_framebuffer_fbo_id = 0;
  m_framebuffer_texture.reset();
}

void GPU_HW_OpenGL::CreateVertexBuffer()
{
  glGenBuffers(1, &m_vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, 128, nullptr, GL_STREAM_DRAW);

  glGenVertexArrays(1, &m_vao_id);
  glBindVertexArray(m_vao_id);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribIPointer(0, 2, GL_INT, sizeof(HWVertex), reinterpret_cast<void*>(offsetof(HWVertex, x)));
  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, true, sizeof(HWVertex),
                        reinterpret_cast<void*>(offsetof(HWVertex, color)));
  glVertexAttribPointer(2, 2, GL_UNSIGNED_BYTE, true, sizeof(HWVertex),
                        reinterpret_cast<void*>(offsetof(HWVertex, texcoord)));
  glBindVertexArray(0);

  glGenVertexArrays(1, &m_attributeless_vao_id);
}

bool GPU_HW_OpenGL::CompilePrograms()
{
  bool result = true;
  result &= CompileProgram(m_color_program, false, false);
  result &= CompileProgram(m_texture_program, true, false);
  result &= CompileProgram(m_blended_texture_program, true, true);
  if (!result)
    return false;

  const std::string screen_quad_vs = GenerateScreenQuadVertexShader();
  for (u32 palette_size = 0; palette_size < static_cast<u32>(m_texture_page_programs.size()); palette_size++)
  {
    const std::string fs = GenerateTexturePageFragmentShader(static_cast<TextureColorMode>(palette_size));

    GL::Program& prog = m_texture_page_programs[palette_size];
    if (!prog.Compile(screen_quad_vs.c_str(), fs.c_str()))
      return false;

    prog.BindFragData(0, "o_col0");

    if (!prog.Link())
      return false;

    prog.RegisterUniform("samp0");
    prog.RegisterUniform("base_offset");
    prog.RegisterUniform("palette_offset");
    prog.Bind();
    prog.Uniform1i(0, 0);
  }

  return true;
}

bool GPU_HW_OpenGL::CompileProgram(GL::Program& prog, bool textured, bool blending)
{
  const std::string vs = GenerateVertexShader(textured);
  const std::string fs = GenerateFragmentShader(textured, blending);
  if (!prog.Compile(vs.c_str(), fs.c_str()))
    return false;

  prog.BindAttribute(0, "a_pos");
  prog.BindAttribute(1, "a_col0");
  if (textured)
    prog.BindAttribute(2, "a_tex0");

  prog.BindFragData(0, "o_col0");

  if (!prog.Link())
    return false;

  prog.Bind();

  if (textured)
  {
    prog.RegisterUniform("samp0");
    prog.Uniform1i(0, 0);
  }

  return true;
}

void GPU_HW_OpenGL::SetProgram(bool textured, bool blending)
{
  const GL::Program& prog = textured ? (blending ? m_blended_texture_program : m_texture_program) : m_color_program;
  prog.Bind();
}

void GPU_HW_OpenGL::SetViewport()
{
  int x, y, width, height;
  CalcViewport(&x, &y, &width, &height);

  y = VRAM_HEIGHT - y - height;
  Log_DebugPrintf("SetViewport: Offset (%d,%d) Size (%d, %d)", x, y, width, height);
  glViewport(x, y, width, height);
}

void GPU_HW_OpenGL::SetScissor() {}

inline u32 ConvertRGBA5551ToRGBA8888(u16 color)
{
  u8 r = Truncate8(color & 31);
  u8 g = Truncate8((color >> 5) & 31);
  u8 b = Truncate8((color >> 10) & 31);
  u8 a = Truncate8((color >> 15) & 1);

  // 00012345 -> 1234545
  b = (b << 3) | (b >> 3);
  g = (g << 3) | (g >> 3);
  r = (r << 3) | (r >> 3);
  a = a ? 255 : 0;

  return ZeroExtend32(r) | (ZeroExtend32(g) << 8) | (ZeroExtend32(b) << 16) | (ZeroExtend32(a) << 24);
}

void GPU_HW_OpenGL::UpdateDisplay()
{
  GPU_HW::UpdateDisplay();
  m_system->GetHostInterface()->SetDisplayTexture(m_framebuffer_texture.get(), 0, 0, VRAM_WIDTH, VRAM_HEIGHT);
}

void GPU_HW_OpenGL::FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color)
{
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_fbo_id);

  glEnable(GL_SCISSOR_TEST);
  glScissor(x, VRAM_HEIGHT - y - height, width, height);

  const auto [r, g, b, a] = RGBA8ToFloat(color);
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
}

void GPU_HW_OpenGL::UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data)
{
  std::vector<u32> rgba_data;
  rgba_data.reserve(width * height);

  // reverse copy the rows so it matches opengl's lower-left origin
  const u32 source_stride = width * sizeof(u16);
  const u8* source_ptr = static_cast<const u8*>(data) + (source_stride * (height - 1));
  for (u32 row = 0; row < height; row++)
  {
    const u8* source_row_ptr = source_ptr;

    for (u32 col = 0; col < width; col++)
    {
      u16 src_col;
      std::memcpy(&src_col, source_row_ptr, sizeof(src_col));
      source_row_ptr += sizeof(src_col);

      const u32 dst_col = ConvertRGBA5551ToRGBA8888(src_col);
      rgba_data.push_back(dst_col);
    }

    source_ptr -= source_stride;
  }

  m_framebuffer_texture->Bind();

  // lower-left origin flip happens here
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, VRAM_HEIGHT - y - height, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                  rgba_data.data());
}

void GPU_HW_OpenGL::UpdateTexturePageTexture()
{
  glBindFramebuffer(GL_FRAMEBUFFER, m_texture_page_fbo_id);
  m_framebuffer_texture->Bind();

  glDisable(GL_BLEND);
  glViewport(0, 0, TEXTURE_PAGE_WIDTH, TEXTURE_PAGE_HEIGHT);
  glBindVertexArray(m_attributeless_vao_id);

  const GL::Program& prog = m_texture_page_programs[static_cast<u8>(m_texture_config.color_mode)];
  prog.Bind();

  const float base_x = static_cast<float>(m_texture_config.base_x) * (1.0f / static_cast<float>(VRAM_WIDTH));
  const float base_y = static_cast<float>(m_texture_config.base_y) * (1.0f / static_cast<float>(VRAM_HEIGHT));
  prog.Uniform2f(1, base_x, base_y);

  if (m_texture_config.color_mode >= GPU::TextureColorMode::Palette4Bit)
  {
    const float palette_x = static_cast<float>(m_texture_config.palette_x) * (1.0f / static_cast<float>(VRAM_WIDTH));
    const float palette_y = static_cast<float>(m_texture_config.palette_y) * (1.0f / static_cast<float>(VRAM_HEIGHT));
    prog.Uniform2f(2, palette_x, palette_y);
  }

  glDrawArrays(GL_TRIANGLES, 0, 3);

  m_framebuffer_texture->Unbind();
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_fbo_id);
}

void GPU_HW_OpenGL::FlushRender()
{
  if (m_batch_vertices.empty())
    return;

  SetProgram(m_batch_command.texture_enable, m_batch_command.texture_blending_raw);
  SetViewport();

  if (m_batch_command.texture_enable)
    m_texture_page_texture->Bind();

  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_fbo_id);
  glBindVertexArray(m_vao_id);

  glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizei>(sizeof(HWVertex) * m_batch_vertices.size()),
               m_batch_vertices.data(), GL_STREAM_DRAW);
  glVertexAttribIPointer(0, 2, GL_INT, sizeof(HWVertex), reinterpret_cast<void*>(offsetof(HWVertex, x)));
  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, true, sizeof(HWVertex),
                        reinterpret_cast<void*>(offsetof(HWVertex, color)));
  glVertexAttribPointer(2, 2, GL_UNSIGNED_BYTE, true, sizeof(HWVertex),
                        reinterpret_cast<void*>(offsetof(HWVertex, texcoord)));

  const bool is_strip = ((m_batch_command.primitive == Primitive::Polygon && m_batch_command.quad_polygon) ||
                         m_batch_command.primitive == Primitive::Rectangle);
  glDrawArrays(is_strip ? GL_TRIANGLE_STRIP : GL_TRIANGLES, 0, static_cast<GLsizei>(m_batch_vertices.size()));

  m_batch_vertices.clear();
}

std::unique_ptr<GPU> GPU::CreateHardwareOpenGLRenderer()
{
  return std::make_unique<GPU_HW_OpenGL>();
}