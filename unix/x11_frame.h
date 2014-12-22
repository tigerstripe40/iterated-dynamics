#if !defined(X11_FRAME_H)
#define X11_FRAME_H

#include <X11/X.h>

class x11_frame_window
{
public:
    int width() const { return 0; }
    int height() const { return 0; }
    Window window() const { return 0; }
    int get_key_press(int option) { return 0; }
    int pump_messages(bool wait_flag) { return 0; }
};

#endif
