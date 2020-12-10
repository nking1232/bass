#include "text_emu.h"

#include <ansi/console.h>
#include <ansi/terminal.h>
#include <coreutils/algorithm.h>
#include <coreutils/utf8.h>
#include <thread>

#include "emulator.h"
#include "petscii.h"

#include <array>
#include <chrono>

using namespace std::chrono_literals;
static constexpr std::array<uint8_t, 16 * 3> c64pal = {
    0x00, 0x00, 0x00, // BLACK
    0xFF, 0xFF, 0xFF, // WHITE
    0x68, 0x37, 0x2B, // RED
    0x70, 0xA4, 0xB2, // CYAN
    0x6F, 0x3D, 0x86, // PURPLE
    0x58, 0x8D, 0x43, // GREEN
    0x35, 0x28, 0x79, // BLUE
    0xB8, 0xC7, 0x6F, // YELLOW
    0x6F, 0x4F, 0x25, // ORANGE
    0x43, 0x39, 0x00, // BROWN
    0x9A, 0x67, 0x59, // LIGHT_READ
    0x44, 0x44, 0x44, // DARK_GREY
    0x6C, 0x6C, 0x6C, // GREY
    0x9A, 0xD2, 0x84, // LIGHT_GREEN
    0x6C, 0x5E, 0xB5, // LIGHT_BLUE
    0x95, 0x95, 0x95, // LIGHT_GREY
};

std::string translate(uint8_t c)
{
    char32_t u = sc2uni_up(c);
    std::u32string s = {u};
    return utils::utf8_encode(s);
}

void TextEmu::set_color(uint8_t col)
{
    int b = (col & 0xf) * 3;
    int f = ((col >> 4) + 16) * 3;
    uint32_t fg =
        (palette[f] << 24) | (palette[f + 1] << 16) | (palette[f + 2] << 8);
    uint32_t bg =
        (palette[b] << 24) | (palette[b + 1] << 16) | (palette[b + 2] << 8);
    console->set_color(fg, bg);
}

void TextEmu::writeChar(uint16_t adr, uint8_t t)
{
    if (regs[RealW] == 0 || regs[RealH] == 0) {
        return;
    }
    auto offset = adr - (regs[TextPtr] * 256);
    textRam[offset] = t;

    auto x = (offset % regs[WinW] + regs[WinX]) % regs[RealW];
    auto y = (offset / regs[WinW] + regs[WinY]) % regs[RealH];
    console->set_xy(x, y);
    auto c = colorRam[offset];
    if ((t & 0x80) != 0) {
        c = ((c << 4) & 0xf0) | (c >> 4);
    }
    set_color(c);
    console->put(translate(t));
}

void TextEmu::writeColor(uint16_t adr, uint8_t c)
{
    if (regs[RealW] == 0 || regs[RealH] == 0) {
        return;
    }
    auto offset = adr - (regs[ColorPtr] * 256);
    auto t = textRam[offset];
    colorRam[offset] = c;

    auto x = (offset % regs[WinW] + regs[WinX]) % regs[RealW];
    auto y = (offset / regs[WinW] + regs[WinY]) % regs[RealH];
    console->set_xy(x, y);
    if ((t & 0x80) != 0) {
        c = ((c << 4) & 0xf0) | (c >> 4);
    }
    set_color(c);

    console->put(translate(t));
}

void TextEmu::updateRegs()
{

    auto sz = (regs[WinW] * regs[WinH] + 255) & 0xffff00;

    textRam.resize(sz);
    colorRam.resize(sz);

    auto banks = sz / 256;
    emu->map_write_callback(regs[TextPtr], banks, this,
                          [](uint16_t adr, uint8_t v, void* data) {
                              auto* thiz = static_cast<TextEmu*>(data);
                              thiz->writeChar(adr, v);
                          });
    emu->map_read_callback(regs[TextPtr], banks, this,
                         [](uint16_t adr, void* data) {
                             auto* thiz = static_cast<TextEmu*>(data);
                             auto offset = adr - (thiz->regs[TextPtr] * 256);
                             return thiz->textRam[offset];
                         });
    emu->map_write_callback(regs[ColorPtr], banks, this,
                          [](uint16_t adr, uint8_t v, void* data) {
                              auto* thiz = static_cast<TextEmu*>(data);
                              thiz->writeColor(adr, v);
                          });
    emu->map_read_callback(regs[ColorPtr], banks, this,
                         [](uint16_t adr, void* data) {
                             auto* thiz = static_cast<TextEmu*>(data);
                             auto offset = adr - (thiz->regs[ColorPtr] * 256);
                             return thiz->colorRam[offset];
                         });
}

void TextEmu::fillOutside(uint8_t col)
{
    auto cw = console->get_width();
    auto ch = console->get_height();
    set_color(col);
    for (size_t y = 0; y < ch; y++) {
        for (size_t x = 0; x < cw; x++) {
            if (x < regs[WinX] || x >= (regs[WinX] + regs[WinW]) ||
                y < regs[WinY] || y >= (regs[WinY] + regs[WinH])) {
                console->set_xy(x, y);
                console->put(" ");
            }
        }
    }
}

TextEmu::TextEmu()
{
    auto terminal = bbs::create_local_terminal();
    terminal->open();
    console = std::make_unique<bbs::Console>(std::move(terminal));
    logging::setAltMode(true);

    uint8_t cw = console->get_width() > 255 ? 0 : static_cast<uint8_t>(console->get_width());
    uint8_t ch = console->get_height();

    emu = std::make_unique<sixfive::Machine<>>();

    emu->map_read_callback(0xd7, 1, this,
                         [](uint16_t adr, void* data) -> uint8_t {
                             auto* thiz = static_cast<TextEmu*>(data);
                             if (adr >= 0xd780) {
                                 return thiz->palette[adr - 0xd780];
                             }
                             return thiz->readReg(adr & 0xff);
                         });

    emu->map_write_callback(0xd7, 1, this,
                          [](uint16_t adr, uint8_t v, void* data) {
                              auto* thiz = static_cast<TextEmu*>(data);
                              if (adr < 0xd780) {
                                  thiz->writeReg(adr & 0xff, v);
                              } else {
                                  thiz->palette[adr - 0xd780] = v;
                              }
                          });
    regs[WinH] = 25;
    regs[WinW] = 40;
    regs[WinX] = (cw - regs[WinW]) / 2;
    regs[WinY] = (ch - regs[WinH]) / 2;
    regs[RealW] = cw;
    regs[RealH] = ch;

    regs[TextPtr] = 0x04;
    regs[ColorPtr] = 0xd8;
    regs[Freq] = 20;
    regs[IrqE] = 0;
    regs[IrqR] = 0;

    updateRegs();
    utils::fill(textRam, 0x20);
    utils::fill(colorRam, 0x01);
    utils::copy(c64pal, palette.data());
    utils::copy(c64pal, palette.data() + 16 * 3);
}

void TextEmu::run(uint16_t pc)
{
    /*

       set start_t
       run for n cycles
       calc t/cycle

       if t > nextUpdate
         update

       */
    start(pc);
    int32_t cycles = 1000;
    auto t = clk::now();

    nextUpdate = t + 20ms;

    while (true) {
        emu->run(cycles);
        auto tpc = (clk::now() - t).count() / cycles;

        if (t >= nextUpdate) {
            console->flush();
            regs[RealW] = console->get_width();
            regs[RealH] = console->get_height();

            if ((regs[Control] & 1) == 1) {
                return;
            }

            nextUpdate += 20ms;
        }
    }
}

uint16_t TextEmu::get_ticks() const
{
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(clk::now() -
                                                                    start_t);
    return (ms.count() / 100) & 0xffff;
}

uint8_t TextEmu::readReg(int reg)
{
    switch (reg) {
    case WinX:
    case WinY:
    case WinW:
    case WinH:
    case TextPtr:
    case ColorPtr:
    case Charset:
    case IrqR:
    case IrqE:
        return regs[reg];
    case Keys:
        if(auto key = console->read_key()) {
            return key;
        }
        return 0;
    case TimerLo:
        return get_ticks() & 0xff;
    case TimerHi:
        return (get_ticks() >> 8) & 0xff;
    case RealW:
        return console->get_width();
    case RealH:
        return console->get_height();
    default:
        return 0;
    }
}

void TextEmu::writeReg(int reg, uint8_t val)
{
    switch (reg) {
    case WinX:
    case WinY:
    case WinW:
    case WinH:
    case TextPtr:
    case ColorPtr:
        regs[reg] = val;
        updateRegs();
        break;
    case Charset:
        regs[reg] = val;
        break;
    case CFillOut:
        fillOutside(val);
        break;
    case Control:
        if ((val & 1) != 0) {
            throw exit_exception();
        } else if((val & 2) != 0) {
            std::this_thread::sleep_until(nextUpdate);
            doUpdate();
        }
        break;
    case IrqR:
        regs[IrqR] ^= val;
        break;
    default:
        break;
    }
}

void TextEmu::doUpdate()
{
    console->flush();

    auto oldR = regs[IrqR];
    regs[IrqR] |= 1;

    if ((~oldR & regs[IrqR]) != 0) {
        if ((regs[IrqR] & regs[IrqE]) != 0) {
            emu->irq(emu->read_mem(0xfffc) | (emu->read_mem(0xfffd) << 8));
        }
    }
}

bool TextEmu::update()
{
    uint32_t cycles = 1;
    try {
        cycles = emu->run(10000);
    } catch (exit_exception&) {
        return true;
    }

    if(clk::now() >= nextUpdate) {
        nextUpdate += 20ms;
        doUpdate();
    } else {
        std::this_thread::sleep_for(1ms);
    }

    return cycles != 0;
}

void TextEmu::load(uint16_t start, uint8_t const* ptr, size_t size) const
{
    constexpr uint8_t Sys_Token = 0x9e;
    constexpr uint8_t Space = 0x20;

    if (start == 0x0801) {
        const auto* p = ptr;
        size_t sz = size > 12 ? 12 : size;
        const auto* endp = ptr + sz;

        while (*p != Sys_Token && p < endp)
            p++;
        p++;
        while (*p == Space && p < endp)
            p++;
        if (p < endp) {
            basicStart = std::atoi(reinterpret_cast<char const*>(p));
            fmt::print("Detected basic start at {:04x}", basicStart);
        }
    }
    emu->write_memory(start, ptr, size);
}

void TextEmu::start(uint16_t pc)
{
    if (pc == 0x0801 && basicStart >= 0) {
        pc = basicStart;
    }

    fillOutside(0x01);

    emu->setPC(pc);
    start_t = clk::now();
    nextUpdate = start_t + 10ms;
}

int TextEmu::get_width() const
{
    return console->get_width();
}
int TextEmu::get_height() const
{
    return console->get_height();
}

TextEmu::~TextEmu() = default;
