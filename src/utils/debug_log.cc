#include "tensorflow/lite/micro/debug_log.h"
#include "am_util.h"

// Implementation of DebugLog for TensorFlow Lite Micro
// This redirects TF Lite error messages to UART via am_util_stdio_printf
extern "C" void DebugLog(const char *s)
{
    am_util_stdio_printf("%s", s);
}
