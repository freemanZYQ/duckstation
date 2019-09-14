#include "system.h"
#include "bus.h"
#include "cpu_core.h"
#include "dma.h"
#include "gpu.h"

System::System(HostInterface* host_interface) : m_host_interface(host_interface)
{
  m_cpu = std::make_unique<CPU::Core>();
  m_bus = std::make_unique<Bus>();
  m_dma = std::make_unique<DMA>();
  // m_gpu = std::make_unique<GPU>();
  m_gpu = GPU::CreateHardwareOpenGLRenderer();
}

System::~System() = default;

bool System::Initialize()
{
  if (!m_cpu->Initialize(m_bus.get()))
    return false;

  if (!m_bus->Initialize(this, m_dma.get(), m_gpu.get()))
    return false;

  if (!m_dma->Initialize(m_bus.get(), m_gpu.get()))
    return false;

  if (!m_gpu->Initialize(this, m_bus.get(), m_dma.get()))
    return false;

  return true;
}

void System::Reset()
{
  m_cpu->Reset();
  m_bus->Reset();
  m_dma->Reset();
  m_gpu->Reset();
  m_frame_number = 1;
}

void System::RunFrame()
{
  u32 current_frame_number = m_frame_number;
  while (current_frame_number == m_frame_number)
    m_cpu->Execute();
}

bool System::LoadEXE(const char* filename)
{
#pragma pack(push, 1)
  struct EXEHeader
  {
    char id[8];            // 0x000-0x007 PS-X EXE
    char pad1[8];          // 0x008-0x00F
    u32 initial_pc;        // 0x010
    u32 initial_gp;        // 0x014
    u32 load_address;      // 0x018
    u32 file_size;         // 0x01C excluding 0x800-byte header
    u32 unk0;              // 0x020
    u32 unk1;              // 0x024
    u32 memfill_start;     // 0x028
    u32 memfill_size;      // 0x02C
    u32 initial_sp_base;   // 0x030
    u32 initial_sp_offset; // 0x034
    u32 reserved[5];       // 0x038-0x04B
    char marker[0x7B4];    // 0x04C-0x7FF
  };
  static_assert(sizeof(EXEHeader) == 0x800);
#pragma pack(pop)

  std::FILE* fp = std::fopen(filename, "rb");
  if (!fp)
    return false;

  EXEHeader header;
  if (std::fread(&header, sizeof(header), 1, fp) != 1)
  {
    std::fclose(fp);
    return false;
  }

  if (header.memfill_size > 0)
  {
    const u32 words_to_write = header.memfill_size / 4;
    u32 address = header.memfill_start & ~UINT32_C(3);
    for (u32 i = 0; i < words_to_write; i++)
    {
      m_cpu->SafeWriteMemoryWord(address, 0);
      address += sizeof(u32);
    }
  }

  if (header.file_size >= 4)
  {
    std::vector<u32> data_words(header.file_size / 4);
    if (std::fread(data_words.data(), header.file_size, 1, fp) != 1)
    {
      std::fclose(fp);
      return false;
    }

    const u32 num_words = header.file_size / 4;
    u32 address = header.load_address;
    for (u32 i = 0; i < num_words; i++)
    {
      m_cpu->SafeWriteMemoryWord(address, data_words[i]);
      address += sizeof(u32);
    }
  }

  std::fclose(fp);

  // patch the BIOS to jump to the executable directly
  {
    const u32 r_pc = header.load_address;
    const u32 r_gp = header.initial_gp;
    const u32 r_sp = header.initial_sp_base;
    const u32 r_fp = header.initial_sp_base + header.initial_sp_offset;

    // pc has to be done first because we can't load it in the delay slot
    m_bus->PatchBIOS(0xBFC06FF0, UINT32_C(0x3C080000) | r_pc >> 16);                // lui $t0, (r_pc >> 16)
    m_bus->PatchBIOS(0xBFC06FF4, UINT32_C(0x35080000) | (r_pc & UINT32_C(0xFFFF))); // ori $t0, $t0, (r_pc & 0xFFFF)
    m_bus->PatchBIOS(0xBFC06FF8, UINT32_C(0x3C1C0000) | r_gp >> 16);                // lui $gp, (r_gp >> 16)
    m_bus->PatchBIOS(0xBFC06FFC, UINT32_C(0x379C0000) | (r_gp & UINT32_C(0xFFFF))); // ori $gp, $gp, (r_gp & 0xFFFF)
    m_bus->PatchBIOS(0xBFC07000, UINT32_C(0x3C1D0000) | r_sp >> 16);                // lui $sp, (r_sp >> 16)
    m_bus->PatchBIOS(0xBFC07004, UINT32_C(0x37BD0000) | (r_sp & UINT32_C(0xFFFF))); // ori $sp, $sp, (r_sp & 0xFFFF)
    m_bus->PatchBIOS(0xBFC07008, UINT32_C(0x3C1E0000) | r_fp >> 16);                // lui $fp, (r_fp >> 16)
    m_bus->PatchBIOS(0xBFC0700C, UINT32_C(0x01000008));                             // jr $t0
    m_bus->PatchBIOS(0xBFC07010, UINT32_C(0x37DE0000) | (r_fp & UINT32_C(0xFFFF))); // ori $fp, $fp, (r_fp & 0xFFFF)
  }

  return true;
}