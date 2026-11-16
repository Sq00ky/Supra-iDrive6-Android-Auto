#include "native/native_screen.hpp"

int main() {
    kombi::pip::screen::ScreenOutput output = {};
    const bool inspected = output.inspect();
    const bool closed = output.close();
    if (inspected && closed) {
        printf("SCREEN_INSPECT_OK secondary=%ux%u\n",
               unsigned(nbt::cluster::VideoProfile::BackingWidth),
               unsigned(nbt::cluster::VideoProfile::BackingHeight));
    }
    return inspected && closed ? 0 : 1;
}
