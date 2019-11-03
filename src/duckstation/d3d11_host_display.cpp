#include "d3d11_host_display.h"
#include "YBaseLib/Log.h"
#include "common/d3d11/shader_compiler.h"
#include <SDL_syswm.h>
#include <array>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl.h>
Log_SetChannel(D3D11HostDisplay);

class D3D11HostDisplayTexture : public HostDisplayTexture
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  D3D11HostDisplayTexture(ComPtr<ID3D11Texture2D> texture, ComPtr<ID3D11ShaderResourceView> srv, u32 width, u32 height,
                          bool dynamic)
    : m_texture(std::move(texture)), m_srv(std::move(srv)), m_width(width), m_height(height), m_dynamic(dynamic)
  {
  }
  ~D3D11HostDisplayTexture() override = default;

  void* GetHandle() const override { return m_srv.Get(); }
  u32 GetWidth() const override { return m_width; }
  u32 GetHeight() const override { return m_height; }

  ID3D11Texture2D* GetD3DTexture() const { return m_texture.Get(); }
  ID3D11ShaderResourceView* GetD3DSRV() const { return m_srv.Get(); }
  bool IsDynamic() const { return m_dynamic; }

  static std::unique_ptr<D3D11HostDisplayTexture> Create(ID3D11Device* device, u32 width, u32 height, const void* data,
                                                         u32 data_stride, bool dynamic)
  {
    const CD3D11_TEXTURE2D_DESC desc(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, D3D11_BIND_SHADER_RESOURCE,
                                     dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT,
                                     dynamic ? D3D11_CPU_ACCESS_WRITE : 0, 1, 0, 0);
    const D3D11_SUBRESOURCE_DATA srd{data, data_stride, data_stride * height};
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&desc, data ? &srd : nullptr, texture.GetAddressOf());
    if (FAILED(hr))
      return {};

    const CD3D11_SHADER_RESOURCE_VIEW_DESC srv_desc(D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 1, 0,
                                                    1);
    ComPtr<ID3D11ShaderResourceView> srv;
    hr = device->CreateShaderResourceView(texture.Get(), &srv_desc, srv.GetAddressOf());
    if (FAILED(hr))
      return {};

    return std::make_unique<D3D11HostDisplayTexture>(std::move(texture), std::move(srv), width, height, dynamic);
  }

private:
  ComPtr<ID3D11Texture2D> m_texture;
  ComPtr<ID3D11ShaderResourceView> m_srv;
  u32 m_width;
  u32 m_height;
  bool m_dynamic;
};

D3D11HostDisplay::D3D11HostDisplay(SDL_Window* window) : m_window(window)
{
  SDL_GetWindowSize(window, &m_window_width, &m_window_height);
}

D3D11HostDisplay::~D3D11HostDisplay()
{
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplSDL2_Shutdown();

  if (m_window)
    SDL_DestroyWindow(m_window);
}

HostDisplay::RenderAPI D3D11HostDisplay::GetRenderAPI() const
{
  return HostDisplay::RenderAPI::D3D11;
}

void* D3D11HostDisplay::GetHostRenderDevice() const
{
  return m_device.Get();
}

void* D3D11HostDisplay::GetHostRenderContext() const
{
  return m_context.Get();
}

std::unique_ptr<HostDisplayTexture> D3D11HostDisplay::CreateTexture(u32 width, u32 height, const void* data,
                                                                    u32 data_stride, bool dynamic)
{
  return D3D11HostDisplayTexture::Create(m_device.Get(), width, height, data, data_stride, dynamic);
}

void D3D11HostDisplay::UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height, const void* data,
                                     u32 data_stride)
{
  D3D11HostDisplayTexture* d3d11_texture = static_cast<D3D11HostDisplayTexture*>(texture);
  if (!d3d11_texture->IsDynamic())
  {
    const CD3D11_BOX dst_box(x, y, 0, x + width, y + height, 1);
    m_context->UpdateSubresource(d3d11_texture->GetD3DTexture(), 0, &dst_box, data, data_stride, data_stride * height);
  }
  else
  {
    D3D11_MAPPED_SUBRESOURCE sr;
    HRESULT hr = m_context->Map(d3d11_texture->GetD3DTexture(), 0, D3D11_MAP_WRITE_DISCARD, 0, &sr);
    if (FAILED(hr))
      Panic("Failed to map dynamic host display texture");

    char* dst_ptr = static_cast<char*>(sr.pData) + (y * sr.RowPitch) + (x * sizeof(u32));
    const char* src_ptr = static_cast<const char*>(data);
    if (sr.RowPitch == data_stride)
    {
      std::memcpy(dst_ptr, src_ptr, data_stride * height);
    }
    else
    {
      for (u32 row = 0; row < height; row++)
      {
        std::memcpy(dst_ptr, src_ptr, width * sizeof(u32));
        src_ptr += data_stride;
        dst_ptr += sr.RowPitch;
      }
    }

    m_context->Unmap(d3d11_texture->GetD3DTexture(), 0);
  }
}

void D3D11HostDisplay::SetDisplayTexture(void* texture, u32 offset_x, u32 offset_y, u32 width, u32 height,
                                         u32 texture_width, u32 texture_height, float aspect_ratio)
{
  m_display_srv = static_cast<ID3D11ShaderResourceView*>(texture);
  m_display_offset_x = offset_x;
  m_display_offset_y = offset_y;
  m_display_width = width;
  m_display_height = height;
  m_display_texture_width = texture_width;
  m_display_texture_height = texture_height;
  m_display_aspect_ratio = aspect_ratio;
  m_display_texture_changed = true;
}

void D3D11HostDisplay::SetDisplayLinearFiltering(bool enabled)
{
  m_display_linear_filtering = enabled;
}

void D3D11HostDisplay::SetVSync(bool enabled)
{
  m_vsync = enabled;
}

std::tuple<u32, u32> D3D11HostDisplay::GetWindowSize() const
{
  return std::make_tuple(static_cast<u32>(m_window_width), static_cast<u32>(m_window_height));
}

void D3D11HostDisplay::WindowResized()
{
  SDL_GetWindowSize(m_window, &m_window_width, &m_window_height);

  m_swap_chain_rtv.Reset();

  HRESULT hr = m_swap_chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr))
    Log_ErrorPrintf("ResizeBuffers() failed: 0x%08X", hr);

  if (!CreateSwapChainRTV())
    Panic("Failed to recreate swap chain RTV after resize");
}

bool D3D11HostDisplay::CreateD3DDevice()
{
  const bool debug = true;

  SDL_SysWMinfo syswm = {};
  if (!SDL_GetWindowWMInfo(m_window, &syswm))
  {
    Log_ErrorPrintf("SDL_GetWindowWMInfo failed");
    return false;
  }

  UINT create_flags = 0;
  if (debug)
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;

  DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
  swap_chain_desc.BufferDesc.Width = m_window_width;
  swap_chain_desc.BufferDesc.Height = m_window_height;
  swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_desc.SampleDesc.Count = 1;
  swap_chain_desc.BufferCount = 3;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.OutputWindow = syswm.info.win.window;
  swap_chain_desc.Windowed = TRUE;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, create_flags, nullptr, 0,
                                             D3D11_SDK_VERSION, &swap_chain_desc, m_swap_chain.GetAddressOf(),
                                             m_device.GetAddressOf(), nullptr, m_context.GetAddressOf());
  if (FAILED(hr))
  {
    Log_ErrorPrintf("D3D11CreateDeviceAndSwapChain failed: 0x%08X", hr);
    return false;
  }

  return true;
}

bool D3D11HostDisplay::CreateSwapChainRTV()
{
  ComPtr<ID3D11Texture2D> backbuffer;
  HRESULT hr = m_swap_chain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));
  if (FAILED(hr))
  {
    Log_ErrorPrintf("GetBuffer for RTV failed: 0x%08X", hr);
    return false;
  }

  D3D11_TEXTURE2D_DESC backbuffer_desc;
  backbuffer->GetDesc(&backbuffer_desc);

  CD3D11_RENDER_TARGET_VIEW_DESC rtv_desc(D3D11_RTV_DIMENSION_TEXTURE2D, backbuffer_desc.Format, 0, 0,
                                          backbuffer_desc.ArraySize);
  hr = m_device->CreateRenderTargetView(backbuffer.Get(), &rtv_desc, m_swap_chain_rtv.GetAddressOf());
  if (FAILED(hr))
  {
    Log_ErrorPrintf("CreateRenderTargetView for swap chain failed: 0x%08X", hr);
    return false;
  }

  return true;
}

bool D3D11HostDisplay::CreateD3DResources()
{
  static constexpr char fullscreen_quad_vertex_shader[] = R"(
void main(in uint vertex_id : SV_VertexID,
          out float2 v_tex0 : TEXCOORD0,
          out float4 o_pos : SV_Position)
{
  v_tex0 = float2(float((vertex_id << 1) & 2u), float(vertex_id & 2u));
  o_pos = float4(v_tex0 * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}
)";

  static constexpr char display_pixel_shader[] = R"(
cbuffer UBOBlock : register(b0)
{
  float4 u_src_rect;
};

Texture2D samp0 : register(t0);
SamplerState samp0_ss : register(s0);

void main(in float2 v_tex0 : TEXCOORD0,
          out float4 o_col0 : SV_Target)
{
  float2 coords = u_src_rect.xy + v_tex0 * u_src_rect.zw;
  o_col0 = samp0.Sample(samp0_ss, coords);
}
)";

  HRESULT hr;

  m_display_vertex_shader =
    D3D11::ShaderCompiler::CompileAndCreateVertexShader(m_device.Get(), fullscreen_quad_vertex_shader, false);
  m_display_pixel_shader =
    D3D11::ShaderCompiler::CompileAndCreatePixelShader(m_device.Get(), display_pixel_shader, false);
  if (!m_display_vertex_shader || !m_display_pixel_shader)
    return false;

  if (!m_display_uniform_buffer.Create(m_device.Get(), D3D11_BIND_CONSTANT_BUFFER, DISPLAY_UNIFORM_BUFFER_SIZE))
    return false;

  CD3D11_RASTERIZER_DESC rasterizer_desc = CD3D11_RASTERIZER_DESC(CD3D11_DEFAULT());
  rasterizer_desc.CullMode = D3D11_CULL_NONE;
  hr = m_device->CreateRasterizerState(&rasterizer_desc, m_display_rasterizer_state.GetAddressOf());
  if (FAILED(hr))
    return false;

  CD3D11_DEPTH_STENCIL_DESC depth_stencil_desc = CD3D11_DEPTH_STENCIL_DESC(CD3D11_DEFAULT());
  depth_stencil_desc.DepthEnable = FALSE;
  depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  hr = m_device->CreateDepthStencilState(&depth_stencil_desc, m_display_depth_stencil_state.GetAddressOf());
  if (FAILED(hr))
    return false;

  CD3D11_BLEND_DESC blend_desc = CD3D11_BLEND_DESC(CD3D11_DEFAULT());
  hr = m_device->CreateBlendState(&blend_desc, m_display_blend_state.GetAddressOf());
  if (FAILED(hr))
    return false;

  CD3D11_SAMPLER_DESC sampler_desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  hr = m_device->CreateSamplerState(&sampler_desc, m_point_sampler.GetAddressOf());
  if (FAILED(hr))
    return false;

  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  hr = m_device->CreateSamplerState(&sampler_desc, m_linear_sampler.GetAddressOf());
  if (FAILED(hr))
    return false;

  return true;
}

bool D3D11HostDisplay::CreateImGuiContext()
{
  if (!ImGui_ImplSDL2_InitForD3D(m_window) || !ImGui_ImplDX11_Init(m_device.Get(), m_context.Get()))
    return false;

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplSDL2_NewFrame(m_window);
  return true;
}

std::unique_ptr<HostDisplay> D3D11HostDisplay::Create(SDL_Window* window)
{
  std::unique_ptr<D3D11HostDisplay> display = std::make_unique<D3D11HostDisplay>(window);
  if (!display->CreateD3DDevice() || !display->CreateSwapChainRTV() || !display->CreateD3DResources() ||
      !display->CreateImGuiContext())
  {
    return nullptr;
  }

  return display;
}

void D3D11HostDisplay::Render()
{
  static constexpr std::array<float, 4> clear_color = {};
  m_context->ClearRenderTargetView(m_swap_chain_rtv.Get(), clear_color.data());
  m_context->OMSetRenderTargets(1, m_swap_chain_rtv.GetAddressOf(), nullptr);

  RenderDisplay();

  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

  m_swap_chain->Present(BoolToUInt32(m_vsync), 0);

  ImGui_ImplSDL2_NewFrame(m_window);
  ImGui_ImplDX11_NewFrame();
}

void D3D11HostDisplay::RenderDisplay()
{
  if (!m_display_srv)
    return;

  // - 20 for main menu padding
  auto [vp_left, vp_top, vp_width, vp_height] =
    CalculateDrawRect(m_window_width, std::max(m_window_height - 20, 1), m_display_aspect_ratio);
  vp_top += 20;

  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_context->VSSetShader(m_display_vertex_shader.Get(), nullptr, 0);
  m_context->PSSetShader(m_display_pixel_shader.Get(), nullptr, 0);
  m_context->PSSetShaderResources(0, 1, &m_display_srv);
  m_context->PSSetSamplers(
    0, 1, m_display_linear_filtering ? m_linear_sampler.GetAddressOf() : m_point_sampler.GetAddressOf());

  const float uniforms[4] = {static_cast<float>(m_display_offset_x) / static_cast<float>(m_display_texture_width),
                             static_cast<float>(m_display_offset_y) / static_cast<float>(m_display_texture_height),
                             static_cast<float>(m_display_width) / static_cast<float>(m_display_texture_width),
                             static_cast<float>(m_display_height) / static_cast<float>(m_display_texture_height)};
  const auto map = m_display_uniform_buffer.Map(m_context.Get(), sizeof(uniforms), sizeof(uniforms));
  std::memcpy(map.pointer, uniforms, sizeof(uniforms));
  m_display_uniform_buffer.Unmap(m_context.Get(), sizeof(uniforms));
  m_context->PSSetConstantBuffers(0, 1, m_display_uniform_buffer.GetD3DBufferArray());

  const CD3D11_VIEWPORT vp(static_cast<float>(vp_left), static_cast<float>(vp_top), static_cast<float>(vp_width),
                           static_cast<float>(vp_height));
  m_context->RSSetViewports(1, &vp);
  m_context->RSSetState(m_display_rasterizer_state.Get());
  m_context->OMSetDepthStencilState(m_display_depth_stencil_state.Get(), 0);
  m_context->OMSetBlendState(m_display_blend_state.Get(), nullptr, 0xFFFFFFFFu);

  m_context->Draw(3, 0);
}