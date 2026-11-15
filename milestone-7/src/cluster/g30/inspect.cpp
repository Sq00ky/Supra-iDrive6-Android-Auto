#include "native/native_screen.hpp"
int main() {
    kombi::pip::screen::ScreenOutput output={};
    bool ok=output.inspect();
    bool closed=output.close();
    if(ok&&closed)printf("SCREEN_INSPECT_OK secondary=800x480\n");
    return ok&&closed?0:1;
}
