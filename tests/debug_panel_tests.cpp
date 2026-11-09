#include "core/debug_panel.hpp"
#include <stdexcept>
#include <limits>
#include <string>
#include <iostream>
void require(bool condition) { if (!condition) throw std::runtime_error("Debug panel layout assertion failed"); }
int main() {
    using namespace conduit;
    const auto width = [](const char*, std::size_t size) { return size*6; };
    require(layoutDebugPanel("", 640, 360, width).count == 0);
    require(layoutDebugPanel("x", 0, 360, width).count == 0);
    require(layoutDebugPanel("x", std::numeric_limits<float>::quiet_NaN(), 360, width).count == 0);
    const auto normal = layoutDebugPanel("Title\nScreen: native\n\nFPS: 240", 640, 360, width);
    require(normal.count == 4 && normal.left == 8 && normal.top == 8);
    require(normal.width == 14*6+8 && normal.height == 4*12+8);
    const auto small = layoutDebugPanel(std::string(400, 'x').c_str(), 100, 100, width);
    require(small.count == 1 && small.width <= 84 && std::strstr(small.rows[0].data(), "..."));
    std::string lots = "Title\nScreen\nFactories: 32\n";
    for (int i=0; i<32; ++i) lots += "    factory:\n      children: 7\n      elements: 280\n";
    lots += "FPS: 240\nReloads: 2";
    const auto constrained = layoutDebugPanel(lots.c_str(), 360, 150, width);
    require(constrained.height+16 <= 150 && constrained.width+16 <= 360);
    require(std::string(constrained.rows[constrained.count-1].data()) == "Reloads: 2");
    bool notice = false;
    for (unsigned i=0; i<constrained.count; ++i) if (std::strstr(constrained.rows[i].data(), "more factories")) notice = true;
    require(notice);
    const auto expanded = layoutDebugPanel(lots.c_str(), 640, 600, width);
    require(expanded.count > 0 && expanded.height+16 <= 600);
    require(std::string(expanded.rows[expanded.count-1].data()) == "Reloads: 2");
    const auto scaled = layoutDebugPanel("Title\nScreen: native\n\nFPS: 240", 640, 360, width, 2.0f);
    require(scaled.height == 2*normal.height && scaled.fontScale == 2.0f);
    require(layoutDebugPanel("x", 640, 360, width, 0).count == 0);
    DebugPanelMailbox mailbox;
    DebugPanelText copied;
    require(mailbox.tryRead(copied) && !copied.visible);
    mailbox.publish("first", true);
    require(mailbox.tryRead(copied) && copied.visible && std::strcmp(copied.text.data(), "first") == 0);
    const auto revision = copied.revision;
    mailbox.publish("first", true); mailbox.tryRead(copied); require(copied.revision == revision);
    mailbox.publish("first", true, 2.0f); mailbox.tryRead(copied);
    require(copied.scale == 2.0f && copied.revision != revision);
    mailbox.publish("second", false); mailbox.tryRead(copied);
    require(!copied.visible && copied.revision != revision && std::strcmp(copied.text.data(), "second") == 0);
    std::cout << "Logical viewport layout, clipping, factory omission, footer preservation and copied publication passed\n";
}
